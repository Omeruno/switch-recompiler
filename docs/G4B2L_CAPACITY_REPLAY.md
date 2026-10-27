# G4b.2l — bounded journal-capacity checkpoint replay

Implemented native experiment, 2026-10-27, after
[adaptive exact damage](G4B2K_EXACT_DAMAGE.md). Default CPU GDI/L/ZL unchanged.
No physical Switch/FPS, broad Paint/game, dynarec or D3D acceptance.

## Measured choice and ownership

The shifted partial public workload still required8 intermediate device readbacks
because the64-command recovery journal filled. Every supported command is an
opaque fill or an owned CPU-source copy. Its final logical pixels can also be
constructed from the previous private CPU checkpoint plus that ordered journal.

Room now offers a native same-build capacity-replay control. Eligible pressure:
GPU mode, a complete owned journal, and sum of CLIPPED pixel applications no more
than MaxReplayPixels=512²=262144. Overdraw counts repeatedly; this bounds optional
CPU work, not just target dimensions. Larger work retains the original device
readback checkpoint. Existing64-command/4MiB input/target bounds are unchanged.

The successful path:

1. Flush ALL pending GPU commands in order, with existing GL-state preservation.
2. Replay the journal into the already allocated PRIVATE CPU checkpoint.
3. Free/reset its owned commands/inputs, then accept the incoming operation.

CPU replay is real extra CPU raster work at capacity, replacing synchronous GPU
readback and full-image CPU checkpoint copying. It is not a free deletion of work
or a canonical CPU render plus duplicate GPU output every operation. No new
buffer/allocation, guest callback, canonical screen write, USER/ownership epoch
or device texture upload occurs during the successful replay checkpoint. The
GPU remains live and ordered; snapshot reuse still renders its actual texture.

Pending commands cannot be discarded before successful GPU submission. Flush
failure uses the existing complete journal recovery into CPU fallback; the new
incoming operation is then applied once. A recovered fallback is not counted as
a successful capacity replay. Device errors and actual loss retain genuine pixels.
The optional work cap does not prevent mandatory error recovery of accepted work.

Copy still snapshots its complete input BEFORE Room, including a borrowed private
checkpoint alias: Room may mutate that source checkpoint. Failed incoming input
allocation accepts nothing and changes no old journal. Copy inputs are owned,
clipped offsets remain correct and no caller pointer survives submission.

Explicit CpuRead/GetPixel, ROP, DIB, host/lifecycle/coherence boundaries remain
real device reads when GPU is healthy. Capacity replay does not manufacture a
successful guest read or certify scanout. Context loss instead uses genuine CPU
checkpoint+journal recovery, as before. New commands beyond opaque Fill/Copy need
a new audit before using this capacity policy.

EnableSolidDispatch selects the bounded alternative only in the existing native
experiment. Isolated primitive default stays reference; manager shutdown resets
policy. Native owner-checked EnableCapacityReplay enables same-build comparisons.
Existing retirement-only warm tests explicitly disable this new control so their
original work-count gates continue to compare only that previous feature.

## Public real-x86, framebuffer and CPU-boundary evidence

The native-shell inspector rotates four policies in one build/PE: historical
Region/full snapshot, retained/Region, retained/exact damage, and capacity replay
on top of retained/exact damage.48 normal-exit sessions and432 full1280×720 actual
production-EGL frames compare independently with expected guest/native-shell
pixels. Initial sample0 quartets are excluded from cold/warm timing summaries.
The existing parked ownership/CPU/layout/shell/reset/fault transition case remains.

| Nine-frame workload | Incoming exact damage | Capacity replay |
| --- | ---: | ---: |
| Partial: capacity device reads |8|0|
| Partial: private replay checkpoints / pixel applications |0/0|8/32768|
| Partial: final real GetPixel device reads / total reads |1/9|1/1|
| Partial: owning snapshot builds |1|1|
| CPU DIB: pre-guest device reads |17|9|
| CPU DIB: private replay checkpoints / pixel applications |0/0|8/129032|
| CPU DIB: final real GetPixel / total reads |1/18|1/10|
| CPU DIB: imports / total canonical CPU commits |10/10|10/10|
| CPU DIB: owning snapshot builds |9|9|

Tiles keep the previous covered-retirement path: no pressure replay or capacity
readback. CPU DIB operations still synchronize, reimport and rebuild snapshots;
only their8 incidental capacity reads disappear. Masks/actual accepted-write
tracking and all prior GDI/GPU/CPU/fault/lifetime tests stay gates. No noisy timing
threshold or pixel tolerance weakens correctness.

Native test_gpu_capacity_replay checks pending and already submitted commands,
owned copy data after caller mutation, allocator cleanup, actual EGL destruction
after capacity checkpoint, real GL error/incoming-once CPU fallback, over-budget
overdraw retaining readback,4MiB owned-input pressure, rejected allocation with
unchanged old journal, and a clipped incoming source alias captured BEFORE that
checkpoint. Every pixel matches an independent CPU oracle; allocator returns zero.

Negative omission of the capacity GPU flush fails actual device-read pixels,
even though a private CPU replay could look correct. Negative removal of the CPU
work budget fails the required readback/refusal counter gate. Neither is treated
as successful or as physical console evidence.

## Counters, measurement limits and next work

Primitive counters separate capacity events, successful replay checkpoints,
commands/pixel applications, work-budget refusals and failed submission/recovery.
Window bindings aggregate deltas while Fill owns the initiating resource, surviving
retirement; report v13 captures them through shutdown. Capacity target readbacks
exclude real GetPixel/DIB CPU boundaries and other device recovery work. Successful
replay pixel counts exclude the existing separate fallback recovery work.

benchmark_workspace_reuse.py schema3 records four policies/432 raw intervals,
work counts, CPU replay applications, source/PE hashes and host/cache identity.
Refresh includes output; pending target flush can move between prepare/output,
so total raster+Refresh is the comparable interval. Private CPU replay cost is
included. Independent FBO oracle reads, guest sleeps and load/teardown are excluded.
No GPU timestamp, physical scanout or Switch FPS is inferred. Host timing may vary;
fewer device reads alone is not a universal speedup or default-activation gate.

Next: measure actual CPU-import/reseed bytes and cost at supported CPU/DIB
boundaries, then select a bounded transfer optimization with dirty-write/alias/
read/ROP/lifetime/fallback gates. Preserve this checkpoint/reference control and
actual application reads. D compatibility/Pinball, full dynarec, D3D9, installer/
archive and game targets (including Lineage2/Asterios) remain separate work.
Local devkitA64/libnx absent; actual v13/Switch build is CI-only. Physical Paint/
GPU/fallback/input/relaunch remains deferred; SD recovery remains closed.
