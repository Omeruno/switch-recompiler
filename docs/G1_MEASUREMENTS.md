# G1: public PE phases and instrumentation overhead

Follow-up: [G2](G2_BLIT_AND_PHASES.md) adds a measured CPU SRCCOPY path,
same-build reference/fast comparison and optional production executor phases.

This begins measured Stage G after the bounded software F baseline. It does
not implement a dynarec, GPU GDI or certify real Paint/Minesweeper performance.
The user's current Switch observation is slowdown with Paint and a large
Minesweeper board; no exact-SHA console timing log accompanied that observation.

## Existing graphics path and intended next boundary

The production Switch OpenGL backend already uploads retained window/native
planes, draws their composition and presents the result. Canonical GDI drawing,
ROP destination reads and GetPixel/DIB readback currently run on the CPU.
See [composition](STAGE_E_COMPOSITION.md), [metrics](STAGE_E_PERFORMANCE.md) and
[workspace integration](F4C_WORKSPACE_LAYERS.md). Uploading a CPU-drawn frame
does not make GDI rasterization GPU execution.

The next graphics investigation is bounded GPU execution of selected GDI
operations, beginning with measured blits/fills. It needs a coherent policy for
CPU destination reads, guest DIB access, ordering, clipping, palettes and ROPs;
pixel-exact CPU/actual-EGL oracles and software fallback remain required.
Sending every tiny draw as an immediate GPU command can introduce stalls, so
batching and readback costs must be measured. No direct-rendering speedup is
claimed by this package. Interpreter cost remains a separate potential bottleneck.

## Reproduction

Run the existing complete host suite with MinGW and Mesa:

```
COMPOSITION_GL_TEST=1 sh tools/run_host_tests.sh
```

It builds `graphics_workload.exe` from public C source and `benchmark_guest`
against the same fresh runtime objects used by every other host test. It invokes:

```
python3 tools/benchmark_guest.py build-host/benchmark_guest \
  build-host/guest-run.REPLACE/graphics_workload.exe build-host/stage-g-measurement.json
```

CI uploads the report, fixture and this contract as `stage-g-host-measurement`.
No Microsoft EXE, game assets or user-supplied binary enters public git.

## Work and correctness gates

The CRT-free x86 PE runs three separate deterministic workloads:

* `cpu`: 20,000 integer recurrence iterations, independently checked result
  `0x7a218cb8`.
* `tiles`: 30 by 16 cells, eight redraws, 3,840 SRCCOPY tile blits and one final
  screen blit. The complete 480 by 256 destination is checked analytically.
* `strokes`: 2,048 horizontal one-pixel strokes and one final screen blit.
  Every destination pixel is checked against independent row/color expectations.

The guest checks selected GetPixel results and exports completion/checksum.
The host checks real ExitProcess(0), completion, checksum, every destination
pixel and a pinned RGB hash of the whole 800 by 600 screen. CPU-only also checks
the initial-screen hash. Fault, idle, missing exports, allocation failure,
100,000-quantum exhaustion, observed 30-second deadline or census truncation
fails the measurement. The subprocess deadline is 40 seconds. Quantum and
wall-clock guards are host harness bounds, not interruptible HLE guarantees.
This fixture has no GUI pump, resource/font dependency, real Paint code, arbitrary
curves, alpha, ROP readback, moving windows or interactive performance claim.

## Experiment and accounting

For each workload the Python runner excludes one checked warm-up per mode and
collects five repetitions of `baseline`, `census`, `raster`, `both`. Mode order
rotates per repetition to reduce systematic order bias. It records every sample,
median/minimum/maximum run time and median ratio to baseline. Timing is not a
CI threshold: noisy or sub-1.0 ratios do not establish a speedup. Counted modes
must have identical opcode/host histograms and all modes identical quantum and
pixel results. Raster-enabled runs require exact expected stage counts; disabled
runs require zero. The fixed G0 census limits remain unchanged.

All timestamps are host steady-clock wall time, independent of accelerated
guest time:

| Field | Scope |
| --- | --- |
| create_ns | GuestSession construction, new-UI setup and command line |
| load_ns | LoadMainExe, including mapping/import preparation |
| prepare_ns | PrepareMainThread |
| run_ns | GuestExecution construction, module initialization, interpreter/HLE/scheduler execution, exit and executor destruction |
| teardown_ns | GuestSession destruction; excludes the prior pixel oracle |
| raster stage ns | Existing outermost synchronous GDI timings, **nested in run_ns** |

Export checks, full-screen validation/hashing and report serialization are outside
these measured phases. They are not a complete end-to-end wall total. A residual
after subtracting raster is not exclusively x86 time. Census measures attempted
instructions/calls, not their individual durations; numeric entry IDs are local
to the exact runner build. These timings contain no compositor, GL submission,
GPU execution, swap, physical latency or frames-per-second measurement.

The report includes source HEAD/dirty state, runner and fixture SHA256, fixture
length, host platform, UTC timestamp and all samples. A successful clean exact-SHA
CI artifact is the published result; local precommit evidence is marked dirty.
The output is removed before each experiment; any correctness/subprocess failure
prevents a fresh success report. The full suite removes old G1/F reports first.

## Initial local observation and next step

An initial five-repeat Linux x86-64 run of the precommit tree had median baseline
run times around 5 ms (cpu), 21 ms (tiles), 19 ms (strokes); raster-enabled tile
and stroke runs accounted for roughly 4–5 ms inside their run time. These are
host synthetic observations, not console FPS or a stable performance promise.
Reported census/raster ratios varied around baseline, including ratios below
one, so no precise instrumentation-overhead percentage or optimization win is
inferred. Use each exact CI report's raw measurements rather than these rounded
development observations for reproduction.

Next: production opt-in phase reporting for the same console workloads and a
measured blit/fill or interpreter candidate with before/after evidence. GPU GDI
requires its own coherent command/readback contract and actual EGL pixel oracle
before activation. Existing physical checks remain consolidated and deferred.
Luna visual fidelity remains open; native file tools do not complete XP styling.
Pinball x86 has a separately reproduced first D blocker, missing RegCreateKeyExA;
this package neither fixes that API nor claims Pinball compatibility.
