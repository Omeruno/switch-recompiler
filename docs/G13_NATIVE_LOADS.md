# G13 — checked native 32-bit MOV loads and dynamic prefix ABI

2026-11-04. Incoming verified runtime `af8feab0e0a52d440813d16761c31475ba8178df`
passed [CI 41641059407, all six jobs](https://github.com/Omeruno/switch-recompiler/actions/runs/41641059407).
Outstanding hardware checks remain DEFERRED_BY_USER. G5 stays the default.

## Common instruction support

Owning IR and the ARM64 backend now support unprefixed 32-bit `8B /r` memory
loads and `A1 moffs32`, in addition to existing register MOV. All legal ModRM/SIB
forms, signed disp8/full disp32, scales, absent base/index and destination aliases
use current CPU registers. DS/SS selection follows the effective-address base;
absolute/no-base forms use DS. Linear arithmetic wraps modulo 2^32 as in the
current interpreter model. Source fetch remains checked and bounded.

Emitted ARM64 computes the address and calls a read-only mapped-memory leaf with
the owning address space. That leaf checks the entire four-byte span and reads
through the existing page table, including unaligned reads across separately
allocated adjacent pages. It returns a separate success bit and the real uint32
value, so zero and all-ones data are valid. This is a memory-access helper, not
Interpreter::Step or portable-IR replay. No guest data/mapping pointer is retained.
There is no allocation, HLE callback, guest-data write or yield inside the leaf.

Only successful loads commit the destination and EIP; raw lazy flags remain
unchanged. Missing/wrapping spans refuse native execution without setting or
consuming a pending guest fault. The interpreter then handles the same instruction
and its real fault/model behavior. Prefixes, other widths, stores, RMW/string/SSE,
stack writes/CALL/RET and full NT page-right/guard enforcement remain outside this
package. Mapping protection semantics remain the current common interpreter's
policy; there is no new permission-bypass or full MMU acceptance claim.

## Native ABI v2 and accounting

The private process-local entry is now:

`uint32_t entry(CpuState*, uint32_t firstNode, uint32_t budget, IGuestMemory*)`.

It returns the number of committed nodes relative to that entry, including an
executed branch. W21 counts commits; X22 holds the memory argument. X19–X22,
X29/LR are preserved in a 48-byte,16-byte-aligned frame across flag/read helpers.
The ABI probe now performs an actual checked load as well as arithmetic/branch
work. Images are session-local, never a serialized cache; no old image survives
an application/NRO restart.

The wrapper accounts only the returned prefix. Budget/interior resumes and hooks
retain exact instruction counts, debug EIP, synthetic timestamp and opcode/engine
census. A later refused load keeps earlier committed registers/flags and the EIP
at that load. An initial refusal returns zero. There is no speculative destination,
EIP, fault or instruction count for the refused node. The portable IR independently
implements the same refusal/commit boundary for comparison with full reference.

NativeCache treats MemoryRefusal as retryable, not a backend failure or unsupported
negative entry. A positive prefix returns to the scheduler first; a zero-prefix
retry falls back to the common interpreter. A subsequent mapping can reuse the
same translation without recompilation. Existing current-byte/owner/lease checks,
errno preservation,256-operation quantum and maxSteps accounting remain intact.
Loads cannot modify source bytes and the sole guest owner cannot remap during the
synchronous call; checkpoint hooks revalidate code and read current data mappings.

## Evidence

- `test_native_load.cpp`:6,313 layouts,50,504 mapped IR/full-reference comparisons,
  12,626 refusals and50,504 actual ARM64 load comparisons under QEMU. Coverage
  includes every legal ModRM/SIB form and moffs,DS/SS,zero/sign-bit/all-ones data,
  aliases including ESP,unaligned split mappings and exact raw flags.
- Additional checks cover all budgets0–33 and interior entries in a32-load block,
  a data remap between checkpoint invocations,a refused later load after three
  committed nodes,old pending fault preservation,common AV handling and successful
  reuse after mapping the missing page. A refused node never counts as native.
- The prior emitter suite still passes52,437 native/reference/IR cases and its
  expanded ABI probe;old unsupported-read assumptions now explicitly test stores.
  Cache negative-admission checks use an actually unsupported store, while new
  load refusal/recovery is tested separately.
- The17-case/68-session memory boundary suite now admits the successful split-page
  load natively (five committed native instructions rather than four);faulting
  data/stack/control paths retain exact EIP/ESP/flags/12KiB bytes and quanta.
- Fresh full ARM64 runtime retains all G10/G11/G12 PE/DLL/callback/APC/worker,
  abort,80 scheduling traces,19 shared-validation and mixed workload gates.
  Full host/MinGW/Mesa and focused strict/ASan/UBSan/leak gates accompany publication.
  Host x86 reports generation/fallback only, never actual ARM64 acceptance.

The unchanged five-workload G12 matrix measures this extension against G5 with
engine census on/off and independently checks values,1,024 cells,stack balance,
full opcode/HLE/instruction/quanta equality. Local whole-session native coverage
rose from72.1% to85.6% on memory and54.8% to62.2% on mixed. Local profile-only hot
G5/native wall ratios were approximately1.08 register/0.91 memory/0.78 stack/
0.59 calls/0.65 mixed. Slower results are retained: this remains an experimental
partial backend,not an overall win or default-activation gate. These are synthetic
ARM64/QEMU measurements,not Switch FPS. Exact-SHA CI reports bind their own compiler,
ELF/PE/source hashes and raw timings; do not substitute local ratios for them.

Artifacts `stage-cpu-native-load` and `stage-cpu-native-load-arm64` carry focused
logs and exact source/contract. Existing native-mixed artifacts provide the actual
full runtime and paired report. CI retains six jobs and produces the actual NRO;
local devkitPro/libnx SDK remains unavailable. Hardware tests stay deferred without
requesting a user report. No SD/JIT42 repeat or physical/game acceptance is inferred.

## Next G14

Add checked32-bit MOV stores with precise refusal/commit semantics. A successful
store must end its native block before any further instruction executes: the guest
may write source code,including its own translated prefix. Revalidate current bytes
on the next dispatch,retain atomic mapped-span writes and exact counters/quantum,
and test self-modification,aliases,split mappings and recovery. Stack/CALL/RMW and
broader memory policy follow as separate scopes; default activation still requires
mixed-workload and physical evidence.
