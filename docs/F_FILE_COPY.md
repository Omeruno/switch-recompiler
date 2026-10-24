# Functional F: checked regular-file byte copy

Based on `6ac9aa0`, exact CI 40087919426 passed all six jobs. The shared provider
now implements checked CopyFile; KERNEL32 CopyFileA/W use it, and ShellBrowser
CopyHere exposes a no-overwrite model operation. Native Copy/Paste controls and
clipboard/context menus are **not yet exposed** by DesktopShell.

## Data and error contract

VFS uses the existing F1 resolver/mount/case/Unicode/link policy, resolves source
for reading and destination for writing, and rejects non-regular files, mounted
roots/ancestors and copying onto the source through case/C/S aliases. Native
nonzero inode/device identity additionally detects host hard links. Existing
readonly destination mode bits are rejected; readonly sources may be copied.
No recursive directory copy or metadata/Windows attribute/ACL/stream/timestamp
preservation is claimed: this package copies regular-file bytes.

A new exclusive sibling `.sr-copy-xxxxxxxx.tmp` holds the data. There are at most
64 exclusive-name attempts and one 8192-byte buffer. The requested destination
is never opened/truncated. Source reads and short destination writes are checked;
EINTR is retried for data I/O, zero writes fail. File fsync and both descriptor
closes must succeed before publication. There is no stdio user buffer to flush.
The first actual error survives cleanup. A close consumes the descriptor even
if it reports failure; it is never retried against a possibly reused descriptor.

The existing destination policy is checked before copying and again before native
rename. Fail-if-exists reports an existing destination without replacement.
Explicit replacement is attempted through native rename only after complete
staging and close. A failed copy does not delete/truncate the old destination.
If the device refuses replacement rename, the operation fails visibly and keeps
the old file; there is no delete-old-then-rename workaround. In particular,
Switch fsdev delegates to Horizon rename and does not implement a POSIX overwrite
fallback, so existing-destination replacement is a remaining device compatibility
boundary. New-file copy and host POSIX replacement are separately testable.

On failure the operation attempts to unlink only its owned staging file, never
the requested destination/source. If cleanup itself fails a staging file may
remain; the original error is retained. Crash/interruption may also leave that
file. Successful file fsync is not a claim of power-loss-proof publication:
there is no directory-fsync/SD transactional durability guarantee.

F1's trusted-native-writer race boundary remains. Check-then-rename is not atomic
no-replace against other native writers; source bytes are not a coherent snapshot
if another writer changes them. Calls are synchronous and do not yield or offer
progress/cancellation yet; an 8 KiB buffer does not bound total copy latency.
Shell CopyHere uses the same bounded case lookup and three-way mutation/refresh
result as the other browser actions, and never overwrites by default.

## Checks

`test_file_copy` uses real temporary overlapping mounts and binary payloads larger
than the buffer. It checks empty/Unicode/case/readonly/unmounted/missing/linked/
wrong-type paths, C/S/hard-link self-copy, non-overwrite/collision, actual byte
replacement, and browser selection/errors. Injectable FileCopyIo delegates to
real native files, then injects short I/O, EINTR, read/write/zero-write/fsync/
both-close/publication/open/create/collision failures, late destination creation
and cleanup failure. Tests verify exact errors, descriptor accounting, preserved
source/old destination, no unintended destination and expected staging cleanup.

The existing four-session public x86 filesystem fixture exercises real A/W
CopyFile imports, Unicode destination, ERROR_FILE_EXISTS, self-copy/read-only
refusal and exact copied bytes. Run the complete clean host/MinGW/Mesa suite,
strict no-exceptions/no-RTTI checks and the exact published-SHA six-job CI.
Local Switch SDK remains absent; CI compile is not physical SD/fsync/rename
acceptance. Hardware Paint/GPU/fallback/input/relaunch checks remain deferred;
SD diagnosis stays closed.

Next: native Copy/Paste destination/clipboard and coherent context/controller
input; then preferences/TXT/images/F5. [Installer/archive import](F_INSTALL_AND_ARCHIVES.md)
is a separate planned functional extension with explicit compatibility gates.
