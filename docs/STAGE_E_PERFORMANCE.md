# E8 measurement boundary (2026-10-23)

This package instruments the production GL backend and extends the exit-only
renderer report to v6. It does not award E8's percentage or claim Switch speedup.

| Field group | Scope / denominator |
| --- | --- |
| Snapshot ns / layer snapshot ns | Existing CPU normalization/copy and retained-layer reconciliation; layer time is included in snapshot time, not an additional disjoint stage |
| GPU CPU stage frames | Successful GL Render calls; upload CPU ns and draw submission CPU ns are sums over these calls, with separate maxima |
| GPU upload CPU ns | Texture preparation, revision comparison, upload API calls and obsolete resource retirement; driver stalls may be included |
| GPU draw submission CPU ns | GL state/uniform setup and base/window draw API submission; not GPU execution time |
| GPU command interval samples/ns | Completed asynchronous TIME_ELAPSED queries spanning uploads and draws, including GPU idle gaps between commands; excludes swap/scanout; divide by samples, never frames |
| GPU timer ring skips | Sampling opportunities skipped because all four slots were pending; rendering continues |
| GPU screen/ownership/layer upload attempted bytes | Bytes passed to texture upload calls, including failed attempts; not PCIe/bus measurements or driver traffic |
| GPU uploaded bytes | Existing successful-frame upload total; may differ from attempted totals after a rejected frame |
| GPU logical texture bytes / peak | RGBA8/R32UI allocation sizes plus cursor; excludes driver padding, shader/query objects, EGL/window buffers and total process memory |
| GPU revision cache bytes | CPU-side GL tile revision metadata, separate from logical GPU texture bytes |
| GPU swap ns | Existing CPU time in platform Swap; includes platform blocking, not physical input-to-photon latency |

The backend checks GL query counter bits at initialization. Unsupported or failed
query setup disables sampling without disabling rendering. Four query objects
bound storage. RESULT is read only after RESULT_AVAILABLE; there is no glFinish,
blocking result read, production glReadPixels or wait at session exit. A final
nonblocking poll may leave the newest samples uncounted. Queries encompass render
attempts, including attempts later rejected by GL error checking; CPU stage sums
cover successful calls only. Do not combine their denominators after errors.

Reset invalidates upload revisions but keeps actual texture allocations and
pending query objects; accounting therefore remains nonzero. Shutdown deletes
textures and all query objects while the GL context still exists. Session
Initialize clears cumulative counters. If a runtime fallback shuts GL down, the
peak and cumulative values remain, while current GL storage becomes zero.

Host EGL/Mesa tests compare real GPU output to the software reference at native
1280x720 and scaled/letterboxed sizes, exercise palettes/negative pitch, windows
with poisoned base pixels, cursor-only zero-upload redraw, allocation faults,
GL failure/retry, reset and teardown. Additional assertions verify independent
CPU stage counters, completed asynchronous queries and texture accounting.
Host GL readback intentionally completes work for assertions; production does
not do that. Host timings are not evidence of Switch GPU/latency performance.

## Consolidated deferred Switch checks

Run one exact-SHA NRO/artifact set after CI, without overwriting the stable NRO.
Keep private user logs and proprietary Paint/MFC binaries outside public git.

1. Native Lite 1280x720 presentation corners/borders and all 28 composition
   scenes, including move/hide/destroy exposure, child clipping and screen ROP.
2. Paint: start, toolbox, ChooseColor, continuous strokes while moving, menus,
   Open and Save; retain the session renderer report before another run overwrites it.
3. Cursor-only motion: no new screen/ownership/window texture upload bytes.
   Repeated unchanged frames should remain skipped by the frontend.
4. Compare the same deterministic scene sequences and comparable Paint workload
   on software and GPU paths. Record session duration, workload, sample counts,
   CPU snapshot/layer/convert-upload/draw/swap sums and maxima, bytes and memory.
   Do not label draw-submission time as GPU work or timer average as FPS.
5. Verify GPU timer availability/sample count on actual Switch Mesa, bounded
   pending/skipped samples, sustained input progress and repeated session cleanup.
6. Explicit software fallback smoke and ordinary exit after active window layers.
7. Separately measure raster CPU stage, overall process/driver memory and physical
   input-to-display latency with an appropriate device/method. Current report does
   not measure these, so full E8 acceptance remains open.

Next software block is E9 combined unattended acceptance/reporting. Optimize
further only after the counters or a reproducible benchmark identify excess work.

## v7: CPU raster and observed-input correlation

