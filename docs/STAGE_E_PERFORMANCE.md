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
