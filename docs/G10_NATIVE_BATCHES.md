# G10 — budgeted native execution

Successor: [G11 native LEA](G11_NATIVE_ADDRESS.md) extends pure32-bit address
arithmetic;historical counts/scope below belong to this milestone. G5 remains
default and G10 actual Switch CPU acceptance is pending.


Status: software implemented; **PENDING_USER_SWITCH_RUN** for physical CPU acceptance.
G5 remains the production default. This extends G9; it does not replace the
reference interpreter or enable native/GPU execution by default.

## Common execution contract

`Interpreter::Run` returns the actual instruction count and accepts an explicit
budget. Native batches execute only the G8 pure register/ALU/direct-branch prefix,
at most32 instructions, with G9 current-byte/owner/lease validation. Unsupported
instructions use the existing interpreter. Refusal does not admit the cache twice.
`Step`, debug decoding and nested loader/WndProc/APC paths remain single-step.

`GuestExecution::ExecuteCpu` approves a batch only for its current outer runnable
thread, matching CPU and address space, outside callback depth and without a
pending yield/unwind/exit. It caps execution at the remaining256-operation quantum
and charges every additional instruction. The existing checkpoint/resume boundary
is preserved, including the already-approved first instruction after suspension.
Pure emitted prefixes cannot call HLE, access guest data or suspend midway.
Stop limits and maxSteps are charged by actual execution, not dispatch calls.
Session retirement, host errno, TEB/TLS and owner lifetime remain unchanged.

Normal Switch use requires both `cpu-native.flag` and `cpu-native-batch.flag`
with first byte1. `cpu-reference.flag` wins. The owning integer report records
requested/effective engine, batch calls/instructions/maximum; default G5 stays.

## Reproducible software evidence

- Full actual ARM64 runtime: authored PE/DLL,8 normal reference/G5/native-step/
  native-batch new+legacy sessions and6 genuine main-HLE/nested-WndProc aborts.
  Independent kernel/DLL/APC/worker/file outputs and complete opcode/HLE/
  instruction/RunOne parity; real native loader/callback/APC/worker deltas.
-80 reference/native scheduler traces: caps1/2/3/7/31/32/33/255/256/10000,
  stop counts1/2/31/32/255/256/257/4096, peer fairness, flags/EIP/zero/debug
  and outside-executor guards, code-store release. x86 is explicitly fallback.
- Shared checked production validation runner:19 real ARM64 sessions, reference,
  warmup triplet, five rotating G5/native-step/native-batch triplets. Full census/
  quanta/checksum/file equality, native participation and post-session storage;
  cancellation/missing fixture are failures, missing native capability UNAVAILABLE.
- Source/fixture/SHA-bound paired ARM64/QEMU report schema2 records batch counters.
  Local G5/batch median ratios:1.227 total,1.637 first loop,1.656 hot loop.
  These are **QEMU timings, not Switch FPS or universal application gains**.
- Full host/MinGW/Mesa suite, existing G5–G9 oracles and all six exact-SHA CI jobs
  remain publication gates. See the published run for final CI evidence.

## Separate physical gate

[Switch procedure](G10_SWITCH_CPU_TEST.md) uses the exact CI NRO plus public PE/DLL,
SHA256/FNV manifest and automatic report. Actual hardware acceptance stays pending
until that report arrives. No repeated JIT42/SD investigation; other Paint/GPU/
input/relaunch physical gates remain deferred. The test does not change clocks.

Next: inspect physical CPU correctness/costs, then choose wider memory/stack/
block-linking work from measured fallback coverage. Do not turn native on by
default merely because this register-heavy fixture wins under QEMU. Full dynarec,
DirectDraw/D3D and broad workload/default activation remain open.

G10 packaging follow-up: CI41355944529 at4175be9 compiled the Switch NRO,
but the new ZIP step failed Git's container ownership check before packaging.
The packager now scopes `git -c safe.directory=<this checkout>` to its SHA query;
no global trust exception or runtime change. The completed local full host/
MinGW/Mesa runner passed. Physical CPU acceptance remains pending; publish only
the revised exact-SHA bundle after all six CI jobs and binary/fixture audit.
