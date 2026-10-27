# G4b.2m — measured CPU reseeding and bounded differential import

Implemented native experiment, 2026-10-27, after
[capacity checkpoint replay](G4B2L_CAPACITY_REPLAY.md). Default CPU GDI, L/ZL,
Luna, real application reads and physical acceptance policy remain unchanged.

## Measurement before the optimization

The incoming four-policy public x86/native-shell inspector was first instrumented
and run unchanged:48 normal-exit sessions/432 actual EGL frames. The nine-frame
DIB workload imported10 whole128×128 images, submitting655360 bytes. A real CPU
read before each4×4 DIB blit remains necessary; the subsequent import need not
upload every unchanged pixel. This selected a common CPU→GPU transfer change,
not an application-name shortcut or removal of required CPU synchronization.

Prototype counters record attempted upload/readback payload bytes, accepted full
CPU input pixels, comparison/packing pixels and unchanged accepted imports.
Bindings aggregate seed attempts, full snapshot wall nanoseconds, import wall
nanoseconds, attempted upload bytes, comparison pixels and unchanged imports
before resource retirement. Report v14 captures the aggregate through shutdown.
Time uses steady_clock only around native seed phases, never per guest opcode;
import includes CPU scan/pack/allocation/checkpoint work and driver submission.
Counts do not represent physical DMA, GPU execution duration or scanout.

## Bounded exchange and correctness

A prototype-native same-build control selects differential import. Its default is
off; the already explicit native solid-dispatch experiment enables it. A healthy
current-USER control on WindowGpuBindings can select the original full import.
The manager propagates policy to existing/new targets and resets it on Shutdown.
No extra flag or default product activation is introduced.

Import still accepts exactly one full CPU image, rejects nonempty journals and
malformed input before scanning, and performs its first import in full. Later
healthy-GPU imports compare normalized24-bit RGB with the verified private CPU
checkpoint. Side512/combined512²/four-target budgets bound this full scan. No
CPU read/write intent, page protection or arbitrary raw-pointer interception is
assumed. SnapshotWindow still copies all visible CPU pixels and zeroes unknown
occluded pixels; all CPU/ROP/DIB/layout/lifetime barriers remain in force.

If no pixels changed, accept the logical import without a new upload/draw. A
later explicit CpuRead still reads the real device. Otherwise find ONE bounding
rectangle containing every changed pixel, pack its complete rows into existing
private scratch, then use the existing owned Copy/Flush path. Unchanged pixels
inside the rectangle are copied faithfully; outside it compare equal. Full-image
changes retain full-image transfer. This is not dirty-mask coarsening: no new
canonical/retained/capture ownership is written by this private import.

Copy owns the packed rectangle before submission and before any recovery changes
checkpoint storage. Caller bytes may mutate immediately after return; a borrowed
CpuRead checkpoint alias is safe under its existing synchronous read-only
contract. Allocation refusal accepts nothing and leaves checkpoint/GPU/journal
unchanged (comparison/packing attempt counters may advance). GL error recovers
checkpoint+owned command exactly once and remains explicit CPU fallback. A
successful upload updates the same rectangle of the private checkpoint before
freeing its input, so actual later context loss can replay a pending suffix.
Existing GdiGlScope preserves hostile PBO/pixel-store/raster caller state.

No new pixel allocation is introduced: preallocated scratch packs at most512²
pixels; the existing temporary owned Copy input is at most1MiB. No retained guest
pointer, asynchronous transfer, new GL API or new driver resource is required.
Dense changes may add a full scan/pack without reducing transfer. That CPU cost
is measured, not hidden behind a universal speedup claim.

## Public evidence and controls

The final inspector rotates five policies in one build and the same public PEs:
historical Region/full snapshots, retained/Region, retained/exact damage,
capacity replay, then differential import.60 normal-exit0 sessions/540 full
1280×720 production-EGL frames compare every output pixel with an independent
expected guest/native-shell frame. The parked two-window transition oracle is
retained separately and drained; it is not counted as a normal exit.

| Nine-frame DIB workload | Capacity replay | Differential import |
| --- | ---: | ---: |
| Full CPU input pixels / logical imports |163840 /10|163840 /10|
| Attempted import upload bytes |655360|66112|
| Comparison pixels / packed pixels |0 /0|147456 /144|
| Required pre-final device reads / final real read |9 /1|9 /1|
| Capacity replay checkpoints / pixel applications |8 /129032|8 /129032|
| Canonical CPU commits / owning frame snapshots |10 /9|10 /9|

66112 bytes = one initial16384-pixel image plus nine16-pixel rectangles, all at
four bytes/pixel. Full CPU snapshot work is NOT removed. Tiles/partial retain
one import, their prior replay/read/ownership counts and exact EGL pixels.
Schema4 records all540 raw intervals, five policies, full input/scan/pack/upload
counts and cumulative snapshot/import wall time, plus source/fixture/host/cache
identity. Sample0 groups are excluded from timing summaries. Refresh includes
output; total raster+Refresh is comparable when target flush moves phases.
Independent FBO oracle read/sleep/load/teardown remain outside measured intervals.
Local focused DIB seed-import medians were about52→29us; whole-cycle medians
were about5102→5849us with broad overlapping variation and concurrent validation
load. Reduced transfer does not establish an end-to-end speedup. No noisy timing
pass threshold or physical Switch/FPS claim is attached.

The standalone actual-EGL oracle covers reference/differential,1×1/31×23/512×512,
nonempty-journal rejection, unchanged/high-byte-only input, checkpoint alias,
separated changes/unchanged rectangle interior, single-pixel nonzero offsets,
owned source after caller mutation, allocation refusal without accepted changes,
GL error and actual context destruction after successful partial import plus
pending fill. Injected allocator storage returns to zero. Hostile unpack PBO,
alignment/row/skip/swap state and viewport/scissor are preserved. Negative packing
with a missing source x-offset fails actual GPU pixels; skipping the partial CPU
checkpoint update fails genuine context-loss recovery pixels.

The complete host/MinGW/Mesa suite remains the publication gate, retaining all
prior read/ROP/raw-DIB/alias/visibility/capture/session/state/recovery/budget tests.
Existing stage-gpu-window-coherence artifact gains the new native log/contract;
six core jobs remain. Strict and focused sanitizer checks supplement that suite.
Local devkitA64/libnx is absent; v14/Switch compilation is CI-only. Physical
Paint/GPU/fallback/input/relaunch is deferred; SD diagnosis remains closed.

## Next

Consolidate G4b.2 software acceptance and compare full CPU versus native complete
public workload cycles, including admission refusals and CPU/DIB boundary costs.
Use that evidence to prioritize the next interpreter/dynarec or graphics block;
default activation and physical performance remain open. D/Pinball, full ARM64
dynarec, D3D9, installer/archive and game compatibility are separate milestones.
