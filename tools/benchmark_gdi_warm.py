#!/usr/bin/env python3
"""Same-PE cold/warm CPU/GPU comparison with fixed checkpoint/retirement gates."""
import datetime
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import statistics
import subprocess
import sys

runner, fixture, output = map(Path, sys.argv[1:])
output.unlink(missing_ok=True)
proc = subprocess.run([str(runner.resolve()), str(fixture.resolve())], capture_output=True,
                      text=True, timeout=180,
                      env=dict(os.environ, EGL_PLATFORM="surfaceless", LIBGL_ALWAYS_SOFTWARE="1"))
print(proc.stdout, end="")
if proc.returncode:
    print(proc.stderr, file=sys.stderr)
    raise SystemExit(proc.returncode)
assert "Warm ordinary GDI: PASS" in proc.stdout
rows, reads = [], []
for line in proc.stdout.splitlines():
    if line.startswith("WARM_READ "):
        _, sample, mode, elapsed, count = line.split()
        mode = int(mode)
        assert int(count) == (0, 9, 1)[mode]
        assert math.isfinite(float(elapsed)) and float(elapsed) >= 0
        reads.append(dict(sample=int(sample), mode=("cpu", "reference_gpu", "retired_gpu")[mode], guest_read_us=float(elapsed), total_target_reads=int(count)))
    if not line.startswith("WARM "):
        continue
    _, sample, mode, frame, raster, submit, draw, target_reads = line.split()
    sample, mode, frame, target_reads = map(int, (sample, mode, frame, target_reads))
    assert 0 <= sample < 4 and 0 <= mode < 3 and 0 <= frame < 9
    assert target_reads == (frame if mode == 1 else 0)
    row = dict(sample=sample, mode=("cpu", "reference_gpu", "retired_gpu")[mode], frame=frame,
               raster_us=float(raster), owning_submit_us=float(submit), driver_submit_us=float(draw), readbacks=target_reads)
    assert all(math.isfinite(row[k]) and row[k] >= 0 for k in ("raster_us", "owning_submit_us", "driver_submit_us"))
    row["cycle_us"] = row["raster_us"] + row["owning_submit_us"] + row["driver_submit_us"]
    rows.append(row)
assert len(rows) == 108 and len({(r["sample"], r["mode"], r["frame"]) for r in rows}) == 108
assert len(reads)==12 and {(r["sample"],r["mode"]) for r in reads} == {(n,m) for n in range(4) for m in ("cpu", "reference_gpu", "retired_gpu")}
summary = {}
for mode in ("cpu", "reference_gpu", "retired_gpu"):
    summary[mode] = {"guest_read_us": statistics.median(r["guest_read_us"] for r in reads if r["sample"]>0 and r["mode"]==mode)}
    for phase in ("cold", "warm"):
        group = [r for r in rows if r["mode"] == mode and r["sample"] > 0 and ((r["frame"] == 0) == (phase == "cold"))]
        summary[mode][phase] = {k: statistics.median(r[k] for r in group) for k in ("raster_us", "owning_submit_us", "driver_submit_us", "cycle_us")}
        summary[mode][phase].update(samples=len(group), cycle_min_us=min(r["cycle_us"] for r in group), cycle_max_us=max(r["cycle_us"] for r in group))
root = Path(__file__).resolve().parent.parent
report = dict(schema=1, status="PASS_HOST_WARM_GDI_PENDING_HARDWARE",
              head_sha=subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
              working_tree_dirty=bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=root, text=True).strip()),
              utc=datetime.datetime.now(datetime.timezone.utc).isoformat(), host=dict(platform=platform.platform(), machine=platform.machine(), mesa_shader_cache_disable=os.environ.get("MESA_SHADER_CACHE_DISABLE", "unset")),
              runner_sha256=hashlib.sha256(runner.read_bytes()).hexdigest(), fixture_sha256=hashlib.sha256(fixture.read_bytes()).hexdigest(),
              samples=rows, explicit_read_samples=reads, summary=summary,
              counts=dict(sessions=12, frames_per_session=9, calls_per_frame=64, seed_imports_per_gpu_session=1,
                          capacity_reads_reference=8, capacity_reads_retired=0, retired_commands=512,
                          final_explicit_reads_reference=9, final_explicit_reads_retired=1),
              scope="Same build/PE, actual x86 FillRect/PATCOPY, independent full-screen CPU/EGL oracle at each of nine frames. Each session keeps one window/DC/target and production renderer. CPU, retirement-off GPU reference and retirement-on GPU rotate order; sample0 triplet excluded. Cold frame0 includes target/import/lazy GL work, warm frames1..8 use same resource. Wall phases include guest interpreter/HLE/admission, owning snapshot/flush, driver output submission. Final real x86 GetPixel phase recorded separately. Shader cache policy recorded. Oracle FBO reads and parked sleep excluded. Fixed pixels/counts/normal exits gate correctness; no noisy timing threshold, GPU timestamp, physical Switch/FPS or arbitrary workload claim.")
output.write_text(json.dumps(report, indent=2) + "\n")
print("Warm GDI measurement: PASS (12 normal exits/108 frames; capacity readbacks 8->0, explicit final read retained; no timing threshold)")
