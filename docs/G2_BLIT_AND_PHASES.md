# G2: measured 32-bit SRCCOPY and optional executor wall phases

The user requests continuing independently while physical tests are deferred.
Incoming `9d4f876` passed six jobs in CI run 40141901580; its clean G1 report and
public EXE hash were independently verified. G1 identified CPU blits as one
measurable portion of public tile/stroke workloads, alongside other execution
cost. This package optimizes that concrete case without claiming GPU GDI or
real Paint/Minesweeper FPS. It also prepares future console attribution without
requiring the user to run a new test now.

## Production copy contract

`Gdi::BlitCore` now has a row-based path for SRCCOPY (ROP byte CC), 32-bit
source/destination, equal source/destination dimensions, no mirror, no color key
and a fully in-bounds source rectangle. Existing target mapping, clipping and
same-bits overlap snapshot run first. Negative pitch is honored by `Surface::Row`.
Each copied pixel still masks the unused high byte to zero, exactly as the
reference Get/Set sequence does; this is deliberately not raw memcpy.

The path removes per-pixel scaling division, format switches and general ROP
selection. Uncaptured rows use a simple masked copy loop. Captured destinations
still use `Surface::Set`, preserving owned window pixels, ownership transitions,
tile revisions and no-op behavior. Source clipping, window DC offsets and screen
change accounting retain existing semantics. Unsupported cases continue through
the unchanged general loop, including formats/palettes, scaling, mirrors, keys,
other ROPs and partial out-of-bounds source rectangles. Existing general-path
limitations, alias detection and extreme-coordinate validation are not expanded
by this optimization.

The default is enabled in session-owned GDI state. `EnableBlitFastPath(false)`
is a native diagnostic reference control, not a guest API, theme preference or
application-specific switch. It restores the original loop for comparison in
the same executable/build. Fresh guest sessions restore the default.

## Correctness and measured comparison

`test_blit_fast_path` runs 1,600 deterministic reference/fast scenarios. It
compares complete destination/source storage, guard bytes, unused high bytes,
ownership/layer pixels and revision metadata. Cases include clipping, source
bounds, differing formats, positive/negative pitch, translated coordinates,
scaling, mirror, transparent-key, XOR, overlap snapshots and repeated no-op
captured writes. Independent analytic/full-frame public PE checks from G1 still
run through the real interpreter/HLE. Existing actual-EGL composition and
workspace comparisons validate retained layers after real x86 GDI operations.
The same native differential oracle also runs as an optimized static ARM64
binary under QEMU in the existing ARM64 CI job; this is architecture coverage,
not physical Switch acceptance.

```
COMPOSITION_GL_TEST=1 sh tools/run_host_tests.sh
```

The full suite also invokes `tools/benchmark_blit.py` with the same freshly
linked `benchmark_guest` and unchanged public `graphics_workload.exe` as G1.
It compares reference/fast in baseline, raster-timed and quantum-phase-timed
modes for all three workloads. Each of 18 conditions has one excluded checked
warm-up and seven measured repetitions: 126 raw samples, 144 checked invocations.
Order rotates per repetition. All runs must match completion/checksum/full-screen
hash and quantum count, with exact expected raster and optional phase counts.
Faults/timeouts/incorrect results remove/prevent fresh success evidence.

`build-host/stage-g-blit.json` includes raw samples, median/min/max times,
baseline fast/reference ratio, phase/baseline overhead ratio, nested blit time,
source SHA/dirty state, host identity and exact runner/fixture hashes. CI publishes
it with the fixture as `stage-g-blit-comparison`. Timings are observations, never
a noisy CI speed threshold. Ratios below or above one alone do not prove a
universal speedup/regression. CPU recurrence and stroke workload are controls;
only one final blit is present in the stroke workload. No compositor/GPU/swap or
physical console timing is included in this experiment.

## Optional production report v8

Normal execution has the new phase counters disabled. The Switch adapter reads
`sdmc:/switch-recompiler/profile-execution.flag` once at new-UI session begin;
only a first byte `1` enables them. Missing/unreadable/empty flags disable them.
There is no new per-frame or per-instruction SD logging and no user test request.
The existing exit-only `sdmc:/switch-recompiler-renderer.txt` report adds:

* `Execution phase timing enabled`;
* `Executor quantum wall` samples/total/max;
* `Native service wall` samples/total/max;
* `Executor idle sleep wall` samples/total/max.

Each phase scope reads the real steady clock twice when enabled and zero times
when disabled. Storage is three fixed duration counters, reset per session.
Scopes wrap **outer** RunOne calls, native shell service and timer idle sleeps.
They do not span a parked HLE fiber's peer execution: the RunOne interval ends
when that fiber returns to the native scheduler. RunOne still includes its own
interpreter/HLE/callback execution, scheduling, synchronous waiting and nested
presentation; it is wall time, not exclusive x86 or CPU time. Raster, snapshot,
GL upload/draw and swap metrics can be nested in quantum or native-service time:
never sum these as disjoint stages or label their residual interpreter time.
Only the production outer loop is covered; continuation draining during executor
destruction is outside its quantum scopes. Setup/load, frontend polling outside
the three scopes and total process time are not measured by these fields.

Deterministic clock tests verify disabled/reset and phase accounting. The actual
PE benchmark checks one enabled quantum sample per outer RunOne and zero samples
when disabled, and records its measured overhead separately. No clock was added
to Interpreter::Step or around each Win32 call. Existing raster instrumentation
policy is unchanged. Legacy UI does not acquire these new report fields.

## Remaining work

Physical Paint/GPU/fallback/input/relaunch and measured Switch workload evidence
remain deferred. No renewed SD diagnosis and no acceptance inferred from host
timings. Local devkitA64/libnx remains unavailable; CI provides the exact-SHA NRO.
Next independent graphics block: a coherent GPU-GDI target/command/readback
contract and bounded real-EGL prototype against CPU pixels, before activating
GPU drawing. Guest-mapped DIB writes, ROP destination reads and fallback must
remain correct; duplicating all CPU/GPU raster work is not itself acceleration.
Interpreter optimization remains evidence-dependent. Pinball API compatibility,
installer/archive support and Luna styling remain separate tracked work.


Initial local seven-repeat comparison (Linux x86-64, O1 runtime) observed tile
run medians 19.79 ms reference /15.69 ms fast, with raster-timed nested blits
4.70 ms /0.63 ms. CPU control medians were 5.00 ms /4.98 ms. These are bounded
host synthetic observations; use exact-SHA CI raw samples for reproduction,
not these rounded figures as a Switch FPS guarantee.
