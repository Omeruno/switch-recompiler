# G7 — bounded session-owned executable storage

Software milestone,2026-11-01. Builds on [G6 IR](G6_CPU_BLOCK_IR.md).
Production still executes through G5;this package does not translate x86 or
increase normal application FPS. Successor [G8](G8_ARM64_EMITTER.md) now emits actual bounded ARM64 using
the G6 semantic/reference oracles and this storage. Next is guarded native
dispatch/cache and exact scheduler/callback integration. Full dynarec,dispatch/cache
invalidation,scheduler integration and GPU API translation remain open.

## Ownership and publication

`source/cpu/code_storage.h/.cpp` owns trusted host machine-code images,never raw
guest executable buffers. `GuestSession` owns a lazy `CodeStore` bound to its
unique memory identity. Ordinary sessions allocate no executable region.
Backend calls are synchronous/non-reentrant;one native owner may use cooperative
fibers. Parallel native-thread use is unsupported. Backend injection is trusted.
No guest data pointer,write alias or persistent executable entry is exported.
`CodeLease::Invoke` lends the entry only for its synchronous consumer;the pointer
must not escape. The private invocation pin survives a cooperative yield and
consumer destruction of its original lease/store.

An opaque store ID + monotonic slot serial prevents old tokens reentering a reused
slot or another session. Acquire/Invoke check the exact memory owner. Retirement
immediately denies new acquisitions and invocations;already entered code finishes
before release. Immutable replacement uses another allocation. `Close` permanently
retires everything and reports Drained/Pending/Retained;it never pretends that a
pinned frame was freed. GuestSession closes storage after GuestExecution drains
and before guest backing memory disappears. Escaped retired leases preserve code
only,not the memory object or permission to call again.

Code storage does **not** validate translated-source bytes. Future dispatch must
also validate G6 source/owner/entry and budget/checkpoint contracts. It must drain
native guest continuations before destroying the session. No production emitter,
cache lookup,batching,guest ABI register mapping or guest-fault translation is
introduced here.

## Bounds and failure

Default limits are16 images/1MiB per store;hard limits are32 images per store,
64KiB per image,64 images/4MiB process-wide. Actual page-rounded backing counts
against the quotas,including retired pinned code and failed releases. Inputs are
nonempty and4-byte sized;backend granules must be power-of-two4096..65536.
Allocation/initialize/publication/entry failure invalidates the output token and
attempts one close. Already published images stay intact when another allocation
fails. Capacity/backend metadata are trusted host interfaces.

A failed or ambiguous platform operation can leave partial mappings. Such regions
are retained,charged to the same global limits,and never closed twice. After the
last arena owner disappears,a fixed64-slot process-lifetime vault owns them.
Repeated sessions therefore cannot grow executable backing beyond the global
bounds. This deliberately sacrifices capacity instead of risking an unmapped
return address or double release. Exhaustion fails publication;it is not a success
stub or an unbounded retry. Metadata uses nothrow allocation where practical;
this is not comprehensive process-heap OOM recovery. Global state is reachable
for process lifetime,including intentionally retained failures.

## Platform protocol and its limits

Linux: private mmap RW,poison/copy,clear instruction cache,mprotect RX,clear cache
again. No RWX mapping and no rewrite after publication. Release is munmap once.
The executable permission is independently checked through `/proc/self/maps`.

Switch: official libnx `jitCreate`,`jitTransitionToWritable`,private RW copy/cache
flush,`jitTransitionToExecutable`,RX cache invalidation,then `jitClose` once.
Transition failures or missing published RX are ambiguous and retained without a
close attempt. Failed close is sticky because libnx may have partially unmapped
aliases. Other errors propagate without claiming successful publication.

**libnx CodeMemory can hold separate RW and RX aliases simultaneously.** Its
writable transition is a no-op in that mode. Thus Linux enforces RW→RX protection;
on Switch the API enforces immutable publication and hides the write alias,but
this is not a claim of kernel-wide physical W^X. Do not bypass libnx or free its
backing manually. See public [libnx jit.h](https://github.com/switchbrew/libnx/blob/master/nx/include/switch/kernel/jit.h)
and [jit.c](https://github.com/switchbrew/libnx/blob/master/nx/source/kernel/jit.c).
Unsupported platforms return no backend. No libnx implementation is copied.

## Reproducible acceptance

`tests/test_code_storage.cpp` executes8192 generated native arithmetic calls,
checks replacement/slot ABA,owner rejection,page permissions,byte/image/global
quotas,allocation/initialize/publication/entry faults and sticky failed close.
It also fills the failed-release vault across64 destroyed stores,including failed
publication+failed close,and proves the next store cannot allocate past the cap.
Real generated code saves a callee-saved register/return frame,calls C++,parks
inside HostFiber,and resumes after its original lease and store are destroyed.
The invocation pin keeps the native return address valid until completion.
This is run natively and under ARM64/QEMU with actual ARM64 words;it is not an
x86 translator benchmark or a repeat of the earlier physical JIT42 probe.

`tests/test_code_session.cpp` uses real full-runtime GuestSession objects:lazy
allocation,nested owner isolation,real generated calls,retired escaped lease,
sequential reuse and complete successful release. Both run in
`tools/run_host_tests.sh`,alongside all existing PE/scheduler/audio/UI/GPU gates.
The standalone storage test also runs with strict warnings and ASan/UBSan with
leak detection;`--no-fiber` omits unsupported ASan stack switching but still
executes native generated code and failure checks.

Public CI artifacts: `stage-cpu-code-storage` (host/session logs,exact sources,
contract) and `stage-cpu-code-storage-arm64` (ARM64/QEMU log,exact sources,
contract). Existing G5/G6 and native-x86 hardware oracles remain gates.

Local Switch SDK is unavailable after the prior devkitPro HTTP403;the actual
Switch adapter/NRO are compiled by CI. Linux/QEMU and successful cross compilation
are software evidence only. Physical libnx-mode/storage acceptance,application
FPS and Paint/GPU/input/relaunch acceptance remain deferred. No SD diagnosis or
new hardware result is inferred.
