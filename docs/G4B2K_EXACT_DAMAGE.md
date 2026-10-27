# G4b.2k — bounded exact damage, atomic masked CPU commit and measured partial work

Implemented native experiment after [retained snapshots](G4B2J_RETAINED_WORKSPACE.md).
2026-10-27; default CPU GDI/L/ZL unchanged. No Switch hardware/FPS or game acceptance.

## Problem and selected representation

The prior diagonal partial workload exceeded128 dirty rectangles, forcing five
mid-series window CPU commits. That changed CPU/ownership epochs and required6
snapshots for9 frames, despite the guest not reading the window. Bounding-box
coarsening would write untouched pixels and could steal child/other capture owners.
This package preserves the exact accepted write set instead.

Sparse dirty Region remains the first tier. Candidate union stops at the first
rectangle-cap excess rather than constructing a full fragmented cross-product.
At pressure, optional exact tracking promotes the OLD accepted Region into an
owned one-bit-per-target-pixel mask. The current operation is marked only after
each actual target Fill succeeds. No future/unaccepted write is pre-credited.
Subsequent row spans set bits without CPU color rasterization or canonical writes.

Initial all-write bitmap measurement was unnecessary work for simple tiles. The
final adaptive policy allocates/marks a mask only at rectangle fragmentation.
Simple tiled/DIB workloads keep their old sparse tracker and allocate no mask.
No global Region/GDI clip/visible-region semantics are changed.

Bounds: four targets, side512, combined512² pixels remain. Mask memory is at most
ceil(MaxPixels/8)+MaxTargets−1 = **32,771 bytes** over live native slots; dimensions
are fixed for a resource lifetime. Promotion checks that budget explicitly.
Mask storage can be reused after a CPU checkpoint, cleared before recording the
new write set; retirement/shutdown release it. Region/clip limit128 is unchanged.
If allocation/budget refuses promotion, old accepted Region stays intact and the
existing real CPU checkpoint path handles pressure. Failure is not successful
raster with missing dirty pixels. A control change does not drop already active
masked writes; they remain until the next real checkpoint.

Exact tracking is selected by EnableSolidDispatch only inside the existing
explicit native experiment. Native EnableExactDamage is a same-build reference
control requiring a healthy current USER owner. Ordinary default CPU GDI and
isolated explicit-coherence diagnostics are unchanged.

## Atomic CPU authority and capture

Gdi::CommitWindowMask is a synchronous internal authority exchange, not a guest
API. It validates actual HWND identity/layout/frame/image dimensions/pitch, exact
mask byte count/zero padding and aligned pixel storage/pitch, then EVERY marked pixel against current whole-
window visibility before capture setup or mutation. Invisible/foreign/stale/
malformed input is rejected with zero committed pixels and unchanged CPU data.

After all validation, capture is requested once and only marked pixels call
Surface::Set. Untouched CPU pixels, retained pixels and capture ownership remain
untouched; there is no bounding rectangle expansion or silent clipping of illegal
bits. One successful nonempty commit changes the CPU screen revision once. Same
window-mask side bound is compile-time checked against actual GPU target capacity.

WindowGpuBindings uses the masked transaction only for promoted slots. Resident
membership includes both sparse and masked dirty data. Representation promotion
alone does not change ownership/pixel epochs; real CPU commit clears resident
ownership and advances the existing epoch. Snapshot reuse remains guarded by
CPU/layout/shell/ownership/current-owner health. CPU read/ROP/DIB/layout/lifetime,
ordinary first-frame work gates and real context-loss recovery remain mandatory.

Report v12 separately captures promotions, tracking refusals, masked CPU commits,
exact committed pixels and peak mask bytes through shutdown. No DMA/physical
traffic/GPU execution interpretation is attached to those native counts.

## Public workload evidence

The existing actual-x86 native-shell inspector now rotates three same-build
policies: historical full snapshot/Region; retained snapshot/Region (incoming
baseline); retained snapshot/adaptive exact damage.36 normal-exit sessions and
324 full1280×720 production-EGL frames; sample0 triplets excluded from timings.
Every output pixel uses independent guest/native-shell expectations. No private
EXE, native-injected substitute for ordinary GDI, noisy timing threshold or FPS
claim is introduced. Prior policy results remain explicit controls.

| Diagonal partial workload,9 frames | Incoming retained Region | Adaptive exact damage |
| --- | ---: | ---: |
| Mid-series window CPU commits before guest read |5|0|
| Owning snapshot builds |6|1|
| Snapshot materialization bytes |66,443,520|11,073,920|
| Target readbacks before real guest read |11|8|
| Target readbacks after real final GetPixel |12|9|
| Mask bytes after initial frame / subsequent frames |0/0|0/2,048|

Eight journal-capacity readbacks remain genuine; this package does not pretend
they disappeared. Tiles keep1 snapshot/zero capacity reads and need no mask.
Raw guest DIB+SRCCOPY still needs9 snapshots,17 pre-guest reads/final18 and10
imports/CPU commits including the final read. CPU-copy semantics are preserved.
The existing parked two-window snapshot-transition oracle is retained.

New public CRT-free gpu_damage_mask.exe uses an odd37×29 parent, actual child and
occluding top-level windows, selected clip with a hole, nonzero window/viewport
origins and sparse ordinary interpreted-x86 writes. CPU reference and masked GPU
screen/EGL output match, including retained untouched pixels and owners. Four
normal-exit sessions cover normal damage, repeated promotion/bit clearing,
permanent mask-allocation refusal with original Region fallback, and actual EGL
context destruction/software recovery. One lost-layout/authority case is parked,
rejected without stale CPU writes/output, then drained; not a normal exit.
Custom allocator storage returns to zero; odd target uses exactly135 mask bytes.

Native test_window_damage_mask additionally rejects malformed length/padding/pitch/alignment,
invisible hole, wrong identity, stale layout and late capture failure atomically.
Only four explicit dirty pixels can change CPU/retained pixels/capture owners.
Negative omitted visibility guard fails that oracle; omitted clearing of a reused
mask fails exact accepted-pixel count after the next fragmented write cycle.

## Measurements and next block

benchmark_workspace_reuse.py schema2 records all three policies,324 raw intervals,
exact PE/source hashes, per-frame mask memory, work counts and host/cache policy.
Refresh includes output; target flush moves into output on retained reuse, so
compare total cycle. Mask validation/commit can scan the bounded image at a real
CPU boundary; fewer checkpoints are not proof of universal speedup. No GPU
execution timestamp, physical scanout or Switch FPS is inferred.

Next measured block: remaining journal-capacity synchronization on shifted partial
writes, plus CPU-side import/checkpoint cost. Evaluate bounded replay/checkpoint
alternatives against actual GL pixels and real application reads before changing
that contract. Do not remove real DIB/ROP/readback/lifetime authority barriers or
award broad/default activation. Full dynarec/D3D9/D compatibility and game targets
remain separate. Local devkitA64/libnx absent; actual Switch v12 compilation is CI
only. Physical Paint/GPU/fallback/input/relaunch deferred; SD remains closed.
