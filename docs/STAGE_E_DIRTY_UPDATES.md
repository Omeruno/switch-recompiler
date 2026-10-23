# E3 — pixel-verified partial software presentation

> Current launch policy: new UI is the default for every EXE, including Paint;
> **L now requests legacy compatibility**, ZL software output. Older L-for-Paint
> instructions below describe their historical build. Active workspace: [F4a](F4_WORKSPACE.md).


2026-10-22. Software implementation; exact-SHA CI and Switch acceptance are
recorded for the resulting commit, not inferred from host performance.

## Source changes and ownership

Presentation retains one owned 32-bit snapshot. Every Submit examines the
complete source and compares 32x32 tiles against that snapshot. Changed rows
are copied for 32-bit DIBs; other depths are normalized through Surface::Get.
Palette mutations, negative pitch and direct guest writes are therefore seen
at the next submission without assuming all writes pass through GDI setters.
An unchanged submission copies zero pixels and preserves the content serial.
Alpha-only changes in a 32-bit RGBX source can conservatively mark a tile.
No-op detection still scans the source: this is not write-watch tracking.

Each tile carries its latest revision. A synchronous PresentationFrame borrows
the map and owned image only during Render. Clear, resize, attach/detach and
revision wrap invalidate output history. No guest pixel pointer is retained.

## Output buffers, scaling and cursors

OutputBuffer defaults to unpreserved: each render is a conservative full image.
A caller may opt into preserved contents and must change its generation (or
reset the backend) after external writes/reallocation, even at the same address.
The backend keeps at most three histories keyed by identity, generation,
dimensions, stride, viewport and source owner. Eviction forces full output.

Each history compares its own last-seen tile revisions with the current map.
Thus buffers skipped over multiple submissions receive every outstanding
change, without an unbounded damage journal. Dirty tile runs merge horizontally
and vertically when adjacent. More than 128 rectangles or over half the output
area falls back to one full rectangle. First use and geometry changes are full.
Nearest-neighbour damage edges use ceil(source_edge * output / source), matching
the existing floor mapping for output sampling; downsampled empty spans vanish.
Output padding is never written.

The old cursor footprint is stored per output buffer. Changed cursor positions
restore both old/new backgrounds, then overlay the original arrow. Image changes
under a stationary cursor redraw its overlay. An unchanged image/cursor writes
zero pixels. Hide/show, edge clipping and letterbox overlap use the same path.
Output histories advance only after successful rendering; rejected buffers and
backend replacement do not discard pending source revisions.

## Native Switch integration and limits

The native frontend uses libnx framebufferMakeLinear. In the inspected
[libnx implementation](https://github.com/switchbrew/libnx/blob/master/nx/source/display/framebuffer.c),
framebufferBegin returns the same preserved CPU staging buffer in linear mode;
framebufferEnd swizzles its complete image into the dequeued display slot.
Partial writes therefore update that staging buffer, not a guessed display slot.
The generic backend separately tests alternating physical output histories.

Identical content serial/generation/cursor now skips native acquisition, swizzle
and queueing altogether. Keyboard return and any legacy framebuffer drawing
invalidate this optimization and the backend history. Begin/end session also
reset it. Actual changed-frame scanout still performs libnx's full swizzle and
cache flush; E3 does not implement partial GPU upload, GPU composition or GPU
presentation, and does not claim a measured Switch FPS gain.

The renderer v3 exit report retains build/executable and framebuffer timing,
and adds compared normalized bytes, no-op submissions/renders, skipped duplicate
scanouts, tracking bytes and backend cache bytes. Snapshot bytes now count bytes
actually copied; output bytes count actual writes including cursor overlays and
overlapping restore rectangles. They are not SD bandwidth or total GPU traffic.

## Popup exposure

Closing a popup invalidated underlying windows, but continuous modal menu input
could postpone their low-priority WM_PAINT. ClosePopupsFrom now synchronously
updates exposed windows belonging to the current GUI thread before continuing
menu tracking, using their paint procedures rather than stale saved pixels.
Foreign queues keep their ownership and ordinary message priority is unchanged.
The shared UpdateWindowNow preserves existing UpdateWindow cross-thread dispatch;
only menu exposure restricts itself to the current thread.

Paint callbacks can destroy windows or menus. Cleanup revalidates handles,
copies child lists and checks execution-stop state; menu selection revalidates
its owner/menu after exposure painting. Direct desktop background fills now
mark the GDI screen change counter so exposed desktop pixels also get presented.

## Bounded work and validation

- Snapshot cap remains 16 MiB. Native 1280x720 snapshot: 3,686,400 bytes.
- Native tile map: 40x23x8 = 7,360 bytes. Up to three output histories add
  22,080 bytes; column map is 5,120 bytes. At most 130 transient damage Rects
  (2,080 bytes), including cursor restore rectangles. No second full RGB image.
- Larger sources remain constrained by the snapshot cap and 8192-axis limit;
  map size is ceil(width/32) * ceil(height/32), with three bounded histories.
- Host work check: one changed native pixel writes its 32x32 tile, 4,096 bytes,
  versus 3,686,400 for a full output; cursor-only writes <4,096, no-op writes 0.
  These figures describe work in the test, not hardware timing.

test_presentation_dirty compares complete outputs against forced full rendering
under random/direct writes, fractional scaling, up/downsampling, letterbox,
three alternating buffers plus fourth-buffer eviction, missed revisions, cursor
movement/hide, palette changes/all DIB depths, negative pitch, freed sources,
external generation changes, invalid/retried output, backend failure, resize,
clear and reattach. It checks work bounds, padding and complete resource reset.
Existing frozen pre-E golden pixels remain the independent conversion oracle.

test_menu_exposure reproduces an exposed area retaining popup pixels until
paint, then verifies immediate restoration with input still queued, foreign
thread exclusion, callback destruction and desktop change notification.
Paced GUI input, real x86 desktop presentation and sustained-input regressions
now use preserved output through the actual partial backend.

The full local host/MinGW suite passed, including all eight Stage C suites and
open/save dialog regression. Standalone ASan+UBSan passed for dirty/full tests
with detect_leaks=0 (the traced environment does not support LSan inspection).

Switch check on the resulting green ordinary NRO: Paint with L, continuous
stroke with the same input/clocks, cursor at edges, menu bar transitions and
menu cancellation, then save/open and normal exit. Look for stale menu pixels,
cursor trails or missing strokes. Copy switch-recompiler-renderer.txt after
Paint before any other EXE. E3's 50% milestone requires that hardware check;
E4 resource lifecycle and E5 GPU present remain separate, unfinished packages.
