# F3a: real directory explorer and direct path launch

Implemented F3a baseline after F2 (`12c1137`). Subsequent [F3b](F3_DOCUMENTS.md)
adds document applications and arguments; path-only behavior below describes the
original F3a package. This is the first F3 package,
not completion of all F3/F4/F5 requirements. Physical checks remain deferred in
[STAGE_E_PERFORMANCE.md](STAGE_E_PERFORMANCE.md); SD diagnosis is closed.

## User-visible actions

My Computer and My Documents work from desktop and Start. The explorer shows
actual F1 directory-backed C/S/R volumes, files/folders, file sizes, page position,
address, back/up, refresh, sorting and quick roots. My Documents is the actual
`C:\Documents and Settings\Player\My Documents` profile directory. ROMFS is
read-only. My Computer sizes are **available bytes**, not total capacity; ROMFS
shows read-only without inventing its unmeasured capacity. Host screenshots show
host fixture storage capacity, not physical Switch SD measurements.

All Programs retains the bounded ROMFS/old-SD/drive_c/Programs executable catalog.
Explorer directories open and EXEs launch. Run accepts one explicit guest EXE or
directory path, with optional surrounding quotes, through the same request
validator as the catalog/explorer. Relative paths use the shell's remembered VFS
cwd; a process still starts beside its own EXE under the existing F1 policy.

This Run is **path-only**: no command-line argument parsing, executable search on
PATH, host command execution or document associations. Documents are visible;
Open is disabled for file types without support, and keyboard Open reports that
no application is available. The shell does not pretend that every listed EXE
is compatible: PE validation/startup may fail and return through F0 as before.

Controls retain stick/A/X/R/B/PLUS. Up/Down moves selection, Left/Right changes
pages; **Y cycles row/toolbar/quick-root focus**, so all enabled explorer controls
are reachable without a pointer. B first dismisses Start, then navigates history,
then closes the panel. Close always returns to desktop. Address and Run use the
Switch keyboard (maximum 500 characters in this adapter, within a 4096-byte host
buffer). Keyboard cancellation preserves path/selection; return quarantines held
buttons/touch, cancels pointer gestures and forces output restoration. Applet
shutdown stays sticky. Native state survives joined guest sessions; explorer
refreshes on return and keeps selection if the entry still exists.

## VFS, launch and resource contract

`ui::ShellBrowser` owns independent `rt::Vfs` mount/cwd metadata over the exact F1
roots. There is no second path normalization scheme, global host chdir, guest
Process dependency or retained DIR/FILE handle. Initial persistent configuration
creates only missing F1 directories; it is additive, not a migration. Ordinary
host winrun standalone policy stays unchanged. Configure failure leaves actions
disabled on an unconfigured shell; previous valid provider metadata survives a
failed reconfiguration.

Each explicit browser/Run/catalog open uses a `ShellLaunchRequest` with guest and
resolved native paths. The validator rejects absent/unmounted/UNC/stream/linked
paths and non-regular/non-EXE files, checks readability and clears a failed request
so an earlier valid EXE cannot be replayed. A later load failure remains possible.
This package carries no document arguments; F3b/F5 must extend the shared request
and production command line together before advertising document opening.

Enumeration keeps at most **256 entries** from **4096 scanned non-dot directory
entries**, with **32 history locations**, 4096-byte guest paths and no recursion.
Within the scanned prefix it retains the earliest entries in directory-first
ASCII-case-folded name order regardless of readdir order. Sort cycles name up,
name down and size down, keeping directories before files and retaining selection.
Changing sort rearranges that retained subset; it does not claim to sort entries
outside a truncated scan. Unicode names remain exact UTF-8 paths; complete
Windows Unicode collation/font coverage is not claimed. Unsupported native Unix
component names containing colon/backslash and special files are skipped with a
warning. Truncation, inaccessible entries, bad paths and read failures are visible;
failed navigation preserves the previous view/history/selection.

F1's VFS resolver now accepts an optional cumulative **4096-entry case-search
budget** used by shell operations. Exact-case paths need no directory search;
exhaustion returns empty with EOVERFLOW, never a guessed unchecked path. Guest
callers omit the budget and retain prior behavior. Listing shares one lookup
budget across path/children, with bounded additional validation on commit/open;
this is a work bound, not a physical I/O latency guarantee. F1 startup directory
creation remains a separate existing operation. Trusted configured roots and the
existing host symlink/race boundary are unchanged.

## Verification and remaining work

`test_shell_browser` creates public temporary C/S/R storage and checks actual
bytes/paths/sizes, root traversal, invalid/unmounted/linked paths, readonly data,
Unicode/case, deterministic retained subset, independent scan/item limits, history,
selection, failed navigation, cleared requests, cancellation, controller toolbar
reachability, and Start/Explorer/Run parity. Optional arguments are font directory
and screenshot prefix; it emits real native PPM computer/documents/explorer views.
Strict isolated harness build:

```sh
g++ -std=c++17 -O1 -Wall -Wextra -Werror -fno-exceptions -fno-rtti \
  -ffunction-sections -fdata-sections -Wl,--gc-sections -I. \
  tests/test_shell_browser.cpp source/ui/shell_browser.cpp \
  source/ui/desktop_shell.cpp source/ui/font.cpp source/ui/executable_catalog.cpp \
  source/runtime/files.cpp -o /tmp/test_shell_browser
/tmp/test_shell_browser romfs/fonts /tmp/sr-browser
```

The existing F1 real-x86 fixture now enters its four writer/reader/abort/reader
sessions through Start catalog, Run and Explorer requests, checking actual saved
Unicode document bytes and sibling DLLs. The native shell survives that session
teardown and returns to its former folder. The original session/dialog/readonly
checks still run. Required suite: `COMPOSITION_GL_TEST=1 sh tools/run_host_tests.sh`.
Host tests do not execute libnx keyboard, framebuffer and physical input; exact
published-SHA Switch CI and later physical acceptance remain separate.

F3b now implements document application choice and explicit guest arguments.
Next F3c: persisted preferences/association management, supported Start/settings
actions and directory-tree refinements.
F4 active-guest taskbar/compositor/input ownership and F5 save/find/reopen/persistence
remain open. No hardware Paint/GPU/fallback/input/relaunch result is added here.


Functional F update: [shared real file operations/properties](F_FILE_OPERATIONS.md)
are implemented in VFS and the browser model. Their native UI, confirmation and
properties dialog remain the next package; the original controls above do not
yet expose create/rename/delete actions.
