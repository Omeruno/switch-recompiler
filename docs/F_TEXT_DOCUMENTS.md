# Functional F: bounded native TXT documents

Based on `a327117506870a837322de5a0940ee4b52b5bcad`; incoming exact
[CI 40101548903](https://github.com/Omeruno/switch-recompiler/actions/runs/40101548903)
passed six jobs. This is a real native shell document tool, with no additional
Win32 Process, fake Notepad compatibility or private Microsoft binary.

## Reachable workflow

In Explorer's **Folder** menu choose **New TXT**. On a `.txt` file, the **File**
menu/right click offers **Text: read / edit**. Ordinary Open/Choose application
still use the user's explicit EXE association; this package does not replace it.
Both menus are reachable with Y/Tab and A/X/Enter, secondary pointer or ZR.

The modal tool displays five lines. Up/Down selects a line; Left/Right scrolls
the selected line horizontally in 40 UTF-16-unit steps. Clicking a displayed
line selects it. Edit invokes the actual platform keyboard for that line;
Insert adds a following line, Erase removes the selected line. Y/Tab changes
button focus. Empty documents retain one editable empty line.

Save updates the opened path. Save As requires an explicit absolute guest path
and **never replaces** an existing file. New documents use Save As. After a
successful write the real browser listing refreshes and selects the saved file
when it is in the current folder; a failed listing refresh does not turn a
successful write into a retryable write failure.

B/Escape/Close on dirty text offers Save / Save As / Discard / Cancel, with
Cancel selected by default. Cancelling the keyboard leaves the buffer intact.
Ordinary guest exit/abort and frontend re-entry retain this native modal buffer;
root reconfiguration is refused while the document is open. Applet shutdown,
homebrew termination or output failure is not a persisted-draft feature.

## Supported text, limits and input

- At most **64 KiB of actual encoded file bytes**, including BOM. Oversized,
  unreadable, non-regular and invalid input fails without replacing the current
  in-memory document or writing the original.
- Strict UTF-8, with or without BOM, and UTF-16 LE/BE **with BOM**. Overlong UTF-8,
  invalid/truncated sequences, unpaired surrogates, NUL, DEL and C0 controls
  other than tab/CR/LF are refused. Legacy ANSI/code-page guessing and binary
  editing are absent. Supplementary characters survive real saved bytes; the
  existing font may display a missing glyph.
- Existing encoding/BOM and each original CRLF/LF/CR terminator survive editing.
  Newly inserted boundaries use the first observed ending, or CRLF in a new
  document. No automatic normalization or trailing newline is added by Save.
- A line can be edited through the keyboard only if it contains at most **500
  UTF-16 units**. Longer lines remain readable with horizontal scrolling and
  can be removed as a whole; Edit is disabled. The whole file is never loaded
  into the smaller keyboard buffer. Newly returned input is validated again;
  exceeding 500, introducing newlines/control data or exceeding 64 KiB refuses
  the edit without changing the document. There is no silent clipping of data.
- Idle and active Switch paths cap line input at 500 and disable the keyboard's
  newline key. 2048 active / 4097 idle output bytes cover this bound; the model
  validates supplementary pairs independently of the keyboard's length metric.
  Save As paths are also keyboard-bounded; an oversized initial path is left
  empty for explicit entry rather than being truncated.

This is post-line-entry editing rather than a full Notepad text widget: no
selection clipboard, Undo, find/replace, automatic wrap, arbitrary large-file
editing or guest EDIT-control compatibility is claimed. Display slices are
bounded independently of stored data, avoiding quadratic measuring of a long
line. Tabs display as one space; their actual stored bytes are preserved.

## Real storage and checked publication

TextDocument uses the same [F1 mounts/resolver](F1_FILESYSTEM.md) as the browser
and guests. FileCopyIo supplies actual native operations and injectable failures.
Read checks regular-file type/size, bounded data and close. Save stages complete
bytes in a new exclusive `.sr-txt-xxxxxxxx.tmp` sibling, checks short writes,
zero/error writes, file fsync and close. There are at most 64 name attempts and
16 interrupted read/write retries per operation. Mount write intent, existing
file type and owner write mode are checked. No user file is opened with truncate.

For **Save As**, target absence is checked before staging and just before native
rename. Existing destinations, including aliases of the opened document, fail.

For **Save**, actual disk bytes must still match the bytes originally opened or
last saved. They are checked again after staging and before publication. A
changed file produces a visible conflict and keeps the edited buffer for Save
As. The old document is renamed to an unused sibling `.sr-txt-xxxxxxxx.bak`,
then the already closed complete stage is renamed to the now-unused original
name. This works without assuming Horizon supports overwrite rename. **The
backup is deliberately retained on successful Save**, and its filename appears
in the tool; it is in the document's directory. Users can copy/rename/delete
these ordinary backup files with existing Explorer actions. There is no automatic
backup rotation or deletion of old user document bytes.

If publication fails after moving the old file, restoration is attempted only
when the original name is still absent. If restoration fails or another file
occupies that name, the backup remains and its name is reported. Cleanup attempts
only the owned stage; a failed removal reports its remaining filename. The first
failure is retained, the document stays dirty, and a failed write never claims
success. Do not retry a failed save over a missing original without inspecting
its reported backup; Save As remains available.

This is **not a filesystem transaction or power-loss guarantee**. A crash between
the two renames can leave only the `.bak` at its sibling name. Recover explicitly
by finding that file and renaming/copying it; no startup recovery journal is
implemented. Directory fsync, file identity locks and atomic no-replace against
other native writers are absent. Concurrent native changes can race the final
checks, and reads are not a coherent snapshot of a concurrently written file.
Renaming one hard-link path replaces that directory entry, not every alias.
The frontend performs this bounded work synchronously on its owner thread;
64 KiB bounds bytes/work, **not storage latency**. A non-yielding native I/O call
can still pause the frontend. No Switch timing claim is made.

## Validation and remaining work

`test_text_document` uses actual temporary files and native controls: UTF/BOM,
supplementary text, mixed endings, empty/exact/over-limit files, binary refusal,
short I/O, bounded EINTR, ENOSPC/zero writes, read/fsync/close/open/move/publication/
rollback/cleanup faults, readonly paths, external conflicts, saved bytes, dirty
cancel/discard and fresh reopening. Injected rename refuses existing targets to
model Horizon's relevant restriction rather than relying on Linux overwrite.
The optional screenshot is rendered by the production native tool with the real
font, not a separate mock UI.

The five-session actual-x86 document harness now creates and edits/saves its
Unicode document with TextDocument before the real public EXE reads its bytes
through CreateFileW/ReadFile. The seven-session busy-x86 harness also edits,
saves and composes this native tool while the guest heartbeat advances, checking
modal input isolation, actual bytes and return to normal native controls.

Required: complete clean MinGW/Mesa host suite, strict no-exceptions/no-RTTI
checks, actual screenshot, docs/link/diff and all six CI jobs of the exact new
published SHA. Local devkitA64/libnx remains absent; CI compilation is separate
from deferred physical Paint/GPU/fallback/input/relaunch acceptance. SD diagnosis
stays closed. Next: supported image viewing, then F5 save/find/reopen; retained
workspace planes, further input ergonomics and D compatibility remain open.
[Installer/shortcuts/ZIP/RAR](F_INSTALL_AND_ARCHIVES.md) remain planned work.
