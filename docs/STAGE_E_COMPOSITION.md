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

## Retained raster and GPU integration

The follow-on implementation connects independent window storage to actual GDI
rasterization. WindowDcInfo supplies an optional SurfaceWriteCapture for a window
DC. Each drawn pixel writes through to that window's own buffer and the canonical
CPU screen; no finished screen rectangle populates a window image. Coordinates,
BeginPaint, ancestor/sibling clipping, pattern origin and CPU ROP destination
reads retain the existing path. Hidden/occluded pixels are not invented: classic
repaint/invalidation semantics still determine visible writes.

A screen ownership plane records the last window which actually drew each pixel.
Z-order and clipping are resolved by USER32's existing DC provider; ownership
preserves that visible image, including intervals before exposure is repainted.
Screen DC/desktop writes select the canonical fallback plane. Submission checks
retained pixels against canonical RGB: unobserved direct CPU writes or invalid
ownership reconcile to fallback rather than showing stale window pixels.
Write-through submissions skip per-pixel checks for tiles whose canonical
revisions and ownership bytes are unchanged. Arbitrary scene views always get
full reconciliation.

Presentation owns copies of window pixels and ownership after Submit. Cursor
redraw, backend reset and host/guest exit never retain Window*, DC*, guest memory
or borrowed raster buffers. Destroy/eviction/move/resize invalidate source
ownership before freeing storage; key reuse receives fresh tokens/generations.
Renderer cleanup follows guest continuation draining as before.

OpenGL uploads independent window textures and an R32UI ownership texture. The
base pass explicitly masks window-owned pixels black; window passes must supply
them from their own textures. A poisoned-base GL regression proves the flattened
image cannot accidentally satisfy the test. Window scissor bounds preserve exact
nearest-neighbour scaling/letterbox at negative origins. The independent cursor
remains shared with the reference. Each texture reuses the bounded tile/coalescing
upload plan; cursor-only frames upload zero pixels. The CPU snapshot remains
available for Win32 readback and software fallback.

Failures are scoped: window allocation/budget eviction selects canonical pixels
for that window; layer-snapshot allocation failure displays the complete flat CPU
frame; GL allocation/upload/draw/swap failure uses existing software fallback.
New UI enables capture after desktop initialization only when GPU output is
active; software/legacy output avoids capture work. Renderer report v5 records
capture/layer memory, submissions/fallbacks, reconciliation, layer draws and layer
snapshot CPU time.

## Bounds and validation

There are at most 48 raster layers and 48 renderer/GPU layer slots. Aggregate
window pixels are limited to 16 MiB separately in the raster store and owned
snapshot. Each ownership plane is bounded by the source image (16 MiB;
3,686,400 bytes at 1280x720). Canonical CPU snapshot and screen texture remain.
At native Lite size, maximum accounted screen/snapshot/window/ownership CPU pixels
total about 46.1 MiB including State.screen, plus tracking; GPU logical pixel
textures total about 23.1 MiB. Transactional snapshot resize may temporarily
retain another 16 MiB. Guest DIB/GDI objects and EGL/driver/window buffers are
additional resources, not claimed as part of those bounds.

The 28 live x86 scenes now use capture and independent GPU composition at both
sizes. Dedicated tests cover destroyed-source cursor replay, key/geometry reuse,
unobserved screen writes, duplicate/invalid scene fallback, initial required
allocation failures, optional tracking failure, bounded eviction and release.
Actual EGL/GL tests cover poisoned base pixels, overlap ownership, negative
origins, cursor-only zero uploads, destroyed input storage, GL error/retry/reset.

The complete local suite and actual GL tests pass. ASan+UBSan pass the allocation/
lifetime/budget tests with leak detection disabled: LeakSanitizer cannot inspect
tasks in this execution environment. The injected allocator also asserts zero
live allocations after every fault case. No loader change belongs to this block.
Hardware acceptance is deferred per the user's request; E6's 10% is not awarded
on host tests alone, and no Switch FPS/latency improvement is claimed. E7/E8 can
now measure/optimize the real layer path; the canonical base texture still
receives its normal dirty updates as a conservative fallback.

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
