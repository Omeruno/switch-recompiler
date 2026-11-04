# Bundled JavaScript engine

`quickjs/` contains the core QuickJS sources from the official
[2026-06-04 release](https://bellard.org/quickjs/), without its command-line
tools or POSIX helper library. Copyright and MIT license terms are preserved in
`quickjs/LICENSE`. The browser compiles these sources for Switch and host tests.

The JavaScript runtime is an embedded engine, not a browser DOM implementation.
Only the small `document` and `console` bridge in `source/js_runtime.c` is
available to page scripts at this stage.
