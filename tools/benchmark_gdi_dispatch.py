#!/usr/bin/env python3
"""Validate same-PE CPU/GPU pixels/counts and record first-frame host phases."""
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
proc = subprocess.run([str(runner.resolve()), str(fixture.resolve())], text=True,
                      capture_output=True, timeout=180,
                      env=dict(os.environ, EGL_PLATFORM="surfaceless", LIBGL_ALWAYS_SOFTWARE="1"))
print(proc.stdout, end="")
if proc.returncode:
    print(proc.stderr, file=sys.stderr)
    raise SystemExit(proc.returncode)
assert "Ordinary x86 GDI dispatch: PASS (16 normal-exit sessions" in proc.stdout
assert "CASE pre-draw seed allocation refusal PASS" in proc.stdout
assert "CASE actual context destruction then ordinary CPU GDI recovery PASS" in proc.stdout
keys = ("guest_raster_us", "owning_submit_us", "driver_output_submit_us", "guest_read_rop_dib_us")
rows = []
for line in proc.stdout.splitlines():
    if not line.startswith("SAMPLE "):
        continue
    _, repeat, mode, *values = line.split()
    assert mode in ("cpu", "gpu") and len(values) == 6
    row = dict(zip(keys, map(float, values[:4])))
    assert all(math.isfinite(v) and v >= 0 for v in row.values())
    row.update(repeat=int(repeat), mode=mode, accepted=int(values[4]), refused=int(values[5]))
    assert (row["accepted"], row["refused"]) == ((69, 4) if mode == "gpu" else (0, 0))
    row["cycle_us"] = sum(row[k] for k in keys)
    rows.append(row)
assert len(rows) == 14 and {(r["repeat"], r["mode"]) for r in rows} == {(n, m) for n in range(7) for m in ("cpu", "gpu")}
root = Path(__file__).resolve().parent.parent
summary = {}
for mode in ("cpu", "gpu"):
    samples = [r for r in rows if r["mode"] == mode and r["repeat"] > 0]
    summary[mode] = {k: statistics.median(r[k] for r in samples) for k in (*keys, "cycle_us")}
    summary[mode].update(cycle_min_us=min(r["cycle_us"] for r in samples), cycle_max_us=max(r["cycle_us"] for r in samples))
report = dict(schema=1, status="PASS_HOST_X86_GL_DISPATCH_PENDING_HARDWARE",
              head_sha=subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
              working_tree_dirty=bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=root, text=True).strip()),
              utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
              host=dict(system=platform.system(), machine=platform.machine(), platform=platform.platform(),
                        mesa_shader_cache_disable=os.environ.get("MESA_SHADER_CACHE_DISABLE", "unset")),
              fixture_sha256=hashlib.sha256(fixture.read_bytes()).hexdigest(),
              runner_sha256=hashlib.sha256(runner.read_bytes()).hexdigest(),
              warmup_pairs=1, measured_pairs=6, samples=rows, summary=summary,
              scope="Same PE, real x86 interpreter/HLE and production owning mixed/GL renderer. Fresh session/targets/renderer each run: first-frame costs include seed import and lazy GL setup. CPU uses same output path. Alternating order after excluded initial pair. First 64 calls and their owning submission/output, then real guest reads/ROP/DIB; sum of these intervals, not complete application launch or arbitrary workloads. Oracle glReadPixels, lifecycle/budget cases, load and guest sleep outside measured intervals (fast-forward enabled). Mesa software GL, no GPU execution timestamp/swap/scanout/physical Switch/FPS claim. Timing has no pass threshold; exact pixels, counts, normal exits and fault cases gate correctness.")
output.write_text(json.dumps(report, indent=2) + "\n")
print("GDI dispatch measurement: PASS (six measured paired first-frame samples; exact pixels/counts; no timing threshold)")
