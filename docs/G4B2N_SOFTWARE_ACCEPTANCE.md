# G4b.2n — consolidated native software acceptance and CPU/native cycles

2026-10-28. **PASS_BOUNDED_NATIVE_EXPERIMENT_PENDING_HARDWARE** applies only
after the complete MinGW/Mesa runner and its ledger succeed. This is acceptance
of the documented bounded native experiment, not default GPU activation, full
GDI, Switch performance, all Stage G or PC-game compatibility. Default CPU GDI,
L/ZL, Luna and the consolidated deferred hardware checklist remain unchanged.

## One software acceptance ledger

`tools/record_gpu_acceptance.py` runs only at the successful end of
`COMPOSITION_GL_TEST=1 sh tools/run_host_tests.sh`, after the existing F report.
The runner removes the old ledger before work begins; the reporter also removes
it before inspecting evidence. Missing/failed evidence cannot leave a stale PASS.

The ledger binds17 evidence groups to their log hashes:

| Contract | Actual executable evidence |
| --- | --- |
| Unavailable GPU | Ordinary non-GL host refuses simulated GPU success |
| Owned primitive | Actual EGL fill/copy/readback, caps, allocation/error/loss replay |
| Multiple targets | Generations, budgets, resident pins/retirement, stale/foreign reset, genuine fallback |
| Shared renderer | Hostile GL state, production interleaving, resident layers, owning mixed frames, actual lost-context recovery |
| Real bindings | Public x86 HWND/DC/clip/layout/reused identities and sessions |
| Coherence/device | Two10-stage x86 modes; real GetPixel/ROP/raw-DIB/GetDIBits; native shell/USER selection; seven device/fault/cursor/layout modes; four host-owner/lifecycle audits |
| CPU gate | Expired/reentrant/sticky fault and suspended-store isolation |
| Exact CPU commit | Visibility/padding/identity/late-capture atomic rejection |
| Exact GPU damage | Odd window, child/occlusion/clip, bit reset, allocation fallback and actual context loss |
| Capacity replay | Submit-before-free, CPU-work/input bounds, owned alias and loss/error recovery |
| Differential seed | Actual pixels, source ownership, unchanged/offset/gaps, allocation refusal, GL state/error and actual loss |
| Covered retirement | Only covered submitted work retires; pending/partial/copy recovery survives |
| Ordinary dispatch | Actual common x86 solid calls, CPU/EGL/read/ROP/DIB/size/slot/allocator/lifecycle and paired first-frame controls |
| Warm journal | Same-resource reference/retirement and genuine final guest read |
| Retained workspace | Five policy controls/540 EGL frames and owner/CPU/layout/shell/reset/fault transitions |
| CPU/native cycles |32 real normal exits/288 EGL frames through actual default CPU and current native routes |
| Ordered batches | Exact pixels/order/barriers and42 same-build samples |

A successful last line alone is insufficient: the mixed/device and shared-frame
logs must include every named required mode. Five subordinate measurement reports
must have the expected schema/status/sample count and match current HEAD/dirty
state. PE hashes must match this runner's generated files; native runner hashes
are rechecked where provided. Eight public fixtures require actual PE32/i386
headers, not just an MZ prefix. A source manifest/hash supplements dirty local
SHA evidence. Only a clean exact published-SHA CI report is publication evidence.

The existing `stage-gpu-window-coherence` artifact contains the ledger, underlying
five reports/17 logs, eight generated public PEs and this contract. Core CI stays
six jobs. Other-stage acceptance and physical tests retain their own scopes;
these evidence groups are not a fabricated total number of completed guest sessions.
Parked fault/owner/device cases remain parked/drained cases, not normal exits.

## Fair CPU/native route comparison

The new public inspector uses the same PE/build/native shell/clock and actual
production OpenGL output at1280×720. Four workloads each have four rotating
CPU/native pairs (sample0 excluded from timing summaries):32 normal-exit0
sessions, nine frames each,288 independently checked full framebuffer images.

- CPU: no native bindings/coherence/solid hook installed. Real
  ShellSession.TryCompose/Scene → Presentation.Submit/Render with retained CPU
  window/native layers, matching the ordinary production adapter.
- Native: existing coherent ordinary solid experiment and MixedWorkspace.Refresh
  → production resident renderer, with current exact damage/replay/import policies.
- Workloads:64 tiles per frame; shifted64 partial8×8 fills; tiles plus actual raw
  DIB stores/CPU SRCCOPY and a native sentinel;600×128 window exceeding the512-side
  admission bound. Its576 calls must refuse to real CPU with zero target allocation.

Each frame uses independent expected guest/native-shell colors, including
unchanged partial pixels and real DIB data. Explicit final guest GetPixel checks
must actually run before normal exit0. Accepted/refused/import/commit/upload/
snapshot/reuse counts are checked per frame. CPU has no GPU bindings/candidates;
size-refusal native has576 candidates/refusals, no accepted native work/imports.

Raster time sums actual RunOne calls with identical test-only clock overhead,
including real HLE/DIB/synchronous checkpoints. Host probing/expected-pixel work
and idle sleeps are outside that sum. Refresh includes prepare and actual driver
output submission; total cycle=raster+Refresh. Terminal GetPixel and guest exit
RunOne phases are recorded separately and added to nine-cycle sequence totals.
Module load/prepare, native frontend setup/teardown and independent oracle FBO
reads are excluded. No physical swap/scanout, GPU execution or full application
startup latency is measured. Oracle reads synchronize between frames. Renderer
transfer counters and guest-image import payloads remain separate attempted
byte counts, not physical DMA.

Schema1 preserves all288 raw frames/32 read+exit sessions, per-workload cold/warm/
sequence summaries, source/runner/fixture/host/cache identity. No timing threshold
or automatic policy decision is used. Fewer uploads/draws cannot alone award a
speedup; DIB synchronization and refused admissions remain visible CPU work.

## Decision and next block

Final local warm CPU/native cycle medians were approximately5187/2879us tiles,
5107/3159us partial,5083/6518us DIB and6075/6200us refusal. Native tiles/partials
were faster in this host run; DIB and refusal were slower than ordinary CPU. Host timings vary and do not
certify Switch FPS. The current experiment therefore stays explicit, with CPU
the default. Its software semantics/lifetime/fallback acceptance is consolidated;
performance/default activation is still open.

Next: admission-aware USER output routing when there are no resident GPU owners.
A refused/CPU-only frame should be evaluated through the ordinary CPU producer,
with checked CPU authority, output acknowledgements, shell/cursor/lifetime and
failure behavior. Keep real DIB and genuine recovery barriers; do not infer a
DIB fix from the no-resident case. Then choose further G CPU/dynarec or H graphics
work using those complete-path results. D/Pinball, full dynarec/D3D9, installers/
archives, broader shell and target games remain separate milestones.

Local devkitA64/libnx is absent; exact-SHA CI builds NRO. Physical Paint/GPU/
fallback/input/Lite/relaunch remains deferred; SD diagnosis remains closed.
