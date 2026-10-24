#!/usr/bin/env python3
"""Compare general/fast GDI and opt-in outer quantum clocks in one exact build."""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import platform
import statistics
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("runner", type=Path)
parser.add_argument("fixture", type=Path)
parser.add_argument("output", type=Path)
parser.add_argument("--repeats", type=int, default=7, choices=range(3, 16))
args = parser.parse_args()
args.output.unlink(missing_ok=True)
runner, fixture = args.runner.resolve(), args.fixture.resolve()
conditions = [(mode, path) for mode in ("baseline", "raster", "phase") for path in ("reference", "fast")]
expected = {"cpu": (0x7a218cb8, "769ca21d883d8625"),
            "tiles": (0x204080, "8e38b3b5c7422625"),
            "strokes": (0x208040, "91dd4ca5ebbb7625")}


def run(workload, mode, path):
    p = subprocess.run([str(runner), str(fixture), workload, mode, path],
                       capture_output=True, text=True, timeout=40)
    if p.returncode:
        raise RuntimeError(f"{workload}/{mode}/{path}: exit {p.returncode}\n{p.stderr}")
    r = json.loads(p.stdout)
    assert (r["schema"], r["status"], r["workload"], r["instrument"], r["blit_path"]) == (1, "PASS", workload, mode, path)
    assert (r["checksum"], r["pixel_hash"]) == expected[workload]
    assert not r["instructions"] and not r["hosts"] and not r["opcodes"] and not r["host_entries"]
    assert r["quantum_count"] == (r["quanta"] if mode == "phase" else 0)
    assert 0 <= r["quantum_wall_ns"] <= r["run_ns"]
    if mode != "phase":
        assert r["quantum_wall_ns"] == 0
    stages = {s["stage"]: s for s in r["raster"]}
    counts = {"blit": {"cpu": 0, "tiles": 3841, "strokes": 1}[workload],
              "stroke": 2048 if workload == "strokes" else 0,
              "readback": 0 if workload == "cpu" else 2}
    assert len(stages) == 8
    for name, s in stages.items():
        assert s["count"] == (counts.get(name, 0) if mode == "raster" else 0)
        assert 0 <= s["max_ns"] <= s["ns"] <= r["run_ns"]
    return r


samples, summary = [], {}
for workload in expected:
    for mode, path in conditions:
        run(workload, mode, path)
    for repeat in range(args.repeats):
        for mode, path in conditions[repeat % 6:] + conditions[:repeat % 6]:
            r = run(workload, mode, path)
            r["repeat"] = repeat
            samples.append(r)
    subset = [r for r in samples if r["workload"] == workload]
    assert len({r["quanta"] for r in subset}) == 1
    summary[workload] = {}
    for mode, path in conditions:
        rows = [r for r in subset if (r["instrument"], r["blit_path"]) == (mode, path)]
        summary[workload][mode + "_" + path] = {
            "run_median_ns": statistics.median(r["run_ns"] for r in rows),
            "run_min_ns": min(r["run_ns"] for r in rows),
            "run_max_ns": max(r["run_ns"] for r in rows),
            "blit_median_ns": statistics.median(next(s["ns"] for s in r["raster"] if s["stage"] == "blit") for r in rows),
            "quantum_median_ns": statistics.median(r["quantum_wall_ns"] for r in rows),
        }
    s = summary[workload]
    s["baseline_fast_over_reference"] = s["baseline_fast"]["run_median_ns"] / s["baseline_reference"]["run_median_ns"]
    s["phase_fast_over_baseline_fast"] = s["phase_fast"]["run_median_ns"] / s["baseline_fast"]["run_median_ns"]

root = Path(__file__).resolve().parent.parent
head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip()
dirty = bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=root, text=True).strip())
report = {
    "schema": 1, "status": "PASS_HOST_COMPARISON_PENDING_HARDWARE",
    "head_sha": head, "working_tree_dirty": dirty,
    "utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "host": {"system": platform.system(), "machine": platform.machine(), "platform": platform.platform()},
    "runner_sha256": hashlib.sha256(runner.read_bytes()).hexdigest(),
    "fixture_sha256": hashlib.sha256(fixture.read_bytes()).hexdigest(), "fixture_size": fixture.stat().st_size,
    "repeats": args.repeats, "samples": samples, "summary": summary,
    "scope": "Same-binary general/fast CPU SRCCOPY, host PE run wall and nested GDI. No Switch FPS/GPU-GDI claim; no timing pass threshold. Phase clocks surround outer RunOne, not per-instruction or parked HLE calls.",
}
args.output.write_text(json.dumps(report, indent=2) + "\n")
print("G2 GDI reference/fast and quantum clocks: PASS (pixels/counts, 3 workloads, 6 conditions)")
