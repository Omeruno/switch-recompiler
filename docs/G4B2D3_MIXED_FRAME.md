# G4b.2d.3: retained mixed CPU/resident presentation

Incoming verified source `5f72b6782787ad1de0b42c3795b5bd1147d62776`,
[CI40390490786, six jobs](https://github.com/Omeruno/switch-recompiler/actions/runs/40390490786).
Extends [resident rendering](G4B2D2_RESIDENT_PRESENTATION.md). Explicit native
entry only: live HWND/ShellSession wiring and ordinary GDI admission remain open.

## Ownership and submission

`MixedPresentation` owns one accepted base, ownership plane, CPU layer snapshots,
layer geometry/token mapping and a copy of the manager's pinned resident frame.
No producer Surface, tile-map, HWND/DC, guest memory pointer, manager pointer or
borrowed GL name survives submission. Allocator must outlive the object. All
operations remain synchronous on the native GL context/manager owner thread.
Resource pins reference live GPU contents, not immutable historical images.
Later accepted writes are visible on redraw; CPU inputs are immutable snapshots.

Submission validates dimensions, distinct tokens, exactly one scene layer for
each resident image, and every nonzero owner pixel against its layer's geometry.
Unknown/out-of-bounds owners reject, rather than reconcile to stale CPU pixels.
CPU references for resident layers may be null and are never dereferenced/copied.
Base pixels selected by owner0 must be current; base pixels beneath another
owner may be stale. Caller supplies a CPU base plane, not a complete canonical
CPU composition of the GPU layers. CPU overlays preserve explicit ownership and
can occlude resident windows. There is no implicit scaling of a resident layer.

New state is built separately and published only after validation and all
allocations succeed. Rejection preserves the previous accepted frame and pins.
Validation calls ConsumeFrame to obtain real resource dimensions, so it can
flush accepted GPU commands or trigger genuine target recovery. Rejection is
not a transaction rolling back GPU activity or live target contents.

Stable owned CPU revision maps avoid reuploading unchanged base/CPU overlays on
cursor redraw. CPU and mixed presentation share one process-wide resource-ID
space: switching paths cannot alias stale texture-cache identity after address
reuse. A new submission copies its CPU inputs conservatively and establishes a
new identity; this is not incremental submission or a throughput claim.

## Rendering, failure and lifetime

Render reacquires temporary resident views from the supplied manager and calls
the actual production ownership/cursor renderer. Healthy resident composition
performs no target CPU read or upload. The object owns native frame pins across
producer destruction, target retirement and repeated cursor redraw. It does not
keep a manager/context alive. Shutdown/reset/foreign managers reject before
output, and destruction/Clear release pins without GL calls.

A failed GPU render must not be swapped. It does not automatically reconstruct
or swap output. RenderCpu is the explicit software fallback boundary: validate
the frame against the supplied manager, RecoverCpu outside the consumer, then
compose the owned base/CPU layers and genuine checkpoint/journal-replayed
resident pixels into an owned flat scratch surface and use SoftwarePresentation.
Recovery switches the whole supplied manager to CPU mode; it is not a temporary
per-frame readback. After actual context destruction call ContextLost first;
this fallback then needs no live GL context. No device-recreation policy or
frontend failure routing is introduced here. Recovered frames deliberately
reject the GPU entry; they must use the explicit CPU route.

## Bounds and work accounting

Existing resident limits remain four images, side512, combined512x512 pixels.
Scene limits: 48 total layers, output at most1280x720, combined CPU layer pixels
at most16MiB, plus packed base and ownership (at most3,686,400 bytes each),
32x32 revision maps and optional software flat scratch (at most3,686,400 bytes).
Atomic replacement temporarily holds old and candidate CPU snapshots. Metadata,
standard allocation of State/resource pins and driver storage are additional;
only trivial buffers use the injected PresentationAllocator. CpuBytes reports
owned trivial buffers, including revision maps and allocated fallback scratch;
CopiedBytes accumulates accepted snapshot storage including initialized revision
maps. It does not measure CPU memory traffic, GL transfers or rejected attempts.
CPU ownership validation and software fallback scan the scene conservatively.
No overall memory/FPS/speedup claim follows from this bounded native entry.

## Evidence

Extended public actual-EGL shared-context oracle checks:

- poison/free all producer pixel, layer and ownership storage, then compare every
  output pixel against an independent CPU oracle for twelve cursor redraws;
- asymmetric later GPU write, CPU overlay overlap, exactly zero target reads/
  uploads and one base/one small CPU-overlay upload during those twelve frames;
- injected failure at each of five trivial-buffer allocations, unknown owners,
  out-of-bounds owners and duplicate tokens preserving the accepted frame;
- actual switching through ordinary Presentation on the same backend, then
  returning to the mixed frame with independent all-pixel comparison;
- target retirement and release of all other pins, foreign-manager rejection,
  real renderer GL-error rejection and explicit software recovery pixel equality;
- a separately created/destroyed real EGL context, retired sole-pinned image,
  both submitted and pending writes, and software replay with no current context;
- shutdown revocation, unchanged output on failed software request, complete
  injected-buffer cleanup after Clear.

Strict O2/no-exceptions/no-RTTI and ASan/UBSan cover the full standalone renderer/
target/presentation harness; driver leak scanning disabled, module allocations
checked independently. A negative control forces mixed resource-ID collision:
the cross-path pixel comparison fails. Full COMPOSITION_GL_TEST=1 suite and exact
published-SHA six-job CI remain required package gates. The existing
stage-gpu-gdi-shared-context artifact contains the log and this contract.

Local devkitA64/libnx remains absent; CI builds NRO. Physical Switch/Paint/GPU/
fallback/input/relaunch checks stay deferred. Host Mesa is not physical acceptance.

## Next

[G4b.2d.4](G4B2D4_LIVE_WINDOW_FRAMES.md) now connects validated live-window
ownership/resources through an explicit native coherent entry. Next wire
ShellSession/persistent frontend to it without unconditional canonical CPU
composition for the selected path. Audit remaining raw host reads/writes and
route coherence/render faults through controlled frontend teardown. Only then
admit a bounded ordinary GDI subset with public real-x86/CPU/EGL and work-count
checks. Ordinary GDI/default Paint remains CPU, not GPU-accelerated by this change.
