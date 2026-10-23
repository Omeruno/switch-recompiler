# Stage G0: bounded workload census

The first package measures **frequency**, before choosing an optimization.
It changes no instruction, Win32, scheduler, renderer or input semantics.
It is not a CPU-time profiler, a dynarec, or evidence of Switch speedup.

## Collection contract

`rt::WorkloadProfileScope` explicitly binds one caller-owned profile to the
native execution thread. Its cooperative guest fibers and nested interpreters
share the collector. It does not use fiber-stack timing scopes across yields.
Leaving the scope restores the previous binding; a null scope disables it.
Storage has 513 opcode counters and 4096 host-index counters, plus an aggregate
for dynamic MFC stubs or host IDs outside the fixed table. No guest pointers,
strings, code bytes or per-event history are retained. There is no allocation,
clock read or file write in the recording path. Disabled recording performs a
null-pointer check at each instruction/recognized host call.

Each instruction and host stream has an independent default budget of 1,000,000
events. Once exhausted, collection stops for that stream and marks truncation;
execution continues. Counters cannot wrap, even with UINT64_MAX budgets. Results
are a **prefix census**, not a random sample or whole-session total if truncated.
The histogram sum equals the recorded total for its stream. Reported storage is
collector storage only, not runtime/process/driver memory.

Opcode buckets strip ordinary prefixes and distinguish the primary and 0F maps.
The interpreter records one Step attempt, including HLT, UD2 and faults; this
is not retired-instruction count. A missing first/second opcode goes to the
fetch-failure bucket. A fault reading a later operand stays in its opcode bucket.
REP is one Step attempt regardless of elements processed. Operand widths, ModRM
suboperations, SSE prefixes and x87 suboperations are not separate buckets.
Host counters record recognized dispatcher entry **before** calling the handler,
including nested/cancelled calls; suspended/resumed frames are not new entries.
They do not measure host duration, self time, blocking, callback time or raster
cost. Unrecognized address probes do not count. Known host IDs are meaningful
only within the same build; reports resolve bounded DLL/name labels and ordinals.
Dynamic MFC is deliberately aggregated, not silently lost or assigned false names.

## Reproduce

Run `sh tools/run_host_tests.sh`. Its native `test_workload_profile` executes a
public byte fixture without external files, then repeats with collection enabled,
disabled and exhausted. It verifies arithmetic/flags/PC, REP memory results,
exception rollback, real lstrlenA/lstrlenW dispatcher returns and stack
cleanup, nested binding restoration, exact repeated histograms and capacity edges.
It prints a workload-v1 report and fails on a wrong result or census.

The fixture performs 1000 ADD/DEC/JNZ iterations, prefixed and 0F NOPs, HLT,
REP STOSD, UD2 and two opcode-fetch faults. Expected: 3009 instruction attempts,
32 recognized host calls, 1000 entries each in 83/49/75, 16 calls each to
lstrlenA/lstrlenW. This intentionally selected mix is a correctness and
repeatability baseline; it cannot establish Paint's hot instructions.

With MinGW available the ordinary host runner accepts:

```sh
./build-host/winrun --profile-workload /path/to/public-fixture.exe 2>workload.txt
```

The collector covers module startup, execution and normal teardown of that one
runner invocation. It prints once at ordinary return, including load failures;
a report alone is not a pass. Check the underlying exit code and fixture's PASS
marker. Fatal native termination can leave no report. The runner retains its
normal exit status. No command-line option is added to the Switch frontend yet;
its profiler stays disabled. Existing CI compiles the shared instrumentation for
Switch and runs the native regression as part of the host suite.

## Next decision

Next add a representative **public PE** workload with independently checked
output, phase boundaries and repeated enabled/disabled host wall measurements.
Record build/compiler, fixture identity, budgets, repetitions and dispersion;
separate startup/steady work and report instrumentation overhead. Select a small
interpreter fast path only from that evidence and preserve oracle/SEH/fallback.
Before a dynarec, separately prove block-cache invalidation, executable-memory
lifetime, icache and precise fault/flags fallback. Existing minimal JIT probe
success does not close these integration gates. Console profiling and speedup
remain unmeasured; deferred hardware tests remain in STAGE_E_PERFORMANCE.md.

Local runner smoke also used the repository's CRT-free public `dlltest.exe`
with `testdll.dll` (MinGW GCC 14, `tests/guest/build.sh` flags). Two independent
profiled invocations produced byte-identical reports: 1426 instruction attempts,
111 host entries, no truncation, 36,920 bytes of collector storage on this host.
The disabled invocation produced identical guest stdout and exit 0, with no
profile report. Missing-EXE invocation retained exit 3 and emitted zero counts.
These are startup/DLL/API fixture counts, not a representative Paint benchmark.
Rebuilt PE timestamps/toolchain changes can change binary identity and counts;
record the exact files/build when comparing runs. Reproduce from the generated
fixture directory with `../winrun --profile-workload dlltest.exe`.
