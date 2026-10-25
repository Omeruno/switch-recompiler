# G4a: bounded GPU target identities and texture presentation

Incoming `a98e87e` passed all six CI jobs in run 40189429070. This builds on G3's
real GL fill/copy/readback/recovery experiment. G4a remains **isolated from live
Win32 GDI, USER windows and the production Switch presenter**. It does not turn
Paint into a GPU-rendered application or claim physical Switch FPS. Hardware
checks remain deferred at the user's request; development proceeds on host/CI.

## Target ownership and generations

GpuGdiTargets owns up to four G3 images in one dedicated GL context on one native
thread. The combined target pixel limit is 512x512, each dimension at most 512.
These prototype limits permit multiple small windows, not arbitrary desktop
allocations. Pixel budget is charged only after successful creation and released
on destruction. Allocation failure leaves existing targets and accounting intact.

Handles contain a slot and a process-global monotonic generation. Generations
are not reset at manager reinitialization; a deleted target, reused slot or new
manager cannot revive an old handle. Generation exhaustion fails closed instead
of wrapping. These are native GPU handles, not Windows HWND/HBITMAP/HANDLE or
persistent identifiers across host process restarts. All operations validate the
exact handle, including frame submissions. Raw GL texture names are private.
TargetStats/CpuRead pointers are borrowed only until their target is modified or
destroyed; generation checking does not authorize retaining a stale native pointer.

## GPU frame path

Present validates the complete bounded layer array before touching output. It
flushes accepted target commands, then samples the targets' resident textures
into framebuffer 0 with a GL shader. No CpuRead, glReadPixels or CPU frame snapshot
is part of composition. Flushing newly queued drawing copies may upload their
owned input; an unchanged target is never reuploaded from CPU merely to compose
it. Old target commands are not resubmitted on each frame. Moving/scaling/overlapping layers changes only
frame geometry and draw submission. Composition is opaque, nearest-neighbor,
back-to-front over black; clipped rectangles and output sizes up to 1280x720 are
supported. Invalid/stale/foreign handles, invalid dimensions and more than four
layers reject the frame. An empty valid frame clears to black.

Layer coordinates are top-down, while G3 texture row 0 represents logical row 0.
The shader flips the output framebuffer axis once and samples the logical target
rows directly. Output rectangle bounds/size checks bound integer shader mapping
arithmetic; extreme rectangles are rejected before GL calls or output writes.
The manager clears the default framebuffer; it does not preserve unrelated
content outside a smaller viewport. The caller owns the output surface, current
context, context loading, swapping and buffer size. No frame is swapped here.

A false Present must not be swapped: a GL failure can leave partial output.
Validation rejection occurs before output writes. On device error the manager
recovers all target images through G3's CPU checkpoint/journal mechanism and
retires its presenter; GpuActive becomes false. New GPU targets then fail until
explicit reinitialization. Logical Fill/Copy and CPU composition of existing
handles continue on genuine CPU images, rather than reporting simulated GPU work.

This module changes GL state and does not restore an independent renderer's
state. It remains a dedicated-context prototype. Sharing the production present
context or switching threads without a state/lifetime contract is unsupported.

## CPU barriers and recovery

CpuRead explicitly synchronizes one target, preserving G3 semantics. A readback
error propagates to manager-wide fallback, so a mixed GPU/CPU scene is not
silently reported as GPU active. CopyTargetViaCpu is deliberately named: it
validates source/destination handles/rectangles, snapshots source CPU pixels at
an explicit read barrier, then hands an owned input to the destination. It is
not a zero-readback GPU-to-GPU copy. Self-copy is protected by that snapshot.

ComposeCpu validates all layers, allocates a bounded owned output and performs
real target read barriers followed by the same defined opaque scene semantics.
It works after a GL failure or actual context destruction. Its view is borrowed
read-only until next compose/reset; callers must not modify its backing memory.
Output allocation failure does not flush targets or damage a previous buffer.
After actual context destruction the caller notifies ContextLost before other
operations; target replay and cleanup then require no GL calls. CPU operations
can continue, but new GPU targets/presentation are rejected.

Inherited target command/byte capacity checkpoints can still cause readbacks
when drawing, and explicit CPU consumers do so by design. **G4a presentation
avoids per-frame readback; the whole system is not readback-free.** Batch draw
calls, cross-target GPU copies, guest raw-DIB writes, arbitrary ROPs, palettes,
alpha, shaped-window ownership and a live USER/GDI bridge remain separate work.

## Limits and accounting

Four targets each retain G3's 64-command/4 MiB source journal. Combined CPU
checkpoint/scratch pixels are at most 2 MiB; input journals at most 16 MiB. A pending
input plus a cross-target snapshot can conservatively add 2 MiB. The optional CPU
output adds at most 1280x720x4 bytes. Metadata is fixed by four target slots and
four scene layers. Logical GL image allocations are bounded by 1 MiB target pixels
plus four upload images of at most 1 MiB each. Driver storage, shader resources,
window buffers and total process memory are outside these pixel-buffer bounds.
Limits concern accepted logical storage, not transient internal driver copies.

Counters record successful GPU frames, attempted scene draws and GL-error fallback
transitions (failures). Explicit context-loss notification and invalid input/not-
ready rejections do not increment that counter. Per-target draw/upload/readback counters retain G3's attempted-work
semantics. None measures physical FPS or GPU execution duration.

## Reproduction and evidence

```
COMPOSITION_GL_TEST=1 sh tools/run_host_tests.sh
```

Actual EGL/Mesa tests check four patterned, overlapping images, clipped and
scaled scenes, top-down orientation and every pixel including native 1280x720
against an independent 64-bit CPU mapping oracle. Twelve geometry-only frames
require per-target readbacks=0 and uploads=1 after the initial source upload.
The test reads the default FBO externally to compare pixels; those test-only
readbacks are outside the manager and are not a zero-readback benchmark claim.
Explicit CPU composition is separately compared and must record read barriers.

Tests also cover empty output, invalid/extreme/stale frame inputs without output
writes, slot reuse, foreign manager IDs, reinitialization, both target allocation
failures, input/output allocation failures, pixel/slot limits, source mutation
following cross-target copy, target draw failure, presenter failure without
pending drawing, readback-induced manager fallback, CPU continuation and actual
EGL context destruction with flushed and pending work. An injected allocator
requires all module allocations released. Ordinary non-GL host initialization
fails explicitly; no simulated success is used to pass a GPU oracle.

The existing gpu-reference CI job independently compiles/runs the same oracle
at O2 with strict warnings/no exceptions/no RTTI; host/MinGW/Mesa suite includes
it too. CI still has six jobs. The host job publishes stage-gpu-gdi-targets with
the successful log and this contract; old logs are removed at suite startup.
Mesa software rendering verifies GL semantics on host, not Switch GPU timing.
Local devkitA64/libnx remains absent; the Switch CI compiles the GL source.

## Next gate

G4b needs bounded batching and a live-window/raw-DIB/ROP coherence contract,
then a texture-aware production presenter bridge that can avoid the existing
ShellSession canonical-CPU memcpy. Target IDs here are not HWND bindings yet.
Production activation requires actual-x86 GDI/window/pixel/lifecycle regressions
and measured transfer/checkpoint cost, preserving CPU fallback, input and Luna.
No hardware test request or release acceptance follows from this package.
