# G8 — actual bounded ARM64 emission from x86 IR

Successor: [G11 native LEA](G11_NATIVE_ADDRESS.md) extends pure32-bit address
arithmetic;historical counts/scope below belong to this milestone. G5 remains
default and G10 actual Switch CPU acceptance is pending.


2026-11-01. Incoming verified runtime
`b4e160ab39f29bee07bcf9c452dbc6448c1042e8` passed
[CI41253673567,all six jobs](https://github.com/Omeruno/switch-recompiler/actions/runs/41253673567).
Builds on [G6 owned IR](G6_CPU_BLOCK_IR.md) and
[G7 executable storage](G7_EXECUTABLE_STORAGE.md).

This package translates the supported G6 x86 operations into real AArch64 machine
instructions and executes them under ARM64/QEMU. It is a software correctness
milestone,not production dynarec activation or a Switch FPS result. Ordinary
application execution remains G5. Next is guarded dispatch/cache and exact
scheduler/callback integration with public executable measurements.

## Generated operations and ABI

`source/cpu/arm64_emitter.h/.cpp` emits MOV/XCHG/NOP,all eight32-bit register ALU
kinds,INC/DEC,TEST,direct JMP and all16 short/near Jcc from immutable G6 nodes.
Native arithmetic/register loads/stores and branch selection implement those
operations;the native entry does not call Interpreter::Step or replay portable IR.
No guest data memory,stack/call/RET,prefix,shift/IMUL,x87/SSE or HLE operation is
added to the G6 subset. Those boundaries stay at the current guest EIP for the
reference interpreter,including genuine guest faults/SEH.

The private ABI is `void(CpuState*,uint32_t firstNode,uint32_t count)`. Native code
preserves x19/x20,x29/LR and16-byte stack alignment across calls. Other callee-saved
registers are untouched. Dispatch accepts a checked node boundary and positive
bounded prefix;native branches always exit and cannot loop indefinitely inside
a call. All32 guest bits wrap naturally in W-register arithmetic. Guest EIP is
stored explicitly,including32-bit target/address wrap.

The image is bounded to4096 words/16KiB,below G7's64KiB image limit. Writer capacity
and branch displacements are checked;empty emission is refused. Images have no
public writable access and publish through CodeStore only. They contain trusted
process-local helper addresses and CpuState field offsets;they are **not** a
serialized or cross-process cache. CpuState must remain standard layout.

## Exact flag representation

Raw lazy state is preserved,not just a materialized EFLAGS checksum. Generated
stores implement lazyOp/width/A/B/result/carry;logic zeroes A/B,ADC/SBB retain
incoming carry,INC/DEC preserve CF and apply the existing eflags mask. Fields
that the reference leaves alone,including a previous lazyCarryIn,stay untouched.
All non-arithmetic architectural state and x87/SSE/segment fields are preserved.

Two small pure C++ helpers read incoming CF or materialized flags from CpuState.
They do not change state,run guest code,read guest data memory,yield or interpret
an IR node. Flag-dependent ADC/SBB/INC/DEC/Jcc may call them;the actual arithmetic,
raw tuple updates and Jcc selection remain emitted ARM64. This first implementation
is not fully inlined flag lowering/register allocation and makes no speed claim.
ARM NZCV alone cannot represent x86 CF/PF/AF or the existing lazy tuple.

## Validation and execution contract

`Translation` owns its source snapshot and opaque G7 token. Compile checks current
bytes/owner,refuses empty IR or storage failure,and clears its output on failure.
It does not retire earlier publications implicitly;the store owns them until
explicit token retirement/Close. No host executable pointer escapes a lease.

Execute checks budget,translation,source owner/current bytes,guest entry and live
code lease. Non-AArch64 hosts explicitly return ArchitectureUnavailable without
calling ARM64 bytes. A retired/closed/foreign store returns Unavailable;no success
or fallback instruction is fabricated. The caller must inspect status and use the
current EIP/executed prefix if choosing reference fallback. Status Complete can
still mean a semantic exit such as CodeChanged,WrongOwner or Checkpoint.

Without checkpoints,the known prefix is one synchronous native call. Its supported
operations and flag queries do not yield or write guest memory;current source
validation therefore covers that call under the sole native-owner contract.
With checkpoints,the hook runs before every instruction and source/EIP/lease are
checked again afterwards. The wrapper copies source and pins code before any
hook,so caller destruction of Translation or CodeStore during a parked hook
cannot leave a borrowed source/entry. Store retirement prevents later entry;
guest memory itself must stay alive. Parallel native guest execution is unsupported.

Successful native prefixes use separate per-memory `IntegerStats().native` work
counts,shared timestamp/debug-EIP/opcode accounting and the existing bounded
profile. `CPU executed x86 steps` includes fast/reference/IR/native,and the report
adds `CPU native ARM64 steps`. Ordinary engine selection still reports G5. Native
accounting occurs after each synchronous emitted prefix;there are no guest
observations or HLE calls within it. Hooks observe accounting at each boundary.
Precise internal native fault mapping/unwind and production asynchronous-stop
integration are not implemented by this register-only test executor.

## Reproducible public evidence

`tests/test_arm64_emitter.cpp` executes52437 three-way native/reference/IR
comparisons and198371 translated nodes under ARM64/QEMU,including26013 checkpoint
comparisons. It covers all register selections/aliases,edge values,all lazy kinds/
widths,all64 materialized arithmetic-flag combinations for each Jcc,mixed data/
carry dependencies,all budget prefixes,interior-node resume and maximum32-node/
192-byte blocks. Raw flags and complete remaining architectural state,opcodes and
instruction counts match independently executed reference instructions.

Additional gates cover explicit callee-saved GPR/SP ABI checks across flag helpers,
real HostFiber suspension in checkpoints,destruction of source/store,mutation/
retirement between instructions,wrong owners/entries/stores,quota/refusal,no stale
native entry,host/fetch/unsupported boundaries,top-of-address wrap,real reference
access-violation rollback and exact RDTSC delta/debug EIP. Three parked checkpoint
cases prove actual resume and cleanup,not merely a checkpoint pointing at a yield.
The earlier G7 probe separately retains a generated native return frame while
parked inside C++;G8 checkpoints are outside the generated native call.

On x86_64 the same inspector validates1483 emitted/published layouts and guard/
refusal/fallback contracts,but explicitly reports native execution unavailable.
Host strict and ASan/UBSan with leak detection cover emission/wrapper/storage;
they do not execute ARM64. Native arithmetic execution evidence comes from the
actual ARM64/QEMU binary. Full host/MinGW/Mesa regressions,G5/G6/G7 gates and
3×50000 real native-x86 hardware oracle remain required.

CI publishes `stage-cpu-arm64-emitter` and `stage-cpu-arm64-emitter-arm64` with
logs,exact emitter/IR/storage/CPU sources and this contract. No private game asset,
EXE,DLL,trace or screenshot is needed or published.

## Remaining activation work

No production dispatch,hot-block selection or code-cache lookup is enabled.
Current-byte/owner validation must cover guest writes,raw host aliases,DLL unload/
remap and invalidation before use;page generations alone are insufficient.
Production integration must preserve exact quantum charges,TEB/SEH/APC/callback/
stop and continuation-drain contracts,then prove real PE behavior and paired
reference/native measurements before activation. Memory/stack/SIMD lowering,
flag inlining/register allocation,full x86 dynarec and real GPU game APIs remain
separate work. Preserve CPU GDI default,Luna/F7/F8/L/ZL,audio and fresh sessions.

Local Switch SDK remains unavailable after the prior devkitPro HTTP403;CI compiles
the actual Switch adapter/NRO. Compilation,QEMU and host checks are software
acceptance only. Physical Switch native-block execution,FPS,Paint/GPU/fallback/
input/relaunch checks remain deferred;do not repeat SD diagnosis or JIT42.

## G8 CI fixture follow-up (2026-11-01)

First G8 SHA `6a49055e6c6337de66f11954c17777ae7153b806` passed five CI jobs;
[CI41342882076](https://github.com/Omeruno/switch-recompiler/actions/runs/41342882076)
failed the host D6 compound DLL/string assertion at guest source line60. It is
not accepted as six-job success. Locally,the same failure is reproducible by
placing the resource-only non-relocatable DLL at0x70000000/0x70010000,occupied by
HLE modules. MinGW's path-hashed default image base varies with the fresh fixture
output directory;the original failed DLL was not retained,so its exact base is
not asserted here. Public class/resource DLL builds now pin noncolliding bases
0x62000000/0x63000000. Deliberate relocation/collision fixtures are unchanged.
The D6 inspector records separate load/string errors and flushes loader diagnostics.
Production loading/failure/relocation and resource assertions are unchanged.
The revised package must pass fresh full host/MinGW/Mesa and exact-SHA six-job CI.

Successor [G9](G9_NATIVE_DISPATCH.md) now integrates a guarded experimental cache
through common Interpreter::Step and actually executes full ARM64 PE/DLL/callback/
worker sessions with exact one-step quantum accounting and paired measurements.
G5 stays default;native batching remains next. Revised G8 b110fcc passed
[CI41344237545,all six jobs](https://github.com/Omeruno/switch-recompiler/actions/runs/41344237545).
