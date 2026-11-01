# G6 — bounded owned block IR and portable execution oracle

2026-11-01. Incoming `fe29b2493f4bb1a99895e93f5df4d11be48c151f` passed
[CI41243997654, all six jobs](https://github.com/Omeruno/switch-recompiler/actions/runs/41243997654).
This closes G5's next bounded frontend/oracle step. It supplies a semantic block
contract for future ARM64 emission, not an ARM64 dynarec or new production speedup.
Ordinary Switch/application execution remains the G5 interpreter path.

## Frontend and representation

Block owns at most32 nodes and192 source bytes, with no allocations, guest data
pointers or executable-memory leases. Each node records an unprefixed32-bit
operation, guest offset/length, operands/immediate, branch target, register
read/write masks and flags read/write masks. Supported operations are MOV,
register XCHG/NOP, ADD/OR/ADC/SBB/AND/SUB/XOR/CMP, INC/DEC, TEST, direct JMP and all
16 short/near Jcc conditions. ModRM must identify registers. Memory operations,
stack access/calls/RET, prefixes, shifts/IMUL, x87/SSE and other instructions stop
this frontend; the complete instruction remains for the interpreter.

The frontend uses checked current mapped bytes, never the legacy unbounded
Decode(pointer) interface. Incomplete instruction tails are not copied or emitted.
Inspection neither clears nor raises a guest memory fault. Limits, unsupported
instructions, unavailable fetch, explicit host boundaries, branches and top-of-
address-space boundaries are recorded separately. Direct targets retain IA32
wrap arithmetic; a fetch spanning32-bit wrap is conservatively left to fallback.
No host-call object, success result or synthetic guest exception is fabricated.

A source snapshot belongs to a unique address-space identity, independent of the
native C++ object's address. PagedGuestMemory assigns an atomic64-bit identity;
placement-reusing the same host address cannot accept an old block. Memory's
normal G5 policy/counters remain per owner. Blocks expose const node access only.
This is a trusted host frontend object, not a deserializer for untrusted IR.

## Portable semantic oracle and exits

The portable executor implements the node operations directly over CpuState,
using its architectural/lazy-flag model. It does not replay the source through
Interpreter::Step. ADC/SBB preserve carry inputs, INC/DEC preserve CF, comparisons
avoid register writes, and conditional branches leave lazy flags intact.
Shared instruction accounting preserves debug EIP, synthetic timestamp increments
and opcode census. IR work has a separate optional counter and appears in the
existing diagnostic report's total when explicitly executed.

Execute accepts an instruction budget and optional pre-instruction checkpoint.
Budget0 executes nothing. Resume must be at a node boundary. Branches always exit,
including not-taken Jcc; loops cannot spin inside one call. Host/unavailable/
unsupported tails, capacity, budget, wrong owner/entry, changed source and requested
checkpoint stop remain distinct outcomes. Prefix work is counted exactly; no
unsupported instruction is skipped or advanced. The caller retains fault/SEH/HLE
handling through the full interpreter at the current guest EIP.

Whole source bytes/owner are validated at entry and again after every checkpoint.
A checkpoint may park/yield or alter bytes: stale work is refused before the next
node. Hook effects themselves are caller effects and are not rolled back. Without
hooks the supported nodes cannot yield or write guest memory, so one validation
covers the synchronous run. This relies on the existing sole native owner;
parallel guest execution is not added. A host-boundary predicate is a non-yielding
metadata predicate, not a guest callback.

Raw HostPointer writes, changed mappings, wrong owners and C++-address ABA are
covered. Remapping identical bytes in the same owner remains semantically valid:
IR owns bytes and guest targets, not host pointers. This is not an executable code
cache or proof of future machine-code retirement/invalidation. Page access-right
semantics remain those of the existing interpreter; full page-right enforcement
is a separate incomplete contract.

## Public executable evidence

The native68865-case differential probe executes147069 nodes in the generated
comparisons, including mixed dependency/flag/branch sequences and every budget
prefix. It checks exact registers, raw lazy and materialized flags, EIP and opcode
census against the full reference. Additional probes cover flag/register metadata,
32-node/192-byte limits, source mutation before entry/during a checkpoint, owner
ABA, unmap/remap, host boundaries, missing fetch tails, fallback AV/rollback, top-
address-space handling and identical RDTSC accounting. Native O2 strict and full
CPU+probe ASan/UBSan/leak-scan gates apply; this probe has no guest fibers. The
same actual ARM64 executable is run under QEMU, not physical Switch hardware.

The native x86 hardware oracle now compares three paths for each existing case:
G5 fast, full reference and portable IR with reference fallback, before comparing
against native instructions. Three seeds×50000 cases remain the CI gate. Logs
include both fast/reference and IR/reference parity plus a nonzero accepted IR
count for this mixed suite. Local QEMU-i386 is not a hardware substitute: the
existing seven AAM discrepancies in the50000-case integer sample remain in both
reference and experimental paths; IR/reference agrees there. Real native CI is
required before calling the published SHA ready.

The authored old_pe_imports.exe/ordinal DLL fixture now additionally supports mode2
SleepEx(INFINITE,TRUE) for a genuinely suspended native HLE frame; existing modes
and payload remain unchanged. The new PE inspector runs four normal new/legacy
sessions (reference versus explicit portable IR) plus two such aborts. It checks
actual loader/IAT/ordinal DLL execution, real Win32 APIs and independently read
file bytes. Full opcode/HLE histograms, total instruction counts and scheduler
quanta match in normal sessions. A live stack scope proves the abort is inside
the HLE call and is unwound after executor destruction. Plain Sleep/Sleep0 is not
used as evidence of a retained HLE frame because it completes before yielding.

This test-only PE adapter executes one IR instruction per scheduler step. It does
not enable production batching or charge multiple instructions as one quantum.
Loader/native callback loops still retain their existing interpreter path. Batched
integration with TEB/SEH/APC/callback/stop boundaries remains future work.

CI artifacts: stage-cpu-block-ir (authored PE/DLL, inspectors, logs, exact core and
contract), stage-cpu-block-ir-arm64, and extended stage-cpu-hardware-oracle. Existing
G5 and full host/MinGW/Mesa/session/graphics/audio tests remain required. No private
game assets, archives, executables or traces are published. Local Switch SDK is
unavailable; CI builds NRO. Hardware acceptance remains deferred.

## Next concrete block

Successors [G7 storage](G7_EXECUTABLE_STORAGE.md) and [G8 actual ARM64 emitter](G8_ARM64_EMITTER.md)
now supply bounded publication/retirement and native emission with three-way
comparison. Next:guarded dispatch/cache and exact scheduler/callback integration. Preserve exact budgets, source validation, fallback and
native continuation draining before production activation. The old JIT42 probe
need not repeat and is not accepted as a translator. Unsupported IR memory/stack/
SIMD operations, complete invalidation/alias handling, hot-block selection and
parallel execution remain explicit later work. G5 production, CPU GDI default,
Luna/F7/F8/L/ZL, media and the closed SD recovery issue are retained.
