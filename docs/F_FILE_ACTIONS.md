# Functional F: native Explorer file actions

Based on `b46b378` (CI 40086771006: six successful jobs). Builds on the shared
[real file provider](F_FILE_OPERATIONS.md), preserving F1 mounts and bounded
browser listings. This is functional native UI, not a Windows Explorer binary.

## Available actions

The Explorer sidebar exposes New folder, Rename, Delete and Properties. They
operate on actual C/S/R paths using the shared browser/provider methods. Folder
creation and rename invoke the existing native text-entry adapter; enter one
name without quotes/path separators. Cancellation does not mutate storage.
Invalid names, collisions, missing parents, readonly media, wrong types and
nonempty directories retain actual errors. No copy, recursive delete, recycle
bin, multiselection or right-click context menu is advertised yet.

Mutations are disabled at virtual My Computer/drive entries and on readonly R.
Properties can inspect a selected file, folder or backing drive-root directory.
It shows actual regular-file bytes, file/directory kind, native access and
modification timestamps in UTC and mount write policy. Directory content size
and Windows creation time are explicitly unavailable. Mount policy is not a
claim that every descendant is writable. Properties are snapshots queried anew
on each open, not a live refresh or recursive capacity/size scan.

Delete opens a modal naming the captured object and stating that deletion is
permanent, without recovery. **Cancel is selected by default.** Enter/A/X on that
focus cancels. B/Escape cancels; Y/Tab or Up/Down changes focus to Delete for an
explicit confirmation. Files and empty directories are supported; a nonempty
folder fails visibly. Name entry and dialogs freeze selection, paging, path
navigation, Start and background/task actions, so they cannot retarget a pending
command. Pointer press/release must stay on the same control to activate it.
Controller-only focus cycling reaches every enabled sidebar action.

The dialog revalidates the captured path and type before deletion; disappeared
objects and changed types do not delete another selected row. The underlying
F1 trusted-native-writer race boundary still applies: identity protection against
another writer replacing an object at the same path with the same type is not
implemented. Closed/re-entered frontend/guest scopes discard transient commands.
No guest pointer or Process is retained by the dialog.

Mutation results distinguish failure, applied, and applied-with-refresh-error.
The latter reports that the operation already happened and asks to refresh the
view, rather than encouraging a second mutation. Successful rename/create selects
the new object where present in the bounded listing. History/associations do not
follow renames automatically. Names and long paths are visually clipped to fit;
operation paths retain their complete original values.

## Active guest and Switch adapters

Actions work with the idle shell and beside the one active new-USER guest.
ShellSession consumes modal input, including clicks over the guest/taskbar and
Windows-key attempts to open Start. Task minimize/restore is blocked during the
modal; guest-owned press/release gestures retain the existing ownership rule.
The modal fits within the existing Explorer composition rectangle. It does not
create a second guest Process or call guest window procedures.

Idle Switch keyboard labels distinguish folder/name entry from path/Run input.
The existing active text-input callback retains startup/return input quarantine;
its conversion now uses the shared UTF-8/UTF-16 path codec, preserving supplementary
Unicode characters. Output is invalidated/restored after both accept and cancel
of that applet. These Switch-only paths require CI compilation and later actual
console checks; host tests cannot execute libnx keyboard or physical scanout.

## Reproduction and remaining work

`test_shell_file_actions` drives production DesktopShell controls on real temporary
C/S/R trees. It checks controller focus, cancel/default-cancel, collisions,
Unicode names, storage bytes, fresh file size/property snapshots, nonempty/readonly
errors, frozen selection/background actions, changed/missing targets and discarded
pending input on re-entry. Its optional font/screenshot arguments render actual
Explorer/properties/delete frames; no mock drawing or bitmap-font acceptance.

The existing public x86 busy-shell harness additionally creates/renames/inspects/
cancels/confirms deletion through real native input/text callbacks while the
application executes without USER pumping. It checks actual native storage,
zero leaked guest input/callbacks, modal Start/task guards, normal/abort/restart
and unchanged later guest event counts. Run the complete clean
`COMPOSITION_GL_TEST=1 sh tools/run_host_tests.sh`, strict no-exceptions/no-RTTI
checks and exact published-SHA six-job CI.

The [checked-copy provider](F_FILE_COPY.md) now implements explicit I/O/staging
semantics. Next: native Copy/Paste and coherent
right-click/context actions and further controller interaction, persisted
preferences/associations, TXT/images and F5 save/find/reopen. Separate retained
native/guest GPU planes, D compatibility and measured G remain open. No complete
Windows environment, local Switch SDK or physical Paint/GPU/fallback/input/
relaunch acceptance is claimed. SD troubleshooting stays closed.
