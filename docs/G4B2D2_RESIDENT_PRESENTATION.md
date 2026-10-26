# G4b.2d.2: resident images in the production GL compositor

Incoming verified source `72545db7d2785e50a95294d387b0609b379c832c`,
[CI40343116185, six jobs](https://github.com/Omeruno/switch-recompiler/actions/runs/40343116185).
Extends [owning resource frames](G4B2D_RESIDENT_FRAMES.md) and the existing
production ownership/cursor shader. Explicit native entry only; default Win32 GDI
and ShellSession CPU composition have not been switched to it.

## Synchronous resource exchange

`GpuGdiTargets::ConsumeFrame` validates a prepared owning frame, flushes accepted
GPU commands and lends a bounded `ResidentFrameView` to a native consumer.
No GPU image readback or CPU image clone occurs for a healthy GPU resource.
After real target failure or notified context destruction it instead lends
actual recovered CPU surfaces from checkpoint/journal replay. Both live and
retired pinned resources participate. Shutdown/reset/foreign/default frames
reject before invoking the consumer. Existing four-image / side512 / combined
512x512 budgets remain; view storage is fixed on the stack.

The consumer must return synchronously on the owner thread without changing the
context, mutating/resetting the manager, yielding or retaining views/GL names.
It borrows resources, never owns/deletes them. The caller retains the owning
frame for later redraws and asks for new views each time. This is an internal
native rendering protocol, not an exposed guest texture/handle API. Frame
contents remain live references, not immutable historical pixel snapshots.
Consumer rejection alone does not declare a device loss: malformed scene/token
input must reject without invalidating otherwise healthy resources.

## Production render path

`OpenGlPresentation::RenderResident` maps at most four resident images by unique
nonzero ownership tokens into a normal bounded scene. Matching CPU layer surfaces
may be null: no guest CPU mirror is read, copied, compared or uploaded for that
layer. The unchanged production shader selects base/CPU layers/resident images
using ownership, clipped translated geometry, viewport scaling and cursor rules.
A previous owned CPU texture cache is retired when its layer becomes resident;
borrowed GPU names are never put into that owned cache or deleted by renderer
reset/shutdown. A later CPU frame uses the normal genuine upload path.

The caller still supplies valid owned CPU base pixels, ownership and any CPU
native chrome/overlay snapshots for this synchronous render. Their existing
revision maps control uploads. Base pixels under resident owners may be stale;
base pixels selected by owner0 must be current. Every resident token must map
exactly once to the scene. Bad/duplicate/unmatched tokens, invalid metadata and
budget overflow reject before framebuffer writes; no raw guest pointer is kept.
This does not yet replace Presentation::Submit/ShellSession::Compose or own
mixed scene snapshots for the persistent frontend. A duplicate CPU canonical
snapshot elsewhere in that old path cannot be counted as eliminated here.

GDI target creation now configures nearest filtering and level0 immediately.
Previously only the isolated target presenter configured sampling; directly
borrowing a freshly created target otherwise samples an incomplete texture.
The new all-pixel oracle fails when only those creation settings are removed.

## Failure and fallback

A failed renderer submission must never be swapped. For an actual renderer
failure, after ConsumeFrame returns the native caller uses `RecoverCpu()` to
switch all manager images to genuine CPU replay, then consumes recovered views
through the existing CPU output/fallback path. This explicit operation is
idempotent and differs from ContextLost: ContextLost must follow actual context
destruction and issues no GL calls. A recoverable target flush failure supplies
CPU views automatically. The production renderer still requires a live GL
context; after context loss recovered CPU views must use software output rather
than calling its GL renderer. Controlled frontend routing remains integration
work; no automatic device-recreation policy is introduced.

## Evidence and work accounting

The existing production/shared-context EGL oracle now also compares all96x64
output pixels for twelve mixed frames against an independent CPU ownership
composition plus the established software viewport/cursor renderer. Two resident
images have no CPU surfaces in the submitted GPU scene. Cases cover negative
origin, moving geometry, scaled/letterboxed output, ownership holes/overlap,
patterned CPU chrome/panel, moving cursor and an asymmetric GPU content change.

Deterministic counters require zero readbacks/uploads for both fill-only GPU
images throughout these twelve frames, exactly one base and one CPU-overlay
upload. Ownership updates are real uploads and are not hidden in those counts.
Test-only default-framebuffer reads compare pixels; this is not a zero-readback
benchmark or a Switch FPS result. No timing speedup is inferred.

Invalid duplicate/unmatched token mappings preserve previous FBO pixels and GPU
activity. Retired image frames still render, and renderer teardown leaves borrowed
textures alive. A real renderer GL error rejects the output; explicit manager
recovery then renders actual CPU-replayed pixels, compared against the same final
expected frame. Existing target context-loss oracle additionally consumes a
retired image after actual EGL context destruction and checks every recovered
pixel without GL calls. Existing live-target continuation tests remain intact.

Strict O2 and focused ASan/UBSan actual-renderer/target tests pass; driver leak
scanning is disabled. The negative sampling-state control fails the first new
frame's pixel comparison. Full COMPOSITION_GL_TEST=1 suite and exact published-SHA
six-job CI are package gates. `stage-gpu-gdi-shared-context` includes its log and
this contract. Local devkitA64/libnx is absent; CI builds NRO. Physical E/F and
Paint/GPU/fallback/input/relaunch remain deferred; no user hardware test needed.

## Next

Own a complete mixed CPU/resident scene in the persistent presentation layer,
connect validated live HWND/window ownership to resident resources, and bypass
the unconditional canonical CPU snapshot for that selected path. Audit remaining
raw host pixel consumers and route coherence/render failures into the existing
controlled frontend cleanup before selective ordinary GDI admission. Keep Luna,
input, CPU ROP/DIB reference, software fallback and session boundaries. This
package is not a dynarec, Direct3D or acceptance of the planned PC-game targets.
