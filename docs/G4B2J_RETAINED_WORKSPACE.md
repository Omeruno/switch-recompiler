# G4b.2j — reuse owned workspace snapshots across live GPU updates

Implemented bounded native experiment, 2026-10-27, after
[warm GDI/journal measurements](G4B2I_WARM_GDI_JOURNAL.md). Default GDI remains CPU;
L/ZL and the existing native experiment flag retain their selection policy.
This is not physical Switch/FPS, general Paint acceleration or D3D acceptance.

## Measured problem and retained-data contract

The public nine-frame workload previously made MixedWorkspace rebuild its owning
CPU base, ownership mask and native shell layer on every GPU update. Baseline:
9 submissions and99,665,280 snapshot bytes, despite unchanged CPU/layout/shell.
MixedPresentation already owns these immutable CPU snapshots and pins live GPU
resources. Rebuilding them is unnecessary if their geometry/authority is unchanged.

WindowGpuBindings now publishes a distinct native OwnershipRevision. Unique
presentation IDs change it on coherent dirty-empty→resident transitions, CPU
checkpoint clearing resident ownership, resource retirement, shutdown and fresh
coherence initialization. Ordinary GPU writes inside an already resident image
only change the existing pixel revision. Adding a second resident window must
invalidate the snapshot even when CPU pixels/layout did not change. Reset must
invalidate it even when CPU/shell/layout revisions coincide with an old frame.
No guest pointer, descriptor or external resource owner is retained by this epoch.

MixedWorkspace can reuse its owning snapshot only when attached/current/healthy
and CPU screen revision, layout generation, shell revision and ownership revision
all match. ShellSession NativeRevision observes pending native task completion
without allocating/rebuilding the ownership mask; foreign USER sessions are
rejected before touching suspended shell metadata. NativeOverlay is still built
for a new snapshot. FrameCurrent additionally checks GPU pixel revision, so new
GPU writes cannot be mistaken for an unchanged cursor redraw.

On reuse, real output still consumes and flushes live resident targets and then
renders/swaps under the existing MixedOutput policy. No GPU work is silently
acknowledged without output, and no old canonical CPU flat frame is queued.
CPU/layout/shell/ownership changes rebuild and validate the full owning snapshot.
No borrowed CPU pointer or mutable ownership map is substituted for ownership.
Explicit genuine software recovery still uses current checkpoint/journal data.
Output rejection latches the existing faults and cannot retry/publish stale pixels.
The existing selector failure/acknowledgement and teardown order remain gates.

EnableSnapshotReuse is a native same-build reference control. Reuse defaults on
only where MixedWorkspace is already active; this does not enable ordinary GPU
GDI or native mixed output by default. Submissions counts actual snapshot builds;
ReusedFrames counts successful Refresh outputs using an existing snapshot;
Redraws remains cursor/unchanged redraw. SnapshotBytes resets its measurement
baseline per Attach. Report v11 captures these counts before destruction.
Snapshot bytes include initialized revision arrays as well as copied pixel/mask
storage; they are not physical memory-bus or DMA traffic measurements.

## Public workloads, authority boundaries and fixed work counts

`tests/test_gpu_workspace_reuse.cpp` runs24 actual normal-exit x86 sessions,
rotating reference/reuse with the same binary/build and production native shell
output. Four sample pairs per workload; initial pairs excluded from timing.
All216 frames compare every1280×720 actual EGL pixel to independent guest/native
shell pixels. Existing ordinary and warm fixtures/count gates stay unchanged.

The original public warm source also builds two explicit variants:

- Tiles:64 full16×16 ordinary FillRect/PATCOPY writes per frame. Resident ownership
  remains stable after the initial frame. One initial seed/upload, zero target
  reads before guest GetPixel; final real read remains1.
- PARTIAL:64 diagonal-shifting8×8 writes within the tiles. Independent oracle
  retains untouched strips from earlier frames. Actual dirty-region fragmentation
  exceeds the existing128-rectangle bound; five mid-series CPU commits remain.
  Snapshot reuse is safe only on three of the eight subsequent frames. This is
  not the full-overwrite journal-retirement case: pre-guest target reads remain11,
  then12 after the real final GetPixel. CPU seed import remains1.
- COPY: real raw guest DIB stores and SRCCOPY BitBlt to the window, followed by a
  solid resident sentinel. Genuine CPU checkpoint/reimport is required each frame.
  All9 snapshot builds remain; pre-guest reads17, final read18,10 imports and10
  total CPU commits after the final read. No CPU DIB-copy acceleration is claimed.

| Nine-frame session | Reference snapshots | Reused snapshots/builds | Snapshot bytes after reuse |
| --- | ---: | ---: | ---: |
| Tiles |9|8 reused /1 build|11,073,920|
| Diagonal partial |9|3 reused /6 builds|66,443,520|
| CPU DIB copy |9|0 reused /9 builds|99,665,280|

Reference materializes99,665,280 bytes for every workload. Work/pixel gates are
asserted for both policies; no timing threshold or pixel tolerance hides a failure.

One additional parked real-x86 two-window session tests GPU-only pixel update,
new resident owner without CPU/layout change, real CPU checkpoint, guarded raw
CPU-only mutation with stable ownership, reimport of the same resource, native
Start/clock change, layout-only change, suspended session rejection/restoration,
manager reset with otherwise identical scene epochs, output failure/no retry.
It is host-inspected and drained, not reported as a completed guest exit.
Two negative controls independently remove the ownership/CPU revision guard and
must fail the full pixel oracle on the new-window/raw-CPU mutation respectively.
Earlier device modes still cover actual GL/context/swap/staging/software recovery.

## Measurements and next decision

`tools/benchmark_workspace_reuse.py RUNNER TILES PARTIAL COPY COHERENCE OUTPUT.json`
removes stale reports and records raw216 intervals, work counts, exact source/PE
identity, host/cache policy and cold/warm medians/ranges. The existing host CI
artifact contains report/log/variant PEs/contract. Report status remains host-only.

Real x86 raster phase is separate from Refresh wall. Refresh includes output;
output includes pending target flush on the reuse path, whereas snapshot submission
flushes on the reference path. Prepare=Refresh−output is therefore not exclusive
copy time; compare total raster+Refresh for timing. Independent oracle FBO reads,
load/teardown and parked guest sleeps are excluded. No GPU timestamp, physical
swap/scanout, Switch FPS or universal workload speedup is inferred.

Next measured candidate: bounded dirty-region accumulation/coalescing with
CPU/visibility/capture-write correctness, rather than hiding its real checkpoints.
Diagonal partial writes expose that cost; COPY checkpoints remain necessary CPU
semantics. Preserve all bounds, actual ROP/read/DIB/layout/ownership/fault/recovery
contracts before any further admission/default activation. Full dynarec/D3D9 and
D/Pinball compatibility remain separate. Physical Paint/GPU/fallback/input/relaunch
is deferred; local devkitA64/libnx absent, Switch compilation is a CI gate. SD closed.
