# F6b: stateful native shell windows and adaptive layout

Incoming `4a91409fc606c1695cbf88412b711bfddd4dbcd7` passed
[CI40353639236, six jobs](https://github.com/Omeruno/switch-recompiler/actions/runs/40353639236).
Extends [guest gestures](F6A_GUEST_WINDOW_GESTURES.md); these are native host UI
windows, not fabricated guest HWNDs, explorer.exe or new guest Processes.

## Real state and geometry

DesktopShell owns normal/current rectangles and maximized state for its
Programs/Explorer/About panel types and its document/modal view. The UI retains
one foreground native tool group, with a modal TXT/BMP/dialog over its owner;
this is not arbitrary independent concurrent Explorer/document instances.

Caption buttons now execute real minimize, maximize/restore and close actions.
A separate native taskbar button survives minimization (next to the guest task
when an EXE is running). Minimize hides the native group, including its modal
document, without dropping path, selection, document bytes/dirty state, BMP or
maximized/normal rectangles. Restore recreates the same visible geometry and
returns native input ownership; minimizing releases keyboard routing to the
guest. Existing modal policy is retained: opening Start while a minimized modal
tool exists restores that tool rather than opening another native application.
A minimized nonmodal panel leaves desktop/Start available.

Programs/Explorer/About and TXT/BMP windows move by their caption and resize by
all four edges/corners. Rectangles stay within the logical1280x680 work area,
leaving the native taskbar visible. Minimum panel size860x560 and document size
632x350 preserve the existing supported controls; modal confirmation/properties/
settings windows move and close but do not acquire fake resize/maximize buttons.
Maximize uses the work area, and restore preserves the exact normal rectangle.
Pointer release commits, Escape/B/cancel restores the pre-drag rectangle; held
input cannot be replayed after return/launch. Guest activation/return and modal
closure cancel transient native gestures before changing their owner state.

Native close follows existing real policies: panels close their visible view,
a dirty TXT enters its save/discard/cancel prompt, clean TXT/BMP close normally,
and copy cancellation uses the existing file-copy cancellation/cleanup. Data is
never discarded merely because the close glyph was clicked. Closing/minimizing
a native tool does not close/minimize the guest application.

## Layout, drawing and input

`ControlBounds`, `PanelBounds`, `DialogBounds`, `ContentBounds` and document
viewport/row counts derive from owned geometry. Legacy static Bounds/PanelArea/
ImageViewport remain defaults for source compatibility, not the production
geometry provider. The panel's sidebar/toolbar retain readable native sizes;
content, footer and right anchors follow the real edges. Program/explorer page
sizes change with height while preserving the logical selected item. TXT row
count follows document height; BMP fit/pan uses the actual enlarged viewport.

Fonts, icons and text lines are rendered afresh at their normal sizes. There is
no screenshot scaling, host-window screenshot extraction or stretched old frame.
A restore glyph reflects maximized state. Original Luna/icon/wallpaper assets
and the one persistent frontend/output are preserved.

The Switch idle loop and active ShellSession observe a geometry revision even
when the pointer stays over the same drag handle. Thus motion redraws without
waiting for a hover-target change. ShellSession copies and assigns ownership to
exact dynamic panel AND dialog rectangles; a moved document can extend beyond
its owner. Vacated/minimized areas expose actual guest pixels/owners, not a stale
native rectangle. Native press/move/release remains native-owned across exposed
guest areas, and guest drags remain guest-owned across taskbar/native areas.

The canonical CPU composition and conservative ownership snapshots still exist.
This package makes no GPU-GDI/FPS/zero-copy improvement claim; G4b.2 mixed resident
frame ownership and selective admission remain the next system queue.

## Evidence

`test_native_windows` checks all panel types and TXT/BMP tools with real directory
inventory, VFS text bytes and a public generated BMP. Assertions cover all eight
resize directions, cancellation, adaptive pages/hit rectangles, independent normal
geometry, maximize→minimize→restore, task buttons, close release cancellation,
dirty-text refusal/save/discard, image viewport pixels, and launch/return cancelling
an in-progress drag. Optional screenshots use the actual renderer and real fonts:

```sh
build-host/test_native_windows romfs/fonts /tmp/native-window-
```

The real-x86 workspace harness additionally injects physical platform input while
an actual guest EXE is alive: resize/move/Escape/min/max/task restore and a modal
document outside its moved parent. An independent per-pixel overlay oracle compares
all1280x720 canonical pixels with separately drawn native regions and the real
guest screen. Existing retained-layer software and actual production EGL oracles
then verify output/ownership, including native input without guest key/click leaks,
normal exit, host abort, repeated sessions and owned redraw after destruction.

The busy-guest settings test now checks its exact dynamic dialog rectangle plus
an adjacent exposed guest pixel. Its old fixed oversized rectangle had included
six extra pixels of native wallpaper outside the actual window; copying that
region after move/minimize would be incorrect. A negative control restoring only
the old fixed panel-copy rectangle fails the independent full-frame assertion.

Full fresh `COMPOSITION_GL_TEST=1 sh tools/run_host_tests.sh` passes the final
runtime/harness sources, including earlier GDI/GPU/lifecycle/file/dialog/C/E/F
gates. Strict O2 changed modules/native harness pass. Focused ASan/UBSan instruments
DesktopShell/layout/theme/ShellSession and native/workspace harnesses; remaining
runtime objects are ordinary builds, leak scanning disabled, known native-fiber
makecontext/swapcontext warning retained. Both targeted tests pass. Native
Programs resized / editor maximized / BMP screenshots were visually inspected.

CI publishes `stage-native-window-actions` with native and actual-x86 software/EGL
workspace logs, public workspace EXE and this contract. Verify all six jobs for
the exact published SHA. Local devkitA64/libnx remains unavailable; CI builds NRO.
Physical Switch/Paint/GPU/fallback/input/relaunch acceptance remains deferred.

## Remaining scope

This is bounded software window behavior, not a full XP shell. One native tool
group and one active guest Process remain; there is no arbitrary native-window
Z-order, multiple simultaneous native document instances or multiple guest EXEs.
Native keyboard/system-menu move-size selection, caption double-click/maximized
drag-to-restore, richer text editing, broader image formats and other shell/API
features remain separate work. Native placement is retained in the live shell,
not newly persisted across process restarts. Resume G4b.2 owned mixed CPU/resident
frames, live-window/shell integration and raw-host/fault audit before ordinary
GDI GPU activation. Installer/archive and PC-game compatibility queues remain.
