# G12 — mixed CPU measurements and precise fallback boundaries

2026-11-03. Incoming verified HEAD `b653d950f335e60a2ddd5acd00f21b113d2c03a2`
passed [CI 41608994958, all six jobs](https://github.com/Omeruno/switch-recompiler/actions/runs/41608994958).
The user explicitly deferred the G10 Switch test. All outstanding physical acceptance is
**DEFERRED_BY_USER**; independent software work continues. G5 remains default.

## Optional engine census

`EngineCensus` adds fixed storage for six engines x 513 opcode slots, totals and
one accepted-attempt count. Storage is 24,680 bytes, with a compile-time 32 KiB
ceiling. Engines are fast-register, fast-mapped/LEA/stack, reference, portable IR,
native ARM64 and unclassified. The four existing CPU accounting sites explicitly
label their engine. HLE calls remain separate. Fault attempts and REP retain the
existing one-attempt accounting; prefixes are not additional instructions.

A profile may borrow a host census under `EngineCensusScope`; the binding must
end before copying that profile or destroying the census. Nested/null scopes
restore the previous binding. The shared instruction budget is checked before
both histograms, including zero/truncated limits. There is no second uncapped
counting path. A partial binding reports its own count separately from the whole
profile, rather than claiming complete coverage. Invalid trusted-host IDs use the
unclassified/fetch-failure slots safely. No guest address, data pointer or per-step
clock/I/O is recorded. Engine labels describe actual execution, not byte accesses
or reasons for refusal; a first opcode alone does not identify every ModRM form.

Normal Switch execution allocates no engine census when `profile-execution.flag`
is absent/off. Profiling requests an owning nothrow allocation beside the existing
owning profile. Both survive execution/continuation draining; the report is emitted
before their owners are destroyed. Allocation refusal reports an unavailable
census, without invented zeros presented as coverage. Detailed counters still
stop at the existing profile limit (normally one million attempts). Integer owner
counters may run longer; a truncated histogram is not a whole-session trace.
The report adds `engine-census-v1`, engine totals and nonzero opcode cells.

## Five independent public workloads

`native_mixed.exe` is an authored CRT-free PE32 fixture with five selectable cases:

| Workload | Executed work |
| --- | --- |
| register | LEA/DEC/Jcc recurrence |
| memory | SIB-indexed MOV load, ADD, XOR-to-memory and index wrap |
| stack | Actual balanced PUSH/POP plus recurrence |
| calls | Actual local CALL/RET, not HLE or a host arithmetic substitute |
| mixed | Indexed load/store, stack values, CALL/RET and arithmetic |

Each case seeds 1,024 actual guest cells, runs acknowledged 4,096/12,288-iteration
phases and returns through the real runtime. The inspector independently computes
both recurrence results and every final data cell; stack/call cases capture actual
ESP before/after their balanced assembly. Full opcode/HLE/instruction/RunOne parity
is required across reference, G5, native-step and native-batch with engine census
on/off: 40 sessions plus five baselines. Matrix rows/columns must exactly match
profile histograms and per-owner engine counters, with no unclassified events.
Session/code retirement must drain. x86 host runs report native-unavailable fallback.

The paired report contains 80 rows: five workloads x eight pairs x census on/off,
one excluded warmup and seven measured pairs per condition. Both controls retain
the global opcode/HLE profile; the control isolates the extra engine census,
not all profiler overhead. Within each pair G5/native order rotates; census order
also rotates. Hot intervals measure the optional-census wall ratio. End-to-end
includes bounded metadata initialization, checks and full session/code teardown,
so its on/off ratio is not a pure instrumentation-cost measure. Sub-unity noisy
overhead samples are retained and are not a claim of free or negative overhead.

The JSON/raw log bind the exact clean SHA, full runtime/source hashes, fixture,
actual ARM64 ELF, compiler flags/versions and runner. All values/counters/parity
are checked before timings are published. There is no speed threshold. Local
profile-only hot measurements from the fresh full ARM64/QEMU runtime were:

| Workload | Native fraction,whole session | G5/native hot wall ratio |
| --- | --- | --- |
| register | 97.9% | 1.21 |
| memory | 72.1% | 0.77 |
| stack | 61.9% | 0.81 |
| calls | 61.9% | 0.61 |
| mixed | 54.8% | 0.61 |

Ratios below one mean native was slower. This falsifies a broad win inferred from
a register-heavy fixture: repeated exits at data/stack/control instructions still
cost more than they save here. The public counts identify MOV/XOR memory forms,
PUSH/POP and CALL/RET as major fallback sites. These are synthetic QEMU workloads,
not representative game FPS, physical Switch results or a default-activation gate.
Published CI artifacts contain their own timings; do not transplant local numbers.

## Memory/fault boundary contract

`test_native_memory_boundary.cpp` runs 17 cases / 68 sessions and 51 comparisons
against full reference. A real, primed native four-instruction prefix precedes the
existing data/stack/control instruction. Checks include missing/cross-page loads
and stores, separately allocated adjacent mappings, PUSH/POP including ESP aliases,
SS/FS bases, direct/indirect CALL, RET and a successfully committed CALL followed
by a target-fetch fault. EIP/ESP, raw lazy flags, exception code/address/write bit,
all 12 KiB of the checked data/stack span, census, quanta and code retirement match.

Faulting 32-bit stores/PUSH/CALL do not commit partial bytes. A target-read failure
precedes the CALL's push; a later fetch failure retains the already committed
return address/SP. The previous native prefix stays committed, while the faulting
instruction rolls back only its own register effects. The test does not discard
real side effects from a completed preceding instruction.

The cross-page slow paths in `Read16`/`Read32` now explicitly sequence lower bytes
before higher bytes. The first sticky fault address therefore does not depend on
the compiler's unspecified evaluation order for binary operands. In-page memcpy
and values are unchanged. These checks establish the current mapped-memory and
scalar32 contract, not full NT page-right/guard enforcement, arbitrary RMW/string/
SSE atomicity or all address-space wrap semantics. Stored page rights remain the
existing incomplete policy; this package does not claim to implement them.

## Verification and next implementation

Fresh ARM64/QEMU runs pass the new matrix and boundary tests and the prior G10/G11
PE/DLL/callback/APC/worker, eight normal/six abort, 80 scheduling-trace and19 shared
validation gates. The full host/MinGW/Mesa suite,22 F groups and17 GPU groups/five
reports pass. Focused strict/profile tests cover budgets, invalid IDs, nested/null
binding, reporting, actual IR/native accounting or honest architecture fallback.
Focused sanitizer checks instrument changed CPU/profile code; remaining runtime
objects are ordinary and this sanitizer probe does not use guest fibers.

Artifacts: `stage-cpu-native-mixed` and `stage-cpu-native-mixed-arm64` contain source,
public PE/logs; ARM64 adds its actual inspector and the paired JSON/raw output.
Exact-SHA all-six CI, NRO/source/fixture/report audits and Pages remain publication
gates. Local Switch SDK is unavailable; CI supplies the NRO. No physical test is
requested while the user's deferral stands; old SD/JIT42 investigations stay closed.

**Next G13:** checked 32-bit MOV loads, with a dynamic native executed-prefix result
instead of G8's fixed completed-prefix assumption. Before a failed access, no
load-destination/EIP commit or fictitious native instruction count is allowed.
Keep completed earlier nodes committed, refuse at the current instruction and let
the common interpreter produce its real fault. Mapping/owner/source validity and
cooperative stop/quantum accounting must remain exact. A host mapping pointer may
be borrowed only synchronously; no guest-data pointer survives a callback/yield.
Stores, stack writes/CALL, linking and broader memory policy are later scopes.
