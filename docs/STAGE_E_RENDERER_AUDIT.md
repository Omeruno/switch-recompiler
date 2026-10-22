# E0–E2: presentation audit and software reference

2026-10-22. Initial implementation package; physical acceptance pending.
This records the real boundary, not completion of dirty updates or a GPU path.

## Data flow and ownership

| Path | Existing producer / consumer | Package change |
| --- | --- | --- |
| Win32 GDI v2 | gdi32_module -> ui::Gdi -> State.screen | Raster semantics/clip/ROP/palettes remain on CPU |
| Windows | user32_window DcProvider gives screen origin/visible region; controls/window/desktop paint into shared screen | No retained-window compositor introduced |
| Present scheduling | user32_message PresentIfNeeded uses ScreenChangeCounter, 16 ms throttle and forced idle refresh; Platform.present is already the output callback | Existing policy remains, callback submits the CPU frame to Presentation |
| Native output | main newui::Present/Blit previously converted/scaled the whole surface directly into libnx framebuffer | Shared SoftwarePresentation performs conversion/scaling/cursor; frontend only acquires/releases the framebuffer |
| Cursor-only redraw | main newui::Wait calls Blit while returning input | Renders latest accepted owned snapshot; no retained State/guest Surface pointer |
| Touch | main newui::Wait computes inverse scaling | Same FitViewport as rendering; clamp existing drags, ignore new presses in letterbox |
| Software keyboard return | main newui::TextInput calls Blit | Same owned-frame redraw |
| DIB memory | CreateDIBSection/MakeDibView exposes guest bits; application may write without Surface::Set | Submit reads complete visible CPU frame; no claim that arbitrary DIB writes are tracked as dirty rectangles |
| Direct fills | user32_window desktop/background uses FillSurface; normal GDI MakeCtx increments ScreenChangeCounter | Counter is not complete pixel damage tracking; forced presentation remains; E3 must audit these producers |
| Legacy UI | main DrawGdiScreenIntoFramebuffer/PumpHostFrame uses legacy gdi::Bitmap plus toolbar/menu/palette/dialog overlays | Deliberately retains existing path and input semantics; existing regressions remain mandatory |
| Exit | normal frontend scope, after executor cleanup | Restores old platform callbacks, writes one summary and detaches owned frame on all ordinary returns |

Native fatal exits may bypass C++ destructors and therefore the renderer
summary. Existing stop/fatal diagnostics remain the source for such failures.
FramebufferBegin/End stays in the frontend; renderer has no libnx dependency,
no guest callbacks and no ownership of guest memory or framebuffer storage.
All calls are synchronous on the current execution/platform thread.

## Switch Lite / screen policy

Physical output is 1280x720. The Switch new UI now also advertises a 1280x720
Win32 desktop, so full-desktop content uses the complete panel without 4:3
side bars or nonuniform stretching. GetSystemMetrics/work area/desktop geometry
are derived from that State size. A fixed-size application window is not
stretched to fill the desktop. Paint still uses legacy UI by default; holding
L selects its new UI. For the source-only presentation EXE launch without L.

Other source sizes use centered aspect-preserving nearest-neighbour fit;
800x600 -> 960x720 with x=160, 1600x600 -> 1280x480 with y=120. Both axes use
the same viewport for input. The earlier code capped width but kept full
height for ultrawide sources, distorting aspect; the shared fit fixes that.
Cursor pixel shape/channel order match the previous native implementation.
New press in a black bar does not become an edge click; captured drags clamp.
No settings, host screen resolution changes or hidden guest EXE hardcodes.

## Contract

`PresentationBackend::Render(frame, output)` is synchronous and returns
success/failure. Frame contains a read-only CPU Surface, viewport, cursor and
serial. OutputBuffer is caller-owned RGBA8888 (`0xAABBGGRR` little-endian)
with explicit width/height/stride/capacity. Invalid input is rejected before
writing, output padding is untouched; an implementation failure is not counted
as a successful rendered frame. A backend must not retain borrowed arguments.

Presentation owns a normalized 32-bit snapshot of the most recent accepted
frame, supports redraw/attach/detach, and releases memory on detach or invalid
submission. This adds a full-frame copy on submission in return for explicit
lifetime. Cursor redraw does not resubmit or copy guest pixels. Direct guest
writes/palette changes are observed on a subsequent explicit submission.
Failed backend can be replaced and the same accepted frame retried.
Frame sizes/pitches/formats and output capacity arithmetic are validated.
C++ exceptions and RTTI are not required. Allocator exhaustion recovery and
asynchronous GPU lifetime remain E4/E5 work, not claimed here.

## Budgets and measurements

- One owned snapshot, 1280x720x4 = 3,686,400 bytes (800x600: 1,920,000).
  Supported submission cap 16 MiB; reject oversized frames before reads.
- Column map: native width 1280 integers = 5,120 bytes; validated output width
  max 8192 limits this to 32 KiB. Framebuffer memory remains libnx-owned.
- Full native output writes 3,686,400 bytes/render; no-op scheduling policy
  unchanged. Cursor redraw is still full output work. No E3/GPU savings claimed.
- Host test prints legacy/snapshot/render nanoseconds for the same 12 frames
  in both 800x600 and 1280x720 profiles. The frozen legacy comparator keeps
  the original 32-bit fast path and column map. Host figures are not Switch FPS.
- Switch writes `sdmc:/switch-recompiler-renderer.txt` once on ordinary exit:
  Build, guest/output dimensions, submissions/renders/redraws/rejections,
  current frame storage, cumulative snapshot/output bytes and stage times.
  Render ns covers CPU conversion/composition, not framebufferBegin/End,
  scanout/GPU time, guest raster time or input latency. Timing boundaries are
  intentional; do not interpret them as total frame time.
- Pixel error budget is zero for the legacy-supported profiles. Persistent
  renderer storage at native size must stay one snapshot + bounded column map;
  E3+ must justify additional buffers. Real Switch timing baseline is pending
  the first hardware report and will govern later acceleration comparisons.

## Verification / physical acceptance

`test_presentation` compares complete native outputs and old cursor pixels,
Lite full width, portrait/ultrawide aspect/input mapping, all six DIB depths,
16-bit 555/565, palettes, negative pitch, padded destinations, source freed
before render, resize, detach/backend failure, invalid geometry and raw-write
resubmission. It also emits the bounded host timing baseline.
`test_gui_input` sends five real platform batches while the peer polls and
renders through the actual software backend; paced mode uses 1280x720.
`test_presentation_guest` executes actual x86 Win32 desktop/metrics/GDI drawing
and checks all four native output corners, at both 800x600 and 1280x720.
Existing GUI controls/dialog/menu/ROP/SEH/thread regressions remain mandatory.
ASan+UBSan also exercise the standalone converter; LeakSanitizer is unavailable
under this environment's process tracing, so that run disables leak scanning.

Hardware next: matching green ordinary NRO + `gui_presentation.exe`, without L.
All four coloured corners and the continuous yellow border must be visible.
It exits automatically after about 4.5 guest seconds and writes
`stage-e-presentation.txt` beside EXE. It must report virtual screen 1280x720
and final PASS. Save root `switch-recompiler-renderer.txt` before another run.
Then a short new-UI Paint test (hold L for Paint): draw, move cursor to edges,
open/save, dialog placement and text usability on the Lite panel. Note any
speed/readability change: native desktop increases guest pixel area vs 800x600.
This short check is still needed before crediting the full E0–E2 35% packet.
