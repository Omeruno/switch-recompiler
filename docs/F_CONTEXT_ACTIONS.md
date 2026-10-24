# Functional F: native Explorer context menus

Based on `d8ad9a1`, exact CI 40098135433 passed all six jobs. Uses the same real
[file actions](F_FILE_ACTIONS.md) and [Copy/Paste](F_FILE_PASTE.md); it does not
add a second file-operation implementation or guest Process.

## Object and current-folder actions

A completed right press/release on an Explorer row selects that row without
opening it and shows its object menu. File actions include Open, Choose application
(for document types), Copy, Rename, Delete and Properties as applicable. TXT files additionally offer the native
[Text read/edit tool](F_TEXT_DOCUMENTS.md). Directories
and drives have their supported subsets; folder copying, Cut, archives, installer
and shortcut commands are absent. Readonly or one-active-guest restrictions stay
visible as disabled commands; menus do not bypass ordinary action validation.

A completed right click on an empty part of the listing shows current-folder
Paste, New folder, New TXT and Refresh. Paste operates on the current browser folder,
not on a previously selected child. The Explorer titlebar also has File and
Folder buttons. Y/Tab focus then A/X/Enter reaches both menus without using the
pointer: File targets the selected object, Folder targets the current location.
The Menu/Apps key opens/closes context only while the native shell owns keyboard
focus. It is not globally intercepted from an active guest.

ZR is the secondary mouse button in the existing active new-USER adapter; idle
Switch Explorer now supplies the same press/release behavior. Touch uses the
titlebar buttons as an alternative. Left click still selects an Explorer row;
Open/A/X explicitly opens it. No double-click, desktop background menu, drag/drop,
multiselection or complete Windows input model is claimed here.

## Ownership, focus and target validity

Press/release must complete over the same row/background, with the same captured
parent/row path. A dragged right press does not open another row's menu. Another
right press dismisses an open menu and consumes its release, without reopening
one underneath. B/Escape dismisses without navigating away; an outside primary
click dismisses and consumes both edges without launching or toggling a task.

The menu captures parent/object path and type, freezes paging/selection/Start/
task actions, and revalidates selection/path/native file type before dispatch.
Missing or type-changed objects fail visibly instead of executing a command
against another row. Same-path/same-type replacement by another writer retains
the existing F1 identity/race limitation. Entering rename, delete confirmation,
properties or copy progress first closes the context and uses the ordinary
production action. Delete still defaults to Cancel and Paste never overwrites.

Up/Down and Y/Tab cycle enabled entries, pointer hover moves menu focus and
Enter/A/X chooses the focused action. No disabled item is activated. Context
state is transient and discarded on re-entry/guest return/reconfiguration.
At most eight 30-pixel rows are retained. Geometry is clamped to the actual Explorer
listing area; its shadow also stays inside the existing native composition
rectangle. Rendering and hit-testing use the same menu bounds. There is no
unrendered overlay extending over guest windows.

ShellSession routes native right-button edges into these actions. Modal context
consumes native-owned input, including outside clicks and taskbar/Start attempts;
a guest-owned right/left gesture retains its original ownership even over native
chrome. Context APIs never call guest WndProc from an unbound host stack. Native
keyboard/secondary presses are not relayed a second time to the guest.

## Checks and remaining work

`test_shell_context` drives production controls over temporary real files:
right selection/cancel/drag, menu geometry and hover/focus, actual Copy/Paste/
rename/delete/create/navigation/properties, default deletion cancel, readonly
items, missing/type-changed targets and re-entry. Optional font/screenshot args
produce the actual native menu frame. No mock storage or proprietary fixture.

The seven-session busy-x86 harness now also opens/dismisses native right menus,
checks Menu-key routing and Start/task guards, copies through context, validates
zero leaked guest input and guest-owned right gestures, then retains prior
incremental-copy/heartbeat/window/overflow/quit/restart checks. The full clean
MinGW/Mesa suite, strict no-exceptions/no-RTTI checks, actual screenshots,
Markdown/diff checks and exact published-SHA six-job CI remain required.

[Bounded persisted preferences/associations](F3C_PREFERENCES.md) and management
are now implemented. Next: real TXT/image tools and F5 save/find/reopen. Further input ergonomics and retained
native/guest GPU planes remain open. [Installer/shortcuts/ZIP/RAR](F_INSTALL_AND_ARCHIVES.md)
remain planned future scope. Local Switch SDK is unavailable; CI builds the NRO,
while physical Paint/GPU/fallback/Lite/input/relaunch checks remain deferred.
SD troubleshooting remains closed.
