#!/usr/bin/env python3
"""Compare native workspace snapshots for tiled, partial and CPU-DIB workloads."""
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

runner, tiles, partial, copy, coherence, output = map(Path, sys.argv[1:])
output.unlink(missing_ok=True)
proc = subprocess.run([str(p.resolve()) for p in (runner, tiles, partial, copy, coherence)], text=True,
                      capture_output=True, timeout=180,
                      env=dict(os.environ, EGL_PLATFORM="surfaceless", LIBGL_ALWAYS_SOFTWARE="1"))
print(proc.stdout, end="")
if proc.returncode:
    print(proc.stderr, file=sys.stderr)
    raise SystemExit(proc.returncode)
assert "Retained workspace: PASS" in proc.stdout and "Retained transitions PASS" in proc.stdout
rows = []
for line in proc.stdout.splitlines():
    if not line.startswith("FRAME "):
        continue
    _, sample, mode, workload, frame, raster, refresh, render, snapshots, copied, reads, commits, mask_bytes = line.split()
    sample, mode, workload, frame, snapshots, copied = map(int, (sample, mode, workload, frame, snapshots, copied))
    assert 0 <= sample < 4 and mode in (0, 1, 2) and 0 <= workload < 3 and 0 <= frame < 9
    partial_reads=(0,2,3,4,6,7,8,10,11)
    partial_commits=(0,1,1,2,3,3,4,5,5)
    partial_snapshots=(1,2,2,3,4,4,5,6,6)
    assert snapshots == (((1 if mode==2 else partial_snapshots[frame]) if workload==1 else 1) if mode and workload!=2 else frame+1)
    assert int(reads)==(1+2*frame if workload==2 else (frame if mode==2 else partial_reads[frame]) if workload==1 else 0)
    assert int(commits)==(frame+1 if workload==2 else (0 if mode==2 else partial_commits[frame]) if workload==1 else 0)
    assert int(mask_bytes)==(2048 if mode==2 and workload==1 and frame>0 else 0)
    assert copied == snapshots * 11073920
    row = dict(sample=sample, mode=("reference", "reuse", "exact_damage")[mode], workload=("tiles", "partial", "copy")[workload],
               frame=frame, raster_us=float(raster), refresh_us=float(refresh), output_us=float(render),
               snapshots=snapshots, snapshot_bytes=copied, target_reads=int(reads), cpu_commits=int(commits), damage_mask_bytes=int(mask_bytes))
    assert all(math.isfinite(row[k]) and row[k] >= 0 for k in ("raster_us", "refresh_us", "output_us"))
    assert row["refresh_us"] >= row["output_us"]
    row["prepare_us"] = row["refresh_us"] - row["output_us"]
    row["cycle_us"] = row["raster_us"] + row["refresh_us"]
    rows.append(row)
assert len(rows)==324 and len({(r["sample"],r["mode"],r["workload"],r["frame"]) for r in rows})==324
summary = {}
for workload in ("tiles", "partial", "copy"):
    summary[workload] = {}
    for mode in ("reference", "reuse", "exact_damage"):
        summary[workload][mode] = {}
        for phase in ("cold", "warm"):
            group = [r for r in rows if r["workload"]==workload and r["mode"]==mode and r["sample"]>0 and ((r["frame"]==0)==(phase=="cold"))]
            summary[workload][mode][phase] = {k:statistics.median(r[k] for r in group) for k in ("raster_us", "prepare_us", "output_us", "refresh_us", "cycle_us")}
            summary[workload][mode][phase].update(samples=len(group), cycle_min_us=min(r["cycle_us"] for r in group), cycle_max_us=max(r["cycle_us"] for r in group))
root = Path(__file__).resolve().parent.parent
report = dict(schema=2, status="PASS_HOST_RETAINED_WORKSPACE_PENDING_HARDWARE",
              head_sha=subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
              working_tree_dirty=bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=root, text=True).strip()),
              utc=datetime.datetime.now(datetime.timezone.utc).isoformat(), host=dict(platform=platform.platform(), machine=platform.machine(), mesa_shader_cache_disable=os.environ.get("MESA_SHADER_CACHE_DISABLE", "unset")),
              fixture_sha256={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (tiles,partial,copy,coherence)},
              samples=rows, summary=summary,
              scope="36 normal-exit x86 sessions, 324 production EGL/full independent-pixel frames with actual native shell overlay; one additional parked two-window transition session is inspected/drained, not a normal exit. Same build, rotated historical/reference-reuse/exact-damage order; sample0 pairs excluded. Snapshot bytes include copied base/owner/native overlay pixels and initialized revision storage, not pure DMA or physical traffic. Refresh wall includes output; prepare=refresh-output, output includes pending target flush when reusing and is not GPU execution duration. Use total cycle for comparisons: GPU flush can move from snapshot into output. Driver lazy setup/cold and repeated warm phases separated. Oracle FBO reads/guest sleeps/load/teardown excluded. Historical diagonal partial writes need five mid-series CPU commits/six snapshots/eleven pre-guest reads. Exact damage uses no mid-series commit, one snapshot and eight journal-capacity reads; CPU DIB copies require real checkpoints/reimport and all nine snapshots. No noisy timing threshold, default broad activation, physical Switch/FPS claim.")
output.write_text(json.dumps(report, indent=2)+"\n")
print("Workspace reuse measurement: PASS (36 exits/324 frames, tile snapshots 9->1, partial 9->6->1; CPU DIB remains 9; no timing threshold)")