New-UI sessions enable eight CPU GDI counters: pixel writes, strokes, shapes,
fills, blits, flood fills, text and readback. Each records outermost call count,
sum and maximum wall nanoseconds. Counts include rejected/clipped calls and
synchronous context/clip/font/layout preparation. Nested GDI/helper calls are
included once in their caller's stage: FrameRect's four FillRect calls, Polygon's
PolyPolygon, InvertRect's PatBlt and DrawText's TextOut are not added again.
FillSurface desktop painting is covered. Raw guest DIB writes, direct Surface
writes outside these entry points, guest instruction time and legacy UI are not
raster timings. Recording is outside the per-pixel loops. An external SetPixel
call is still one measured operation; instrumentation overhead must be considered
for tiny calls. Scopes assume the current synchronous, non-yielding GDI contract.

Input stamps use real steady-clock time, never rt::Clock's accelerated guest
time. The Switch adapter stamps the HID polling iteration; platform events with
no stamp get one when the batch is ingested, before hit-test/activation callbacks.
The stamp stays host-only: the guest 28-byte MSG, GetMessageTime, message ordering,
coalescing, ownership, key state and 16 ms present throttle remain unchanged.
Only removal from the input queue records a queue-wait sample. PM_NOREMOVE and
posted/synthetic messages without a platform stamp do not create samples.
Coalesced motion retains the newest observation and has a separate counter.

Up to 64 recently removed inputs wait for a successful output adapter completion.
A sample is completed only if both the CPU change counter and normalized frame
content serial differ from its removal baseline. A cursor-only/no-op output,
unchanged content or failed render leaves it pending. GPU-to-software retry may
complete it only after successful software output. Multiple inputs may share one
frame; reported sample count is therefore not frame count. Ring overwrites,
invalid future timestamps and pending/unrendered samples are reported separately.
Session begin/end resets and disables tracking; no input payload or pointer is
retained, no per-frame SD log is written, and storage cannot grow.

Fields distinguish observed-to-queued-frame from removed-to-queued-frame. The
endpoint is successful Swap/framebufferEnd API completion, including CPU snapshot,
upload/conversion and platform waiting. This is correlation with a later changed
frame, not proof that a particular DispatchMessage caused its pixels. Applications
can discard input and draw unrelated content; peer GUI threads can also draw.
Neither measure includes the delay before host observation or after API queuing,
so neither is physical input-to-photon latency. No Switch latency value is claimed.

The native metrics regression verifies exact nested timing with a deterministic
clock, zero clock calls when disabled, bounded ring/drop/reset behavior, and
pixel equality for measured/unmeasured GDI. Queue/output integration verifies
peek versus removal, unsuccessful output, changed content and independence from
100 seconds of guest fast-forward. Existing real-x86 input/capture tests also run
with tracking enabled and exercise motion coalescing at 800x600 and 1280x720.

Deferred checklist item 7 now needs actual console raster measurements and a
separate physical input-to-display method, rather than a missing CPU raster
counter implementation. Total process/driver memory and hardware E8/E9 remain open.


Additional deferred startup smoke after F0: multiple EXEs across pages, long
labels, ROMFS/SD ordering, single-file autolaunch, empty/incomplete inventory,
B/PLUS cancellation and applet closure before guest startup. Verify current L/ZL
launch overrides and launch-held input quarantine. This does not change or award
E8/E9 graphics acceptance.


Core-session ownership now has a host regression (F0_SESSION_CORE.md). Deferred
physical scope also includes normal application exit and startup/load failure
with the new owned lifetime in GPU and forced software modes. Repeated picker
return/relaunch remains a future check after GUI/legacy lifetime isolation and
launch-loop integration; it has not been implemented or accepted by core tests.


New USER/GDI lifetime is now covered by 12 sequential host x86 window/DialogBox/
ChooseColor sessions (F0_GUI_SESSIONS.md), including parked cancellation and owned
renderer redraw after memory destruction. Physical exit/load-failure smoke must
use the new lifetime too. Repeated frontend relaunch remains deferred until the
legacy UI/MFC/input/image-binding gate and actual launch loop are implemented.


Legacy session lifetime now has nine sequential public x86/MFC fixture runs
(F0_LEGACY_SESSIONS.md). Frontend hit/click/touch scratch resets and scoped EXE/
resource/pump/stack bindings still require the actual Switch smoke with current
Paint and both render modes. Production return-to-picker is not yet connected;
repeat-launch hardware acceptance follows that separate frontend integration.
