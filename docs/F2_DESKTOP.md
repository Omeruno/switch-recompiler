# F2: native idle desktop and theme

> Current launch policy: new UI is the default for every EXE, including Paint;
> **L now requests legacy compatibility**, ZL software output. Older L-for-Paint
> instructions below describe their historical build. Active workspace: [F4a](F4_WORKSPACE.md).


Implemented F2 baseline, based on d6a6351. Subsequent [F3a](F3_EXPLORER.md)
enables real My Computer/Documents and path-only Run; the F2 behavior below is
its original delivery scope. Physical Switch acceptance stays
on the deferred checklist in [STAGE_E_PERFORMANCE.md](STAGE_E_PERFORMANCE.md).

> F4b.2 supersedes the original per-launch device/worker lifetime below;
> native selection and guests now share a persistent owner/output. See [contract](F4B_OUTPUT_LIFETIME.md).

## Delivered behavior

`ui::DesktopShell` owns its bounded catalog copy, selected EXE, active panel,
menu/focus/hover/press state and status independently of any guest Process/USER
windows. It draws a blue native desktop/taskbar, green Start, real device-time
HH:MM clock, licensed icons/fonts, Programs and About panels. Start Programs,
About and Exit are actual actions. My Computer, My Documents and Run are visibly
disabled until F3; the current Programs panel is a bounded executable selector,
not a directory explorer. No second guest or Windows shutdown is represented.

The Switch launcher uses the desktop when selection is needed, rescans the
existing ROMFS/SD/drive_c/Programs catalog on entry and preserves the selected
EXE/panel through ordinary return and errors when that EXE is still in the list.
Existing initial sole-EXE autolaunch is preserved; its return always enters the
shell. A console picker remains available on shell output/allocation failure.
Nothing automatically moves existing applications/documents or rewrites F1 paths.

Controls: stick moves the cursor, A clicks (or accepts controller focus), X opens
focused items, R toggles Start, Up/Down selects, Left/Right pages the EXE list,
B closes menu/panel then exits from desktop, PLUS exits the idle shell. Touch
selects a row; Open explicitly launches it. Press/release must target the same
control. Dismissing Start restores the previous panel/row focus. Entry quarantines held buttons/touch; controller navigation cancels and
quarantines an in-progress pointer gesture. L/ZL held on guest launch retain the
existing UI-layer/software-output overrides.

The logical canvas is 1280x720; `FitViewport` is shared by presentation and touch.
Idle output uses its own SoftwarePresentation/Presentation on the main thread,
without guest renderer metrics or USER state. Dirty UI/clock changes submit a
new frame; cursor motion alone redraws the retained snapshot. Frames/buffers are
released and framebuffer output closes before the guest's existing output/worker
starts. Shutdown is sticky; a closed applet does not restore a console. F4 still
owns active-guest taskbar, minimize/restore, compositor/input ownership: the
current guest worker join remains blocking.

## Artwork and reproducible checks

Three Tango icons are imported from pinned public-domain sources. Source PNGs,
COPYING declaration, URLs and original/decoded/generated SHA-256 values are in
[romfs/shell](../romfs/shell/README.md). Native conversion uses only Python stdlib
at development time; Switch has no new decoder or image-library dependency.
The blue/green theme is original native drawing; no XP.css code/artwork or
Microsoft logos/wallpaper/binaries were imported. Existing Liberation/OFL fonts
are used. Unicode paths are retained; complete CJK font coverage is not claimed.

`test_desktop_shell` checks native rendering with the shipped font, pressed and
cancelled Start, disabled commands, Start/About/exit actions, bounded real-file
catalog paging/Unicode paths, explicit launch, selected EXE/panel restoration,
controller navigation, initial held input quarantine, and output/touch alignment
at 1280x720, 800x600 and 640x360 with pitch/letterbox guards. Native strict build:

```sh
g++ -std=c++17 -Wall -Wextra -Werror -fno-exceptions -fno-rtti \
  -ffunction-sections -fdata-sections -Wl,--gc-sections -I. \
  tests/test_desktop_shell.cpp source/ui/desktop_shell.cpp source/ui/font.cpp \
  source/ui/shell_browser.cpp source/runtime/files.cpp \
  source/ui/executable_catalog.cpp source/ui/presentation.cpp \
  source/ui/presentation_layers.cpp source/ui/window_layers.cpp \
  -o /tmp/test_desktop_shell
/tmp/test_desktop_shell romfs/fonts /tmp/sr-shell
python3 tools/generate_shell_icons.py --check
```

The optional second argument writes actual native PPM screenshots with fixed
clock text: desktop, Start, About and Programs. The last panel includes temporary
fixture paths; these are not byte-identical across separate executions. The full
`COMPOSITION_GL_TEST=1 sh tools/run_host_tests.sh` includes the new harness and
artwork check alongside existing guest/GL/session regressions. Host checks do
not execute libnx framebuffer/input/service transitions; exact-SHA Switch CI
compilation and later physical checks remain distinct gates.

## Next package

F3a now implements the real F1-backed browser and shared path-only launch. Next:
F3b document arguments/associations, supported Start/settings and tree refinements. Afterwards F4 active-guest integration and F5 the public
save/find/reopen scenario. See [F_SHELL_PLAN.md](F_SHELL_PLAN.md).
