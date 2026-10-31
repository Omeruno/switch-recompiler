# G5 — shared CPU acceleration, measured reference and Switch diagnostics

2026-11-01. Incoming `12c678a2bc38af5c96857d8f59fafc07715e3c0a` passed
[CI41241023826, all six jobs](https://github.com/Omeruno/switch-recompiler/actions/runs/41241023826).
User priority is now common performance before further Doom95 import coverage.
This package accelerates the existing interpreter. Full x86→ARM64 block translation,
DirectDraw/D3D, general GPU GDI and physical FPS acceptance remain separate work.

## Actual CPU path

Previously every instruction used the complete generic execution context and
register/lazy-flag rollback snapshot. The same work was paid by simple register
moves/arithmetic and ordinary successful stack/data accesses. The new default
path admits complete unprefixed32-bit instructions with checked fetch length:

- Register ADD/OR/ADC/SBB/AND/SUB/XOR/CMP, EAX/immediate and register immediates,
  MOV/XCHG/TEST, INC/DEC, ordinary shifts/rotates and signed IMUL.
- Ordinary MOV/ALU/TEST memory operands wholly inside one mapped page, LEA and
  checked PUSH/POP, including stack-register aliases, ModRM/SIB/displacements
  and real default DS/SS bases. ESP offsets and linear stack addresses stay
  distinct; POP ESP retains its real override semantics.

Admission/fetch/address checks finish before changing CPU or memory. Data faults,
page-crossing data, unsupported widths/prefixes/opcodes and other operations go
through the reference path. Cross-page instruction bytes are still fetched by
PagedGuestMemory's real fetch rules. Existing ALU/shift/flag helpers are shared;
undefined-flag policy, lazy state, EIP/debug length, timestamp instruction counting,
opcode census and fault rollback remain the same. This does not enable page-right
checks missing in the existing memory model. It introduces no EXE-name selection,
block batching, altered scheduler quantum, new parallel guest execution or GPU call.

There is no decoded-code/native-code cache here. Current bytes and actual current
mapping are read on each step, so guest stores, raw HostPointer mutation and
unmap/remap take effect immediately. No code pointer escapes the synchronous step.
Policy and optional counters belong to each memory owner; fresh/nested/suspended
owners do not share configuration or counts. The full reference remains selectable.

## Reproducible measurement

benchmark_integer.py runs the authored graphics_workload.exe through the actual
PE loader/scheduler/interpreter/HLE and GDI in three modes of work: CPU loop,
3841 copies, and2048 strokes. Each workload has one excluded warmup plus seven
rotating fast/reference pairs, followed by two opcode/HLE census runs. Both arms
use the same GDI path and counters. Independent expected checksum/full pixel hash,
quanta, total instructions and the complete opcode/HLE census must match. Timings
never replace correctness and have no pass/fail speed threshold.

The schema1 report records exact SHA/dirty state, runner/PE hashes, source hashes,
host/machine, every sample, minima/maxima and median paired reference/fast ratios.
The first local register-only probe measured about1.27× CPU and1.08–1.09× GDI work.
The subsequent broader register+mapped path during the full suite measured about
1.21× CPU,1.10× copies and1.06× strokes; an individual CPU pair was slower. These
are host wall samples with noise, not Switch FPS or a universal application gain.
The final published-SHA CI artifact is authoritative for that runner's samples.

Counters show accepted/refused work: the public CPU run has200211 instructions,
180046 fast/20165 reference in the broader path. Instructions reaching the
reference are still executed correctly. More admitted operations do not imply a
proportional application speedup: interpreter dispatch, remaining instructions,
HLE, raster work and presentation retain costs.

## Switch operation and reports

Ordinary execution uses the bounded fast interpreter without instruction clocks,
allocations, code-cache storage or per-step I/O. Detailed raster/input/quantum
clocks and the bounded opcode/HLE census are now opt-in rather than always timing
raster operations. Existing basic output/device counters remain available. No
physical speedup is inferred from removing instrumentation.

For a later same-build comparison, use the actual SD directory:

1. `sdmc:/switch-recompiler/profile-execution.flag`, first byte `1`, enables CPU
   counters, at most1million opcode samples and1million HLE samples, and detailed
   existing renderer phases. Truncation flags remain explicit.
2. `sdmc:/switch-recompiler/cpu-reference.flag`, first byte `1`, selects the full
   reference for the whole guest address space, including callbacks. Absent/other
   contents selects the default fast path. No per-application profile is selected.
3. At exit/abort/load failure the worker emits
   `sdmc:/switch-recompiler-performance.txt`, with exact build/EXE, policy, tracking
   state, counts and optional census. Disabled/allocation-unavailable census is
   labeled, avoiding a fake populated report. Writes occur once after native
   continuations drain. The existing `switch-recompiler-renderer.txt` adds CPU
   counters and detailed-timing state for modern UI.

Compare the same workload/duration and instrumentation in alternating modes.
Counts include executed instructions in callbacks; HLE calls are separate.
Executor quantum wall time includes HLE/callbacks/synchronous waits/presentation.
Nested raster/GPU intervals must not be added or subtracted to invent exclusive
x86 time. Output changes/accepted frames are not physical scanout FPS. Reports are
best-effort diagnostics, not durable telemetry. No physical test is required to
continue independent work; hardware acceptance remains deferred.

## Correctness gates

The standalone214323-case differential probe checks exact registers, lazy and
materialized flags, EIP/debug length, opcode census, memory bytes and exception
status/address/direction. It covers register forms, immediate sizes, signed/unsigned
edges, ModRM/SIB/DS/SS/stack, prefixes, unsupported/illegal operations, missing
instruction tails, cross-page data faults/partial writes, raw code mutation,
unmap/remap,own-opcode overwrites,exact timestamp increments and owner policy/counter isolation. Native report formatting is checked.
Strict O2/Wall/Wextra/Werror/no-exceptions/no-RTTI and full CPU-module+probe
ASan/UBSan with leak scanning pass locally; there are no guest fibers in that probe.
The same actual ARM64 executable passes under QEMU, not physical Switch hardware.

The existing native x86 oracle now runs both paths for every case and compares
status/registers/all returned flags/memory/FP/SIMD state before its independent
native-CPU comparison. CI retains three seeds×50000 cases across integer/x87/SSE.
Local kernel cannot execute32-bit ELF natively. An exploratory QEMU-i386 oracle
has existing AAM/x87 differences against our native-x86 expectations (7 integer
and900 mixed discrepancies in the respective50000-case samples); both interpreter
paths agreed in those runs. That software-emulated oracle is not accepted as the
hardware CPU gate. Exact-SHA native CI must pass before publication is called ready.
Local Switch SDK remains unavailable; exact-SHA CI builds the real NRO.

Full host/MinGW/Mesa and existing session/audio/input/window/CPU/GPU suites remain
mandatory. CI publishes stage-cpu-integer-performance (public PE, native probe,
paired JSON/log and exact source/contract), stage-cpu-integer-arm64, and
stage-cpu-hardware-oracle. No private game binaries/assets/traces are published.

## Performance implementation queue

1. G5 closes bounded common interpreter fast execution, same-build comparison,
   reference fallback and usable console census/phase reports.
2. Collect actual Switch workload/overhead evidence when hardware tests resume;
   rank remaining CPU instructions, HLE/raster/copy/output work. Do not invent an
   exclusive split from nested wall intervals.
3. Implement a bounded block/IR frontend and differential block oracle; define
   exact instruction budgets, host-call/fault/branch exit and callback boundaries.
4. Implement session-owned executable code storage with Horizon/libnx JIT/W^X,
   instruction-cache publication, retirement and interpreter fallback. The old
   successful JIT42 probe is not a full translator and need not be repeated.
5. Emit actual ARM64 for useful x86 blocks with full flags/register/memory rules.
6. Handle guest code changes, raw host aliases, module unload/remap and safe cache
   invalidation; page generations alone cannot observe raw-pointer stores.
7. Integrate hot-block selection with existing scheduler/TEB/SEH/APC/callback and
   stop/abort draining. Parallel guest execution is another correctness project.
8. Extend measured GPU GDI operations and resident batching where device evidence
   justifies them, retaining ROP/readback/DIB/fallback/ownership. Keep default CPU
   GDI and existing explicit native experiment until activation gates pass.
9. Build real DirectDraw/D3D resources/commands/shaders for the selected games.
10. Repeat exact-version Switch acceptance, memory/input/audio and frame pacing.

D13's next MIDI discovery/Doom95 compatibility queue remains open but is no longer
above this performance direction. Preserve Luna, F7/F8 controls/view, L/ZL overrides,
current audio and the closed SD recovery diagnosis.
