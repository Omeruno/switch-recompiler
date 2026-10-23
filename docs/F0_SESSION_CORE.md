# F0 core guest session ownership

`GuestSession` owns a fresh `PagedGuestMemory` and `Process` for each top-level
launch. The Switch interpreter and host winrun use this scope. Declare it before
presentation cleanup and `GuestExecution`: native continuations must drain first,
then presentation must detach, then Process resources and address space disappear.
The current NRO still exits after one application; return-to-launcher is not enabled.

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

This is **core ownership**, not full GUI-session acceptance. USER classes/windows,
menus, image/GDI objects, legacy controls/MFC and other module singletons still
need explicit lifetime audit/reset. A successful core restart must not authorize
a production launch loop. The next gate is sequential real-x86 window/GDI/dialog
sessions, including cancellation and load failure, with stale callbacks/objects
and renderer snapshots made unreachable before the next launch. Only after that
should picker-return-picker be connected and checked physically in both renderers.

No Switch memory/timing, Paint acceptance or physical repeated-launch result is
claimed by these host tests. Continue the consolidated deferred checklist in
[STAGE_E_PERFORMANCE.md](STAGE_E_PERFORMANCE.md).
