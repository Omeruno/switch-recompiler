# F3b: document applications and launch arguments

Software package after F3a (`12f0114`); full F3 still has supported settings/tree
work remaining. Current scope is one guest at a time, with hardware acceptance
deferred in [STAGE_E_PERFORMANCE.md](STAGE_E_PERFORMANCE.md).

## Actions

Explorer Open on a document without an application asks for an explicit guest
EXE path. **Choose application** lets the user select/change that application.
The filename is passed as one absolute guest argument, including spaces and
Unicode. No Paint/other application is guessed from filenames or advertised as
compatible merely because its EXE exists. The usual loader/startup failure path
still returns to the same shell. Choosing an application establishes only the
user's intent, not verified application compatibility.

For extensions of 1–16 ASCII letters/digits/underscore/hyphen, the lowercased
extension is remembered in the current shell (up to 32 types). That state
survives guest exits, failures and fresh GuestSession teardown; it ends when
homebrew exits or the browser's roots are explicitly reconfigured. There is no
on-disk association persistence yet. Files without a supported extension can be
opened once with an explicit application; EXEs cannot be remapped as documents.
Missing/unreadable applications fail with a visible error; Choose application
can replace them. Cancellation changes no association and launches nothing.

Run accepts a quoted EXE path followed by Windows-style arguments. Empty args,
spaces, embedded quotes and terminal backslashes are preserved. Example:

```text
"S:\Apps\Viewer\viewer.exe" "C:\Documents and Settings\Player\My Documents\drawing.bmp"
```

Run with one document path uses the same association, including the initial
application prompt; one directory path opens the explorer. An unquoted **entire
existing literal path** with spaces retains F3a behavior. Adding arguments to a
path with spaces requires quotes. No PATH search, expanding executable-prefix
guesses, environment expansion or native host command execution is introduced.
Extra arguments are supported for explicit EXEs; document associations supply
one document argument. F1 still starts each guest beside its EXE: relative raw
Run arguments are interpreted there. Generated document arguments are absolute.

## Transport and bounded parsing

ShellLaunchRequest carries the resolved native executable, guest executable,
argument vector and fully quoted guest command line. Catalog, Explorer, Run and
Choose application use the same EXE validator, rechecking documents/applications
and clearing failed requests. The frontend clears command transport at every
selection/fallback and copies the selected request before starting the worker;
old arguments cannot leak into sole-EXE autolaunch or a subsequent selection.

`runtime/command_line.h` shares quote/backslash parsing and encoding. Bounds are
64 arguments including the executable and 16 KiB encoded input/output. Shell Run
rejects unmatched quotes; CRT accepts the final quoted tail as Windows CRT does.
The canonical encoded forms are covered by native and actual-x86 tests. This is
not a claim to implement every historical CRT/Shell32 command-line quirk.

Process::GuestCommandLine normalizes the complete known native executable token
for legacy callers, preserving its suffix; already guest-formatted requests
remain intact. KERNEL32 and the bootstrap CRT use that same guest line. The
bootstrap CRT previously truncated to 63 bytes and forced argc=1; it now has a
bounded 128 KiB area for full ANSI/UTF-16 strings and argv with preserved legacy
pointer slots. A compile-time size bound covers its storage. Native MSVCRT wide
command/argv strings now decode the bridge's UTF-8 representation rather than
widening bytes individually; ordinary non-command ANSI/environment conversion
is not changed.

CommandLineToArgvW now parses bounded UTF-16 input and returns pointers/strings
in one process-heap block compatible with LocalFree. Empty input uses the module
path; leading whitespace yields an empty first argument. Invalid pointers/count
or exceeded limits fail, rather than returning a fabricated single-word result.
GetCommandLineA remains the project's UTF-8 bridge policy; this is not complete
Windows ACP behavior. Standalone winrun quotes individual host argv values so
empty/space-bearing arguments reach the same decoder.

## Public checks

- `test_command_line`: quote/backslash round trips, empty args, tabs, Unicode,
  malformed/oversized/NUL input and argument limits.
- `test_shell_browser`: explicit application choice/cancel, automatic prompt,
  remembered type, uppercase extension matching, full type-table rejection,
  missing application, typed Run arguments and existing navigation regressions.
- `shell_document.exe` / `test_shell_documents`: a public MinGW reader executes
  four fresh x86 sessions: Run with complex arguments → document/application
  choice → remembered association after return → document through Run. It checks
  actual narrow/wide CRT argv, GetCommandLineA/W beyond 63 characters,
  CommandLineToArgvW/LocalFree, bootstrap CRT data and real Unicode/space/emoji
  document bytes. This is interpreter execution, not only native request assembly.

The reader opens a pre-existing public document and checks its content without
modifying it. It is not a Paint save/reopen acceptance test. The regular guest
loop skips it because its dedicated shell/mount harness must execute it. The
complete `COMPOSITION_GL_TEST=1 sh tools/run_host_tests.sh` retains all previous
filesystem/session/dialog, CPU/GL composition and Stage C/E regressions. Actual
libnx keyboard/output is checked by exact-SHA Switch compilation and later
physical testing; host screenshots do not replace that acceptance.

## Next

F3c: bounded persisted preferences/association management, remaining supported
Start/settings actions and directory-tree refinements. F4 still needs active-
guest compositor/taskbar/input ownership, and F5 needs the complete public
save/find/reopen scenario plus deferred physical Paint/SD/input/GPU checks.
