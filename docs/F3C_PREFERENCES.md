# F3c slice: persistent shell sorting and document applications

Based on `62b50f0`, exact CI 40099366270 passed all six jobs. This implements a
bounded native preferences/association slice, not all Windows settings or the
remaining directory-tree/workspace/document milestones.

## Supported settings and management

Explorer sort mode and up to 32 document-extension/application mappings are now
stored in the persistent F1 profile. Choosing an application or changing sort
attempts an immediate save; navigation, row movement and rendering do not write
preferences. No default Paint association or compatibility is inferred. Files
without supported extensions still use the existing explicit one-time open.

Start -> Shell settings exposes the actual sort mode and association records.
Up/Down or Previous/Next selects a record, Change EXE revalidates an explicit
executable path, Forget type removes that mapping, and Save retries/initializes
storage. B/Escape/Close dismisses the window; accepted changes are immediate,
not an unapplied draft. Cancelling the EXE picker leaves the mapping unchanged.
The pending extension is captured independently of selection. No document or
application data is deleted by Forget type.

A fresh shell restores sorting and mappings; documents still launch through the
shared F3b quoted absolute-path argument transport. Missing/moved/unreadable
applications fail through the usual validator and can be reselected; saved paths
are not proof of executable compatibility. Current location/history/clipboard,
registry, theme, input layout, installer state and application settings are not
persisted by this package. There is no arbitrary preferences editor or system
control panel. Other F3c/F4/F5 work remains open.

## Storage contract

Native profile location:

```text
C:\Documents and Settings\Player\Application Data\SwitchRecompiler\prefs-0.bin
C:\Documents and Settings\Player\Application Data\SwitchRecompiler\prefs-1.bin
```

The provider resolves these through the F1 VFS, checks write intent and adds only
its missing directory. It never uses another guest's cwd/global registry. Two
owned slots avoid assuming POSIX replacement rename on Horizon. Load validates
both and selects the highest valid 64-bit generation. Save rescans, writes the
other slot, checks every short write, fsync and close, then reports success. The
previous newest valid slot is not opened/truncated. Generation overflow fails.
Concurrent writers are not supported: production has one frontend owner, and
there is no locking/merge or cross-process security guarantee.

Version 1 has an eight-byte magic, generation, payload length and CRC32 header;
the checksum includes generation/length and payload. The payload contains sort,
count and length-prefixed extension/application strings. Limits: 160 KiB per
slot read, 32 mappings, 1–16 lowercase ASCII extension characters and 4096-byte
absolute guest EXE paths. Duplicate/invalid entries, bad sort/count/length/CRC,
trailing/truncated/oversized records are rejected. Executable existence/mount
availability is rechecked on use rather than dropping remembered removable paths.
CRC is corruption detection, not authentication or Windows Unicode collation.

Missing files use defaults without creating a file. One corrupt slot can recover
from the other; two corrupt slots use defaults and show a warning. Load never
silently deletes corrupt records. A later explicit setting change/save may replace
one invalid owned slot, retaining the other until another update. An unknown
future magic version or read/close error blocks automatic writes, preserving
unrecognized/unreadable records; an available valid older record may still be
used with a warning. No general automatic migration or repair wizard is claimed.

Save failure leaves the current in-memory change usable and displays **saving
not confirmed**, without a durable-success claim or blocking a valid application
launch. A complete record may nevertheless have reached storage before an fsync/
close failure, so a later load can legitimately see old or new validated state.
The previous valid slot remains available at the file-operation level. This is
not a power-loss-proof SD/filesystem transaction: corruption of both slots or
filesystem metadata can still lose preferences. Individual native I/O calls can
block; bounded record size is not a physical latency guarantee.

The settings dialog works idle and alongside the active new-USER guest. Its
actual rectangle is composed even over the native desktop panel. Input follows
the existing native modal/owner rules; EXE reassignment does not launch a second
Process or call guest WndProc. Switch keyboard labels distinguish reassignment
from Run, paths and file names. Save/read warnings remain visible in the shell.

## Evidence and next work

`test_shell_preferences` uses real storage and fault-injected native I/O. It
checks missing/fresh instances, both slots, short reads/writes, partial write,
fsync/close/open/read failure, unchanged previous record, CRC/truncation/oversize/
future-version handling, readonly profile, invalid/duplicate/maximum-size records,
controller-reachable management, valid/invalid reassignment/removal and fresh
shell restoration. The screenshot uses actual native rendering.

`test_shell_documents` now adds a fifth real-x86 session launched through an
association restored into a new DesktopShell. It reads the actual Unicode
(including supplementary characters) document via the full Win32/CRT argument
path. The isolated association-limit test uses its own persistent system root,
so earlier saved mappings are not accidentally counted as an empty profile.
Seven busy-x86 sessions also open native settings, compare the real composed
modal rectangle and save sorting while retaining prior context/copy/input tests.

Run the complete clean MinGW/Mesa suite, strict no-exceptions/no-RTTI checks,
Markdown/diff validation and exact published-SHA six-job CI. Host tests do not
execute actual Switch SD/input/keyboard behavior; local SDK is unavailable and
physical Paint/GPU/fallback/Lite/input/relaunch acceptance remains deferred.
SD troubleshooting stays closed. Next: real TXT read/edit/save, then supported
image viewing and F5 save/find/reopen. [Installer/shortcuts/ZIP/RAR](F_INSTALL_AND_ARCHIVES.md)
remain planned future work; retained native/guest GPU planes and D compatibility
remain explicit separate boundaries.


Native TXT create/read/line-edit/save/Save As is now implemented with bounded
UTF/BOM preservation, retained backups and real conflict/error reporting; see
[F_TEXT_DOCUMENTS.md](F_TEXT_DOCUMENTS.md). Next: supported image viewing and F5.
