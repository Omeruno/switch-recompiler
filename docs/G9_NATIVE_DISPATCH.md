# G9 — guarded session cache and experimental common native step dispatch

Successor: [G10 budgeted batches](G10_NATIVE_BATCHES.md) implements guarded outer
execution and separate [Switch CPU validation](G10_SWITCH_CPU_TEST.md). Historical
one-step timing below remains valid for G9,not a measurement of G10.

2026-11-01. Incoming verified runtime
`b110fccd3304dafee82f447ef5cd5b3e1a6f02c8` passed
[CI41344237545,all six jobs](https://github.com/Omeruno/switch-recompiler/actions/runs/41344237545).
[G6 IR](G6_CPU_BLOCK_IR.md),[G7 storage](G7_EXECUTABLE_STORAGE.md) and
[G8 emitter](G8_ARM64_EMITTER.md) now feed the common Interpreter::Step path.

Actual emitted ARM64 executes in a full ARM64 runtime through PE/DLL startup,
nested USER callbacks,APCs and guest threads. This is experimental **one instruction
per Step**,not production batching or a default performance win. G5 stays default.
The measured local QEMU prototype is slower than G5;next work is exact-budget
batch integration and overhead measurements,not default activation.

## Scope and policy

`NativeCache` belongs to GuestSession,between CodeStore and Process in member
lifetime. It binds a private host callback to the owning PagedGuestMemory;no global
current-cache pointer,CPU/TEB pointer or borrowed guest mapping is retained. Memory
and storage must outlive an explicitly created cache. Nested/fresh sessions bind
independently. Close detaches first,retires owned tokens and releases metadata;
GuestSession closes cache before CodeStore/guest backing,after continuations drain.

The common Interpreter::Step tries this binding before ordinary accounting. A
successful native entry executes exactly one guest instruction and supplies the
same debug length/raw opcode/Unknown mnemonic as the reference fast interface.
Failure/refusal executes the existing interpreter at unchanged EIP. No Step/HLE
instruction is counted twice. Native compilation/syscalls preserve host errno,
which belongs to the current continuation/CRT context. Reference fault/rollback/SEH dispatch remains on the
existing path;G8's compiled register-only subset adds no guest data access or fault
translation. Standalone memory has no binding;the CPU oracle/isolated CPU modules
need no link dependency on the native cache or emitter.

Existing main,nested DLL/WndProc/APC/SEH/CRT loops still call Step. Their HLE call
order,Checkpoint/TEB context,256-checkpoint continuation policy and exit/abort
cleanup are unchanged. G9 does not replace one quantum with many instructions or
run guest code past a checkpoint. Scheduler/loader/callback integration uses the
existing per-instruction boundary;true batching remains next work.

On Switch `sdmc:/switch-recompiler/cpu-native.flag`,first byte `1`,requests the
experimental cache. Absent/other content keeps G5. `cpu-reference.flag` wins for
the whole address space,including nested callbacks. No executable-name selection
or per-game fast path is added. On non-AArch64,requesting native returns ordinary
interpreter work without allocating executable code. Architecture availability is
not proof that a particular libnx JIT permission/mode can allocate successfully.

## Cache,source validity and bounded failure

Metadata is allocated lazily with nothrow only for an armed AArch64 request.
There are16 slots;admission requires8 visits at the same entry. A compiled block
can serve any actual node boundary inside its owned source,so a register loop
does not require a new image per instruction. Hot publications are preferred over
cold/negative admission metadata;when every slot is compiled,the oldest image is
retired. CodeStore's real per-session/global backing limits still apply.

Every native use goes through G8 source/owner/entry/lease validation. No page-
generation shortcut replaces current bytes. Guest writes,raw HostPointer aliases,
unmap/remap and DLL unload cannot authorize stale native instructions. Same-owner
identical remapping remains valid for this register-only snapshot. Changes to a
later unexecuted byte also invalidate the entire image before its first node runs.
No guest callback or yield occurs during lookup/compile/native step.

Unsupported/unavailable instruction admission is negatively cached only with a
16-byte snapshot **and mapping mask**. This covers the complete supported decoder
instruction span,including missing tails and32-bit wrap boundaries. Changed bytes
or newly mapped tails reset admission;negative results do not hide valid new code.
Guest fault state is not consumed by preview. Actual reference Step still raises
its genuine fault as before.

Allocation/publication/quota/closed-store failure disables native attempts for that
cache session and retires its publications;later steps use the interpreter. It
does not repeatedly request a failing JIT mapping. Sticky libnx release failures
remain accounted in G7's bounded process vault. No error is turned into a fake
success or missing-code advance. All mutation/binding APIs are trusted host-only;
one native owner with cooperative guest threads,not parallel native execution.

## Diagnostics

Existing optional integer counters retain fast/reference/IR/native separation.
The report states the effective engine,requested native policy,ARM64 architecture
availability,persistent cache failure state and requests/hits/misses/compiles/
invalidations/evictions/negative/fallback/failure counts. Cache work counters are
always collected only when experimental dispatch is invoked;instruction census
and detailed clocks still obey the existing profile switch. Slot evictions include
warm/negative metadata as well as native publications. No per-step clocks/I/O.
Reference priority and unavailable/disabled fallback remain explicit.

## Public executable acceptance

`test_native_cache.cpp` compares1038 real Interpreter::Step with full reference,
including raw lazy flags,EIP,debug bytes,census,fault status and exact per-step
instruction totals. Gates include source mutation beyond the admission probe,
raw aliases,unmap/remap,negative cache changes,interior reuse,more hot sites than
slots,quota recycling,reference priority and one-shot backend failure/cleanup,including errno preservation.
It executes actual ARM64 cache entries under QEMU;on x86 it explicitly verifies
architecture-unavailable fallback and zero native allocations.

`native_dispatch.exe` and `native_dispatch.dll` are authored CRT-free PE32 fixtures.
A register/ADC kernel has independently computed32-bit outputs. It runs inside
DllMain,WndProc,APC,worker thread and the ordinary main path. The inspector asserts
positive native deltas inside real LoadLibrary/SendMessage/SleepEx callbacks and
worker steps,correct main/worker thread identities,worker LastError preservation,
DLL outputs and independently read actual file bytes. The public fixture uses
shared supported wide USER APIs;it does not add missing legacy ANSI/window APIs.
The window retires through ordinary process/session cleanup.

Six normal new/legacy sessions compare full reference,G5 and requested-native:
complete opcode/HLE histograms,instruction counts and scheduler RunOne quanta must
match. Four additional cases genuinely park inside main SleepEx or nested
SendMessage→WndProc→SleepEx,then unwind their live C++ frames on host abort. Source,
CPU-context and code-storage ownership drain cleanly. Host-only fallback is not
reported as native PE acceptance. Existing broader SEH/lifecycle/GDI/audio/CPU
regressions and150000-case native-x86 oracle remain required.

`run_arm64_native_dispatch.py` builds **all runtime objects** for ARM64 from source,
builds those two PE fixtures with MinGW and executes the full inspector under
QEMU. It never links x86 host/stale objects. CI keeps six jobs;the ARM64 job now
also installs the public fixture compiler and runs this full-runtime gate.

## Paired measurement and current result

One excluded warmup pair +seven rotating G5/native pairs run the same public EXE,
loader,callbacks,thread and output checks. Each sample validates CPU/HLE/quantum
parity and independent arithmetic/file outputs before recording time. End-to-end
wall includes load/init/window/thread/file work,callback startup and complete
session/native-code retirement;two acknowledged
guest markers delimit timed register loops with real scheduler/dispatch work.
EXE Kernel has already been visited by callbacks/worker before those loop markers;
these are not claims of a purely cold compilation benchmark or exclusive CPU time.
The report binds exact SHA/dirty status,host/runner/version,binary/EXE/DLL hashes,
all relevant source hashes,raw samples,counts and paired ratios. There is no speed
threshold;slower native results are retained.

Final local full ARM64/QEMU measurement of the single-step path produced median
G5/native wall ratios about0.36 end-to-end and0.27 in the reused loop:the prototype
was roughly2.8×/3.7× slower on that host/QEMU run. One sample executed108051 x86
instructions,107436 as native,with2 compilations and exact quantum parity. Final
published-SHA CI artifacts are authoritative for their own samples. This is not a
Switch result or a claim that all native lowering must be slower. Per-instruction
lookup/snapshot validation/G8 wrapper/native entry/flag-query costs remain;batch
integration is the next measured engineering target. Do not activate by default
or claim a Paint/game speedup based on admitted instruction coverage.

CI artifacts `stage-cpu-native-dispatch` and `stage-cpu-native-dispatch-arm64`
include source/fixture/log/contract identity and the latter's actual ARM64 runtime
and paired JSON/raw log. No proprietary game binary or user trace is used.

Local devkitA64/libnx SDK remains unavailable;CI builds the real Switch NRO.
Physical native-cache/libnx-mode/FPS and existing Paint/GPU/fallback/input/relaunch
acceptance remain deferred. Preserve CPU GDI default,Luna/F7/F8/L/ZL/audio and
closed SD recovery. No physical test request or repeated JIT42 is required.
