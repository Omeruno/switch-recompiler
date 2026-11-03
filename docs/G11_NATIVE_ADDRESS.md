# G11 — native 32-bit LEA address arithmetic

Successor: [G12 mixed coverage/fault boundaries](G12_MIXED_CPU_COVERAGE.md)
measures memory/stack/call workloads and specifies checked-load requirements.
The user deferred G10 hardware testing;historical results below remain scoped.


Date: 2026-11-03. Incoming verified runtime `1710370d9d56dab2b1df53c0fe351360e338b43d`
passed [CI 41356429899, all six jobs](https://github.com/Omeruno/switch-recompiler/actions/runs/41356429899).
Software implementation extends G6/G8/G9/G10. Physical CPU performance is pending.
G5 remains the default; reference priority and both explicit native/batch flags remain.

## Why this operation

The new authored `native_address.exe` computes an independently checked integer
recurrence using two LEAs, XOR, DEC and Jcc. There are 82,019 total instructions,
32,772 LEAs and exactly 324 scheduler quanta. On the incoming G10 engine only
49,131 instructions execute natively; the three-instruction tail repeatedly leaves
the native path at LEA. Before production edits, the actual full ARM64 runtime
under QEMU passed four reference/G5/native-step/native-batch sessions. Seven
rotating measured pairs after warmup gave G5/native wall ratios of about 0.82
end-to-end and 0.78 in the hot loop: the experimental path was slower than G5.

This is a deliberate diagnostic workload, not a representative game or proof that
LEA dominates Paint/Pinball. Its purpose is to measure a concrete compiler-generated
address-arithmetic boundary before changing the common backend.

## Implementation and bounds

The owning IR adds `Address`, with base/index register identities, scale and an
unsigned 32-bit displacement. Register 8 is an absent operand, not guest state.
Unprefixed opcode 8D supports all legal 32-bit ModRM/SIB forms, scales 1/2/4/8,
ESP/EBP bases, absent base/index, signed disp8 and full disp32, including aliases
with the destination. The operation computes modulo 2^32. It preserves all raw
lazy flags and ignores segment bases, as real LEA requires.

ARM64 emission uses constants, loads from CpuState registers and 32-bit ADD with
an optional shifted register, then writes the destination. There is no guest data
load/store, HLE call, pointer translation or IR/interpreter replay. Computing an
unmapped effective address is valid. Register reads precede the destination write.
The portable semantic oracle performs the same architectural operation independently
of emitted machine code; the full reference interpreter remains the comparison.

Existing limits remain 32 instructions / 192 source bytes. A seven-byte LEA sequence
stops at 27 nodes / 189 bytes before the next instruction would exceed the snapshot.
ModRM register form is illegal and stays on the existing UnknownOpcode/illegal-
instruction path. Operand/address/segment/lock prefixes and truncated encodings
stay in the interpreter. Preview preserves pending fault state. Current source,
owner, lease, callback/checkpoint, exact quantum and session retirement contracts
are unchanged. Guest memory access, calls/returns and wider dynarec work remain open.

## Verification and measurements

- `test_native_address.cpp`: 6,314 layouts, 152,304 IR/full-reference cases,
  76,536 positive actual native comparisons on ARM64. All legal ModRM/SIB forms,
  raw flags/segments, edge/random registers, wrap, aliases, unmapped addresses,
  cross-page code, budgets 0–33, source capacity, checkpoint mutation and illegal/
  prefixed/truncated fallback. x86 explicitly reports emission only, not ARM64 execution.
- `test_native_address_pe.cpp`: four genuine PE sessions compare independent
  recurrence outputs, complete opcode/HLE/instruction census and all RunOne quanta.
  The measurement runs eight alternating G5/native pairs, excludes the warmup and
  requires real native participation, LEA census and a prefix of at least five.
- The local rebuilt full ARM64/QEMU runtime reports 81,885 native instructions,
  largest prefix 5, unchanged total 82,019 and unchanged 324 quanta. Seven paired
  medians are about 1.43 end-to-end / 1.61 hot relative to G5 for this fixture.
  These are wall timings including load/session/code retirement and scheduler work;
  not exclusive CPU time, Switch FPS, universal gains or a timing pass threshold.
- Existing G10 eight normal sessions / six parked aborts, 80 scheduling traces and
  19 shared-validator samples pass locally. Strict focused host/ARM64 builds and
  focused host ASan/UBSan with leak checks pass. Full host/MinGW/Mesa also passed,including22 F and17 GPU groups/five reports.
  Exact-SHA six-job CI remains the publication gate; published artifacts record
  their own timings.

`stage-cpu-native-address` and `stage-cpu-native-address-arm64` contain public sources,
PE and logs; ARM64 also contains the actual runtime binary and paired JSON/raw log.
The report binds exact SHA/dirty state, sources, fixture, binary, compiler/runner,
counters and every sample. `dll_sha256` is null for this fixture; it loads no DLL
or window and makes no file-output workload claim. G10's separate PE/DLL/USER/APC/
worker/file tests remain in their existing artifacts and hardware test bundle.

To reproduce the before measurement, use a separate worktree at `1710370`, build
its full runtime with its `tools/run_arm64_native_dispatch.py`, then compile this
revision's authored `native_address.c` and `test_native_address_pe.cpp` against
those baseline runtime objects (with `-I` pointing at that worktree). Run the new
inspector with `--measure` under the same QEMU and identical PE. No G11 production
header or object may enter the baseline build. The inspector's ordinary four-mode
parity check is compatible with both versions. Keep the generated logs, fixture/
binary hashes and exact source identities; compare each version to its own paired
G5 arm instead of dividing unrelated wall times from separate machines.

## Hardware and next package

G10's original exact-SHA hardware package remains available and its user report
is still pending; G11 does not manufacture that acceptance. The automatically
packaged current CPU test still validates its documented G10 public PE/DLL scope;
it is not an exhaustive physical LEA or game acceptance test. Other Paint/GPU/
input/relaunch tests remain deferred. No SD or old JIT42 investigation is reopened.
Local Switch SDK is unavailable; CI produces the NRO.

Next independent work: measure mixed public memory/stack/call workloads and design
precise guest-data access/fault/rollback boundaries before extending native memory
execution. Actual Switch G10 costs and broader workload evidence are still needed
before default activation or application performance claims.
