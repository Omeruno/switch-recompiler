# Functional F: native single-file Copy/Paste

Based on `be95ecb`, exact CI 40092668937 passed all six jobs. This connects
[checked copy](F_FILE_COPY.md) to the native Explorer. Context menus, directory
copy and the persistent settings/document queue remain separate work.

## User flow

Select a regular file and choose Copy. The shell remembers its full guest path
and name, not its file bytes, an open descriptor or a guest pointer. Browse to a
writable real folder, choose Paste, and enter the destination name (initially
the source name). Accept starts copying; cancelling name entry has no effect.
This shell file clipboard survives ordinary frontend guest launches/returns but
is not persisted after homebrew shutdown and is distinct from guest text clipboard.
A changed/disappeared source is revalidated when Paste starts.

The progress dialog displays the destination, source and actual bytes written
to the owned staging file. There is no estimated percentage or speed claim. Copy
is disabled for folders/drives, Paste at virtual My Computer and readonly R.
Readonly source files may be copied. Existing destinations are never replaced:
choose a different name or receive the actual collision error. Invalid names,
self-copy, missing source/parent, readonly media and I/O errors stay visible.
Successful publication refreshes/selects the destination; successful copy with
failed refresh is distinct from mutation failure. Clipboard state remains for
another Paste. The original file is unchanged by copying.

Copy/Paste are reachable by pointer and controller focus (Y/Tab); B/Escape or the
native Cancel button cancels an active copy. Like the earlier file dialogs, it
freezes path/selection/Start/task commands and consumes native-owned input rather
than delivering clicks/keys to the active guest. EndGuest, frontend Enter,
reconfiguration, selection exit/display failure and object destruction cancel
unfinished jobs and attempt owned-stage cleanup. Already published copies remain.
There is no recursive directory copy, Cut, overwrite confirmation, automatic copy
name, .lnk creation, Windows file clipboard or context menu in this package.

## One engine, resumable ownership

FileCopyJob owns a mount/cwd metadata snapshot, native source/destination names,
one 8192-byte buffer and its descriptors/stage. It owns no guest memory, borrowed
VFS/case-budget pointer or thread. Start resolves/validates paths and obtains the
exclusive stage. Each Step performs one read/write or finalization phase; EINTR
returns to the owner before retry. Short writes resume the remaining buffer.
Byte progress counts actual writes, not reads or requested bytes.

Flush, output-close, input-close and publication are separate phases. The final
destination check and rename are performed together without yielding, so another
guest file operation creating the name between earlier chunks is rejected rather
than overwritten. Cancel/destruction closes descriptors and unlinks only the
owned stage. Errors retain the original errno. The prior checked-copy failure,
cleanup-residue, device replacement and durability limits remain.

The native shell advances at most eight phases per service call. Idle Switch
selection services it after input; ShellSession services it after native event
routing, including independent between-quantum polling for a busy guest. An
active guest can execute again between copy chunks. No extra worker or second
Process is introduced. KERNEL32 CopyFileA/W and synchronous browser CopyHere drive
the same engine to completion, retaining their synchronous API contract.

This bounds work count per service, **not wall-clock I/O duration**. Start's case
lookup/exclusive-name attempts, an individual read/write/fsync/close/rename and
final directory refresh can still take time; no physical 8 ms latency guarantee
is claimed. Guest/native writers may modify source data or directories between
chunks; this is not a snapshot, file-sharing lock or security boundary. There is
no atomic no-replace versus concurrent native writers, directory-fsync guarantee
or automatic recovery of staging files after crash/cleanup failure.

## Verification

- Existing real-file/fault-injected copy tests now run against the shared resumable
  engine through synchronous CopyFile, preserving exact errors and data checks.
- Direct job tests cancel/destruct at different phases, check bounded byte
  increments and no destination before publication, delayed name collision and
  fresh restart after failure. All cancellation paths must release descriptors.
- Native shell tests drive real Copy/Paste/name cancellation, readonly source,
  actual destination bytes/selection, name collision/invalid/missing source,
  modal freeze, large-copy cancellation, guest return and frontend re-entry.
  Actual Explorer/progress screenshots can be emitted with the existing arguments.
- The public seven-session x86 busy-shell harness copies a multi-chunk file to
  My Documents while the real guest spin loop continues between service calls,
  compares every byte, cancels another copy and verifies zero leaked guest input.
  Earlier owner-window/overflow/quit/normal/abort/restart checks remain.

Run the complete clean MinGW/Mesa host suite, strict no-exceptions/no-RTTI checks,
Markdown/diff validation and exact published-SHA six-job CI. Local devkitA64/libnx
remains unavailable; host tests do not execute the actual Switch input/SD device.
Physical Paint/GPU/fallback/Lite/input/relaunch acceptance stays deferred; SD
troubleshooting stays closed. Next: coherent context/right-click/controller
interaction, then preferences, TXT/images and F5. The [installer/archive plan](F_INSTALL_AND_ARCHIVES.md)
remains recorded as future functional work.
