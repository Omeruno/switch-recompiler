# G4b.2o — explicit no-resident CPU output routing

2026-10-28. Adds a controlled production alternative after the
[CPU/native acceptance comparison](G4B2N_SOFTWARE_ACCEPTANCE.md).
Default GDI remains CPU. Existing native flag1 keeps its reference policy;
first byte2 of gpu-gdi-native.flag selects the new routing alternative.
No physical performance/default activation or game compatibility is awarded.

## Selection, ownership and device integration

WindowGpuBindings exposes a metadata-only resident-write query. MixedWorkspace
checks current USER identity, GDI/coherence/presentation health and reentrancy
BEFORE choosing a route. A configured native CpuOutput is borrowed for the
workspace lifetime and must synchronously own every submitted CPU/scene pixel.
If no target has uncommitted GPU writes, it uses checked ShellSession.TryCompose
and Scene and the ordinary owning Presentation, without creating a mixed snapshot.
Existing targets may still be allocated: residency means outstanding GPU authority,
not merely a texture handle. Required real reads/ROP/DIB barriers are unchanged.

CPU→GPU selects real resident submission after new accepted GPU writes. GPU→CPU
requires actual authority already committed at its CPU boundary; it does not drop
accepted work or substitute private CPU replay for a guest read. Old mixed frame
pins/snapshots clear on CPU selection. Cached CPU redraw keeps its owning sink
and validates CPU/layout/shell/GPU/ownership epochs, same USER and current health.
Redraw adds no canonical composition/checkpoint solely for cursor motion.

CPU submission/redraw failure latches controller/USER fault, prevents retry and
leaves output acknowledgements unchanged. Suspended owner refresh/policy change
reject without altering the outer producer. Detach resets sink-owned storage
without GL or USER callbacks. Sink outlives the producer; native exchanges do
not yield or call guest code. Existing native/reference and default CPU remain.

The Switch adapter uses the actual owning canonical presenter and device
render/swap/software queue via a non-recursive BlitCanonical entry. CPU refresh
forces actual output because the last visible frame may have used resident
textures while an older CPU serial stayed equal. Existing genuine canonical
software fallback checkpoints/disables layers before device destruction and
notifies bindings.ContextLost afterwards. Existing session drain/detach/device
order remains. Report v15 separates CPU-route submissions/frames and selection.
Local SDK absent; this libnx integration is compiled by exact-SHA CI.

## Evidence and policy

The same public CPU/native inspector now rotates three modes: ordinary CPU,
unchanged native and routed native.48 normal-exit0 sessions/432 independent full
1280x720 production-EGL frames retain four workloads and all original real
GetPixel/DIB/count/exit gates. Refused600x128 windows still have576 real CPU
refusals/no targets; routed mode builds zero mixed snapshots and submits9 genuine
CPU frames. Supported tiles/partials/DIB keep the original native work/read gates.
Schema2 captures all three modes and ratios without a timing threshold.

Three additional parked real-x86 sessions test actual USER selection CPU→GPU→CPU,
real device checkpoint, independent full-frame pixels, cached redraw without
another CPU barrier, raw CPU mutation, native clock, suspended-session rejection,
actual EGL destruction followed by cached software output, CPU submit/redraw
rejection/no retry/no acknowledgement, and clearing owning
CPU storage on detach. Parked sessions are drained, not counted as normal exits.
Existing device/error/loss/read/ROP/DIB/alias/visibility/lifetime gates remain.

Local and previous CI performance ratios differed, including DIB/refusal. Routing
is an experiment to measure, not an assumed universal optimization. No physical
FPS or request for console testing. Stage G full dynarec/Direct3D remain separate.
Next independent large block: real bounded registry creation/open/value/handle
semantics for the established D/Pinball API blocker, with public reproducers.
