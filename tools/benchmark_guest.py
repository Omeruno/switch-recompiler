#!/usr/bin/env python3
"""Repeat public PE work; check pixels/results before publishing host timings."""
import argparse
import datetime
import hashlib
import json
import pathlib
import platform
import statistics
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("runner", type=pathlib.Path)
parser.add_argument("fixture", type=pathlib.Path)
parser.add_argument("output", type=pathlib.Path)
parser.add_argument("--repeats", type=int, default=5, choices=range(3, 16))
args = parser.parse_args()
args.output.unlink(missing_ok=True)  # A failed run must not leave stale success.
runner, fixture = args.runner.resolve(), args.fixture.resolve()
modes = ["baseline", "census", "raster", "both"]
expected = {
    "cpu": (0x7a218cb8, "769ca21d883d8625", 0, 0),
    "tiles": (0x204080, "8e38b3b5c7422625", 3841, 0),
    "strokes": (0x208040, "91dd4ca5ebbb7625", 1, 2048),
}


def run(workload, mode):
    proc = subprocess.run([str(runner), str(fixture), workload, mode],
                          capture_output=True, text=True, timeout=40, check=True)
    r = json.loads(proc.stdout)
    checksum, pixels, blits, strokes = expected[workload]
    assert (r["schema"], r["status"], r["workload"], r["instrument"]) == (1, "PASS", workload, mode)
    assert r["checksum"] == checksum and r["pixel_hash"] == pixels
    census, raster = mode in ("census", "both"), mode in ("raster", "both")
    assert bool(r["instructions"]) == census and bool(r["hosts"]) == census
    assert sum(x["count"] for x in r["opcodes"]) == r["instructions"]
    assert sum(x["count"] for x in r["host_entries"]) == r["hosts"]
    stages = {x["stage"]: x for x in r["raster"]}
    assert len(stages) == 8
    assert stages["blit"]["count"] == (blits if raster else 0)
    assert stages["stroke"]["count"] == (strokes if raster else 0)
    assert stages["readback"]["count"] == (2 if raster and workload != "cpu" else 0)
    assert not any(stages[s]["count"] for s in ("pixel", "shape", "fill", "flood", "text"))
    assert all(x["max_ns"] <= x["ns"] <= r["run_ns"] for x in r["raster"])
    return r


samples, summary = [], {}
for workload in expected:
    for mode in modes:
        run(workload, mode)  # Excluded warm-up, with the same correctness checks.
    for repeat in range(args.repeats):
        for mode in modes[repeat % 4:] + modes[:repeat % 4]:
            r = run(workload, mode)
            r["repeat"] = repeat
            samples.append(r)
    subset = [r for r in samples if r["workload"] == workload]
    assert len({r["quanta"] for r in subset}) == 1
    counted = [r for r in subset if r["instrument"] in ("census", "both")]
    assert len({json.dumps([r["opcodes"], r["host_entries"]], sort_keys=True) for r in counted}) == 1
    summary[workload] = {}
    for mode in modes:
        rows = [r for r in subset if r["instrument"] == mode]
        medians = {key: statistics.median(r[key] for r in rows) for key in
                   ("create_ns", "load_ns", "prepare_ns", "run_ns", "teardown_ns")}
        medians["run_min_ns"] = min(r["run_ns"] for r in rows)
        medians["run_max_ns"] = max(r["run_ns"] for r in rows)
        medians["run_ratio_to_baseline"] = medians["run_ns"] / (
            summary[workload]["baseline"]["run_ns"] if mode != "baseline" else medians["run_ns"])
        medians["raster_nested_ns"] = statistics.median(sum(s["ns"] for s in r["raster"]) for r in rows)
        summary[workload][mode] = medians

root = pathlib.Path(__file__).resolve().parent.parent
head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip()
dirty = bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=root, text=True).strip())
report = {
    "schema": 1, "status": "PASS_HOST_MEASUREMENT_ONLY", "head_sha": head,
    "working_tree_dirty": dirty, "utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "host": {"system": platform.system(), "machine": platform.machine(), "platform": platform.platform()},
    "runner_sha256": hashlib.sha256(runner.read_bytes()).hexdigest(),
    "fixture_sha256": hashlib.sha256(fixture.read_bytes()).hexdigest(),
    "fixture_size": fixture.stat().st_size, "repeats": args.repeats,
    "scope": "Host wall phases. Run includes module initialization, interpreter, HLE and scheduler; raster is nested within run. No GPU/presentation, Switch FPS or real Paint/Minesweeper timing.",
    "summary": summary, "samples": samples,
}
args.output.write_text(json.dumps(report, indent=2) + "\n")
print("G1 public PE phases/overhead: PASS (3 workloads, 4 modes, checked pixels/counts)")
