# E4–E7 large work block: implementation and acceptance gates

The user requested one work block covering E4–E7 (50% → 90% of Stage E).
This document distinguishes delivered code from acceptance; hardware percentage
is not inferred from host output, and E6 window layers are not simulated by
renaming the existing flattened screen texture.

## E4: recoverable bounded renderer storage

Presentation-owned allocations now use PresentationBuffer, with explicit failure
returns under -fno-exceptions. The change is local to presentation: guest GDI
OwnedSurface and other runtime allocation semantics are unchanged. Snapshot pixels
and tile revisions are prepared transactionally. Failure of either allocation
leaves the preceding complete CPU frame, source dimensions, revisions and resource
generation intact. Both temporary allocations are released on failure.

Software backend column-map allocation can fail and be retried. Optional dirty
history allocation failure still produces correct full output, then falls back
to full conversion on the next render. Damage storage is fixed size on the stack;
it cannot allocate per frame. Explicit Detach clears snapshot/history/backend
storage. The shared session survives guest continuation draining, then restores
all three original platform callbacks and frees presentation resources.

Limits: one normalized CPU snapshot ≤16 MiB; resize can temporarily retain the
old and new snapshots (≤32 MiB plus tracking). Source/output dimensions ≤8192;
software tracks at most three outputs, each with at most 65,536 tile revisions
(512 KiB conservative upper bound), plus one ≤32 KiB column map and 131 fixed
Rect slots. GPU owns one screen texture ≤16 MiB, an 11×19 RGBA cursor texture,
one revision map and one shader/VAO set. EGL/Mesa window buffers/driver allocations
are additional platform resources; these numbers do not claim a total driver
memory bound. No growing command/upload queue is owned by this implementation.

Fault injection exercises initial allocation failure, both resize allocation
failures, required column-map failure, optional history failure, repeated resize
under faults, retained-frame replay and zero live renderer allocations on detach.

## E5: actual GPU output

Official API references checked:

- switchbrew/switch-examples, graphics/opengl/README.md: switch-mesa/switch-glad
  dependencies and the prohibition on mixing libnx console output with GPU.
- graphics/opengl/simple_triangle/source/main.cpp and Makefile: EGL window/context
  creation, glad loader and the supported native link libraries.
- switchbrew/libnx, nx/include/switch/display/framebuffer.h: framebufferEnd queues
  a frame; it is not an acquired-buffer cancellation operation.

OpenGlPresentation draws the complete CPU GDI result as a GL texture. It uses
texelFetch and integer coordinates to match reference nearest-neighbour scaling
exactly, including asymmetric letterboxing. DIB byte order is uploaded as BGRA;
shader output has opaque alpha. Dither, blending and framebuffer sRGB are disabled.
The existing cursor definition is shared, uploaded once, and composited by the
shader independently of the desktop texture. Cursor-only frames upload zero
screen pixels. ROP, clipping, palettes, raw DIB writes and readback stay in CPU GDI.

The EGL context is created on the application thread, explicitly released there,
then acquired on the guest host thread. GL resources are constructed/used/freed
on that thread. EGL and libnx Framebuffer do not coexist as window owners.
Failure to initialize EGL/GL, upload, draw or swap tears down the GPU output and
replays the retained CPU snapshot through software; subsequent input uses the
same viewport. A failed software framebuffer allocation can still prevent output;
this is recorded rather than claimed to be recoverable without memory.

New UI defaults to GPU. Paint still uses L to select the new UI. Holding ZL at
launch selects software output for comparison/recovery. Legacy UI remains on the
software framebuffer. The renderer v4 report identifies active backend, fallback
reason/count, texture upload bytes/calls and GPU swap totals/maxima; CPU framebuffer
waits remain separate. No Switch FPS/latency improvement is asserted yet.

## E7 work already connected to the GPU path

One current screen texture is reused across frames. Tile revisions coalesce
horizontal and vertically adjacent runs; a bounded 129-slot plan falls back to one
full upload above 128 regions or half the source area. A metadata-allocation failure
uses full upload, not a broken or unbounded cache. Resource identity, generation,
size and revision scope prevent stale texture reuse. Reset/detach releases tracking;
Shutdown destroys GL objects. Uniform locations are cached only at shader creation.

No glyph/bitmap cache has been introduced: rasterization remains in CPU GDI and
this pass has no measured glyph-upload bottleneck to justify another cache.
Host tests check actual upload work and exact pixels, rather than claiming the
GL upload counters are Switch frame-time measurements.

## E6 initial constraint and connected implementation

Current Win32 windows paint into State.screen; GetDC(0), destination ROPs and
screen-to-bitmap copies read that shared CPU image. The GPU path preserves those
semantics by using the completed image. This is screen+cursor GPU composition,
not independent per-window GPU composition at that initial checkpoint.

The subsequent E6 block records individual GDI writes into bounded window-owned
surfaces, reconciles screen-DC writes/readback with an ownership plane, and feeds
independent window textures to the GPU. The base pass masks owned pixels, and a
poisoned-base regression proves that layer textures supply them. This extends
the original memory bounds with window/ownership storage; current bounds,
fallback and tests: [STAGE_E_COMPOSITION.md](STAGE_E_COMPOSITION.md).
The 28 live x86 scenes pass both backends. E6's 10% still requires the deferred
combined hardware check; no measured Switch benefit is claimed.

## Validation and one Switch check

The actual GL implementation runs with EGL/Mesa surfaceless on the host. It is
compared pixel-for-pixel with SoftwarePresentation over 1/4/8/16/24/32-bit source
formats, palette changes, negative pitch, source edits, scaling in both directions,
1280×720 native output, letterbox, cursor appearance/movement/disappearance and
resource reset. A real invalid GL operation verifies frame rejection/full retry;
backend rebind verifies software fallback retains identical CPU snapshot pixels.
Dirty output and allocation-lifecycle tests additionally run under ASan+UBSan.
A dedicated CI job runs the GL pixel test, and native CI installs/links the actual
Switch Mesa libraries. Full host/guest/Stage C/open-save tests remain required.

Hardware gate after native CI: one new-UI Paint session with GPU (L), checking
startup/menu/toolbar, continuous touch/stick drawing, open/save and normal exit;
preserve switch-recompiler-renderer.txt. One comparison session with L+ZL should
identify software in the report and retain geometry/input behavior. If GPU startup
fails automatically, preserve the fallback reason in that report. This is a single
combined checkpoint; E5 remains pending actual Switch output until it arrives.


Validation follow-up: optimized actual EGL/GL tests and ASan+UBSan dirty/lifecycle
tests pass locally. The first full local suite reported failures in the unchanged
loader rollback tests; ten targeted repeats with the same generated fixtures and
the complete second suite passed without a loader-code change. Retain that fact
when interpreting validation; no loader fix or root cause is claimed. The native
CI dependency step was corrected to dkp-pacman, matching the official container.
GL tests additionally enforce 4 KiB single-tile upload, zero cursor-only uploads,
one full upload for widespread damage, recovery without optional texture metadata
and rejection of invalid dimensions/viewports before GL writes.
