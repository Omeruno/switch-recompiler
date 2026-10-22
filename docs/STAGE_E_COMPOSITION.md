# E6: window composition baseline and exposure repair

## Delivered boundary

The new unattended `gui_composition.exe` exercises actual x86 USER32/GDI calls.
Its 28 stable scenes are inspected at both 1280x720 (Switch Lite desktop) and
800x600 (scaled/letterboxed). An independent host rectangle oracle compares
**every CPU screen pixel**, then every output pixel and protected row padding.
Three retained software output buffers rotate through the scenes, so a buffer
which missed two intervening frames must still receive the right updates.

The same live guest scenes run through the production OpenGlPresentation with
EGL/Mesa. GL readback is compared against the already validated software image,
including a backend reset while the owned popup/other windows remain alive.
This is not a substitute renderer or a test of only manually supplied screenshots.
CI requires both GL sizes; the existing format/palette/cursor/fault tests remain.

The probe also runs independently under the ordinary NRO and writes
`stage-e-composition.txt`. The report starts RUNNING; only the final
`PASS: ALL 28 SCENES` is a pass. No button presses are needed. CI publishes the
redistributable fixture as `stage-e-composition-exe`; it contains no Paint binary.

## Coverage

| Scenes | Contract |
| --- | --- |
| 1–4 | Full desktop, opaque overlap, repainting a lower window cannot overwrite the upper window |
| 5–10 | Move, shrink, hide/show, explicit raise in both directions, exposed old footprints |
| 11–13 | Child clipping at all four parent edges, negative child position, child destruction |
| 14–15 | Owned popup display/destruction and owner exposure |
| 16–17 | Destination-dependent screen ROP, screen-to-memory readback, inverse ROP restores exact pixels |
| 18–19 | Raw guest DIB writes and later mutation, blit into an existing window |
| 20–21 | Window destruction preserves remaining windows and restores background |
| 22–24 | Foreground activation changes z-order and the actual visible image |
| 25–28 | Overlapping child siblings under WS_CLIPCHILDREN, move/hide/destroy exposure |

Paint callbacks fill only their invalid region through ordinary BeginPaint.
The inspector never changes window state to manufacture a passing screenshot.
The fixture updates pending paint regions rather than invalidating the whole
desktop between scenes. NOACTIVATE creation isolates geometry from automatic
activation; scenes 22–24 explicitly test foreground activation separately.

## Two reproduced production defects

1. SetActive inserted the new foreground window at the front of the window tree
   and refreshed DC clipping, but did not invalidate the affected screen area.
   The new order was visible to input/DC queries while old overlapping pixels
   remained on screen. The unmodified implementation fails scene 23 at the first
   overlapping pixel. The repair invalidates the raised window's footprint only
   if its actual z-order changed, before activation callbacks can request paint.
   Damage collection introduces no additional guest callbacks.
2. Moving/hiding a child invalidated its old footprint in the parent with child
   propagation disabled. With WS_CLIPCHILDREN the parent could not repaint an
   exposed sibling, and that sibling never received its own invalid region.
   After fixing activation alone the fixture fails scene 26. Propagating old-area
   invalidation to children repairs it, matching destruction's existing path.

References: Microsoft Learn [SetForegroundWindow](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setforegroundwindow),
[Window Regions](https://learn.microsoft.com/en-us/windows/win32/gdi/window-regions)
and [Window Features](https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features).

## What is still required for E6

This checkpoint fixes the CPU reference that GPU composition must match. It does
**not** turn State.screen into independent retained window surfaces. E6's 10%
remains open, and no new FPS or Switch performance result is claimed.

The next implementation boundary is Gdi::Target/WindowDcInfo: separate window
storage and its origin from the current composed screen, while preserving
BeginPaint/update/ancestor/sibling clipping and a correct CPU destination for ROP.
Direct screen DC writes and screen readback need explicit reconciliation before
using retained layers. Only then can independent GPU textures consume those
window-owned pixels. Copying completed screen rectangles into textures is not
accepted as per-window composition. The 28-scene oracle must continue to pass
through both output backends during that transition.

Reproduce locally:

```sh
COMPOSITION_GL_TEST=1 sh tools/run_host_tests.sh
```

Requires MinGW x86, EGL/OpenGL development libraries and a host Mesa driver.
Without COMPOSITION_GL_TEST the CPU oracle still runs; CI always enables GL.

Local validation note: the first full suite failed the existing invalid-image
rollback fixture; its supposedly malformed child DLL was observed to contain a
valid MZ image. Restoring the deliberate corruption made both failing rollback
tests pass. An independent complete fixture rebuild produced the correct
35-byte invalid image. No loader/runtime fix or cause of the unexpected fixture
contents is claimed. The complete second suite with freshly generated fixtures
passes, including both real GL composition runs, Stage C and dialog/input tests.
