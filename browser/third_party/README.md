# Bundled JavaScript engine

`quickjs/` contains the core QuickJS sources from the official
[2026-06-04 release](https://bellard.org/quickjs/), without its command-line
tools or POSIX helper library. Copyright and MIT license terms are preserved in
`quickjs/LICENSE`. The browser compiles these sources for Switch and host tests.
Two guarded `__SWITCH__` changes in `quickjs.c` use the portable timezone path
and avoid a newlib `malloc_usable_size` call. The browser supplies a size-aware
allocator through `JS_NewRuntime2`, preserving QuickJS's memory accounting.

## Browser module-host extensions

The visual browser adds a narrow asynchronous loading interface in quickjs.c/.h:

- JS_EVAL_FLAG_DEFER_MODULE_RESOLUTION only with MODULE | COMPILE_ONLY: parse a
  real module record without invoking synchronous dependency loading.
- JS_GetModuleRequestCount/Name/Attributes inspect the parsed dependency list;
  returned atoms/values are owned by the caller.
- JS_SetHostImportModuleFunc defers import() from its normal engine job to the
  browser queue. Callback arguments are borrowed; retained resolver values must
  be duplicated. The existing normalize/loader callbacks link preloaded records.
- JS_MarkPromiseHandled lets the host inspect evaluation promises without calling
  page-modifiable Promise.then. Synchronous module evaluation also marks the
  internal rejected promise it consumes, fixing a false unhandled rejection.
- Failed dependency resolution rolls back its resolved flag. With the async host
  installed, JS_ResolveModule preserves other retained, still-compiling graphs.

Without the new compile flag/callback, ordinary resolution/loading remains the
legacy path. Tests cover parsed imports, linking/evaluation, cache/error identity,
TLA, quotas and realm teardown; actual browser fixtures also run in Chromium.
No POSIX/os module, filesystem loader, native JIT or worker runtime is exposed.

QuickJS remains an embedded language engine, not a browser DOM. The reader uses
source/js_runtime.c; visual/source supplies a separate retained DOM and network/
module integration. Their capabilities and limitations are documented separately.
