#!/usr/bin/env python3
"""Same-build GL reference/batched work counts, exact pixels and host wall phases."""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import platform
import statistics
import subprocess
import os

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("runner", type=Path)
p.add_argument("output", type=Path)
p.add_argument("--repeats", type=int, default=7, choices=range(3, 16))
a = p.parse_args()
a.output.unlink(missing_ok=True)
runner = a.runner.resolve()
env = dict(os.environ, EGL_PLATFORM="surfaceless", LIBGL_ALWAYS_SOFTWARE="1")
hashes = {"tiles": "1e8dedf5a4b92b25", "mixed": "0a2696a11ccb0725", "overlap": None}


def run(mode, work):
    proc = subprocess.run([str(runner), mode, work], env=env, text=True, capture_output=True, timeout=40)
    if proc.returncode:
        raise RuntimeError(f"{mode}/{work}: exit {proc.returncode}\n{proc.stderr}")
    r = json.loads(proc.stdout)
    assert (r["status"], r["mode"], r["workload"], r["frames"]) == ("PASS", mode, work, 32)
    assert r["commands"] == 2048 and r["readbacks"] == 32
    assert r["uploads"] == (32 if work == "mixed" else 0)
    assert r["draws"] == (2048 if mode == "reference" or work == "overlap" else 96 if work == "mixed" else 32)
    assert r["batches"] == (0 if mode == "reference" or work == "overlap" else 64 if work == "mixed" else 32)
    assert r["batched_commands"] == (0 if mode == "reference" or work == "overlap" else 2016 if work == "mixed" else 2048)
    if hashes[work] is not None:
        assert r["hash"] == hashes[work]
    assert all(r[k] >= 0 for k in ("enqueue_ns", "submit_ns", "readback_ns"))
    r["cycle_ns"] = r["enqueue_ns"] + r["submit_ns"] + r["readback_ns"]
    return r


samples, summary = [], {}
for work in hashes:
    run("reference", work)
    run("batched", work)  # Excluded full-process warm-ups; each process also warms four checked cycles.
    rows = []
    for repeat in range(a.repeats):
        for mode in (("reference", "batched") if repeat % 2 == 0 else ("batched", "reference")):
            r = run(mode, work)
            r["repeat"] = repeat
            rows.append(r)
            samples.append(r)
    assert len({r["hash"] for r in rows}) == 1
    s = summary[work] = {}
    for mode in ("reference", "batched"):
        group = [r for r in rows if r["mode"] == mode]
        s[mode] = {k: statistics.median(r[k] for r in group) for k in ("enqueue_ns", "submit_ns", "readback_ns", "cycle_ns")}
        s[mode]["cycle_min_ns"] = min(r["cycle_ns"] for r in group)
        s[mode]["cycle_max_ns"] = max(r["cycle_ns"] for r in group)
        s[mode]["draws"] = group[0]["draws"]
    s["cycle_batched_over_reference"] = s["batched"]["cycle_ns"] / s["reference"]["cycle_ns"]

root = Path(__file__).resolve().parent.parent
report = {
    "schema": 1, "status": "PASS_HOST_GL_BATCH_COMPARISON_PENDING_HARDWARE",
    "head_sha": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
    "working_tree_dirty": bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=root, text=True).strip()),
    "utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "host": {"system": platform.system(), "machine": platform.machine(), "platform": platform.platform()},
    "runner_sha256": hashlib.sha256(runner.read_bytes()).hexdigest(),
    "repeats": a.repeats, "samples": samples, "summary": summary,
    "scope": "Actual EGL/Mesa software GL. Host wall for native enqueue/GL submit/explicit synchronous read barrier; each process excludes four warm-up cycles. No GPU execution duration, FPS, guest x86 or Switch claim. Fixed work counts/pixels gate correctness; no noisy timing pass threshold.",
}
a.output.write_text(json.dumps(report, indent=2) + "\n")
print("GPU GDI batching: PASS (exact pixels/order/barriers, tiles 2048->32 draws, mixed 2048->96, overlap 2048->2048)")
