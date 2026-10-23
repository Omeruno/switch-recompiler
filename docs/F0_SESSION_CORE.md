# F0 core guest session ownership

`GuestSession` owns a fresh `PagedGuestMemory` and `Process` for each top-level
launch. The Switch interpreter and host winrun use this scope. Declare it before
presentation cleanup and `GuestExecution`: native continuations must drain first,
then presentation must detach, then Process resources and address space disappear.
Return-to-launcher was later integrated in [F0_LAUNCH_LOOP.md](F0_LAUNCH_LOOP.md);
this document describes the earlier core ownership package.

Process now owns its default `GuestHeap` and a type-keyed, RTTI-free collection
of HLE state. Immutable type keys identify independently destroyed state; the
collection is not keyed by a native Process/memory pointer. Reusing the same host
storage and guest addresses cannot select an older Process's caches.

Migrated state includes:

- Heap HANDLE lookup and additional heaps; VFS root/current directory.
- CRT globals/errno/argv, atexit handlers, signals/new-handler, random seed,
  time/locale buffers and strtok/strerror continuation state.
- Command-line buffers, environment, error mode, exception-filter registration,
  dynamic TLS allocation bitmap and the existing bounded FLS bookkeeping.
- CRT descriptor/FILE metadata, Win32/CRT find state and temporary-name buffers.

OpenFile/HandleTable ownership already closes host files and implements
DELETE_ON_CLOSE. Session teardown does not execute guest callbacks. Normal CRT
exit continues to run its own atexit handlers; cancellation discards registrations
only after GuestExecution has unwound. Process-local state destructors contain
host bookkeeping only and do not consult current guest bindings.

The scope captures the old Process/import-binding and USER call context before
Process construction, clears legacy scalar guest-address/exit globals for launch,
and restores those bindings at scope exit. This permits native nested binding
tests; it does not support nested running GuestExecution scopes. Standalone HLE
harnesses without a bound Process retain their historical singleton fallback.

## Regression and boundary

`test_guest_sessions` runs four real MinGW/x86 sessions at the same placement-new
storage: normal exit, clean restart, host cancellation, clean restart. It checks
fresh heap/default state, independent environment/TLS/CRT random/string state,
restored stderr descriptors, discarded atexit registrations, flushed open files,
DELETE_ON_CLOSE, drained continuations and restored bindings. An initial failed
EXE load is destroyed before the four successful loads. A nested native ownership
test verifies matching guest addresses cannot alias an outer heap and owned state
is actually destroyed. MinGW's pre-main exception-filter installation is allowed.
The public C fixture also runs alone in the ordinary guest suite.

The original core package did not certify full GUI-session acceptance. The
new USER/GDI follow-up is implemented and covered by a 12-session x86 regression;
see [F0_GUI_SESSIONS.md](F0_GUI_SESSIONS.md). Legacy UI/MFC and image-binding lifetime is also covered by
[F0_LEGACY_SESSIONS.md](F0_LEGACY_SESSIONS.md). The frontend launch loop,
per-launch output/mode rebind and held-input quarantine
were subsequently integrated in F0_LAUNCH_LOOP.md. A core restart alone did not
authorize that loop; the later integration supplied its own host tests. The next
software milestone is F1–F5 in F_SHELL_PLAN.md.

No Switch memory/timing, Paint acceptance or physical repeated-launch result is
claimed by these host tests. Continue the consolidated deferred checklist in
[STAGE_E_PERFORMANCE.md](STAGE_E_PERFORMANCE.md).
