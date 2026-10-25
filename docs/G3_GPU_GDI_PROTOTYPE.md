# G3: isolated GL GDI target, readback and bounded recovery prototype

This follows G2's measured CPU SRCCOPY and the preserved Claude Luna update.
Luna integration `b7cb82d` passed all six jobs in CI 40188452904. User hardware
checks remain deferred. This module is **not selected by production GDI or the
Switch compositor**. It is a real, limited OpenGL implementation exercised with
an actual EGL context, not a claim that Paint now rasterizes on Switch GPU.
Host EGL/Mesa tests use the software GL driver; they certify GL/pixel semantics,
not physical hardware speed. The Switch CI compiles the same GL implementation.

## Supported operations and ownership

`GpuGdiPrototype` owns one opaque 32-bit target, up to 512 by 512 pixels. It accepts
clipped solid fills and 1:1 SRCCOPY from an explicit packed RGB32 source rectangle.
GLSL draws into a texture/FBO; source uploads are separate textures, never a
feedback loop sampling the attached target. Logical row 0 is texture row 0;
readback uses that same convention, with no display/swap/scanout interpretation.
Unused input high bytes are ignored and CPU checkpoint pixels normalize to 24 bits.
No live guest memory or mutable input pointer is retained: Copy snapshots its
input before accepting the operation, including before a capacity checkpoint.

Copy requires nonnull input, exact rectangle pixel count, positive dimensions
no larger than 512, and checked 64-bit dimension differences. Malformed inputs
return false before a new pixel operation. Fully clipped valid inputs are no-ops.
There is no scaling, palette/mono conversion, key/alpha blend, arbitrary ROP,
GPU-to-GPU target copy, overlap semantics, window DC transform or multi-target
support. This is not a Win32 API replacement and cannot accept arbitrary GDI.

The native caller owns one dedicated current GL 3.3 context, loaded GL functions,
its native thread and lifetime. The prototype changes GL state and does not save
or restore another renderer's state. It is intentionally not attached to the
existing presentation context. Shutdown/reinitialization requires that context
still be current if device objects exist. Initialize is destructive; failure
leaves an empty object and explicitly reports false. Ordinary host builds
without GL support return unavailable rather than reporting simulated GPU success.

## CPU read barriers, journal and fallback

Accepted commands are recorded in an owned journal, including commands already
submitted to GL. Maximum 64 commands and 4 MiB of source snapshots bound the
journal. The CPU checkpoint and separate readback scratch each need at most 1 MiB;
one candidate source snapshot can temporarily add another 1 MiB before capacity
checkpointing. Thus peak module-owned CPU pixel buffers are at most 7 MiB plus
fixed metadata. Two GL RGBA8 images need at most 2 MiB logical storage; driver
copies, allocations and total process memory are outside those bounds.

At a queue/byte limit the module performs a real checkpoint before accepting
more commands. CpuRead is also an explicit barrier: flush accepted commands,
read the FBO into scratch, check GL outcome, then commit the CPU checkpoint and
retire the journal. It never overwrites the valid recovery checkpoint with a
failed/partial readback. These readbacks are synchronous and may stall; they
are appropriate to this isolated correctness experiment, not added to the
production frame loop. No per-pixel GetPixel/readback speedup is claimed.

A draw/readback GL error replays all journal operations over the last verified
CPU checkpoint, including flushed and unflushed work, then disables the device.
FlushGpu explicitly returns false; Backend reports CpuFallback. Later Fill/Copy
can perform genuine CPU operations, and CpuRead returns their real pixels.
Successful Fill/Copy means logical operation acceptance, not proof of GPU execution.
The mode and counters distinguish these outcomes.

After actual context destruction/loss the caller must notify ContextLost before
other methods. It performs CPU replay and forgets dead object names with **no GL
calls**. The destroyed context owns driver cleanup. The test really destroys the
EGL context, recovers both flushed fill and pending copy, then destroys the
prototype with no context. This is not a device-loss detector or proof that all
Horizon driver failures are recoverable. External unsignaled context loss and
sharing its state concurrently violate this prototype's caller contract.

CpuRead returns a borrowed read-only view, valid until the next operation/reset;
callers must not mutate its backing pixels. No guest address is mapped to it.
Allocation failures on command input preserve prior logical work and reject the
new command. Initialization allocation failures clean up and return false.

## Reproduction and evidence

```
COMPOSITION_GL_TEST=1 sh tools/run_host_tests.sh
```

The runner compiles the actual GL implementation separately from ordinary host
stubs and executes `test_gpu_gdi_prototype` with EGL/Mesa. It compares every
pixel at 1x1, 31x23 and 512x512 against the existing CPU GDI, exercises clipped
fills/copies and input mutation, repeated flush/readback cycles, command and byte
capacity checkpoints, malformed inputs, both initialization allocation failures,
command allocation failure with prior pending work, draw error, readback error,
CPU continuation and real context destruction. It checks actual GPU mode before
failure, draw/readback attempts and successful checkpoint counts, journal bounds and owned
allocator cleanup. No fallback-only pass can certify the GL comparison.

A native unavailable-backend test verifies false initialization/no fake rendering
in the normal host build. The existing gpu-reference CI job independently runs
the actual prototype at O2 with strict warnings/no exceptions/no RTTI. The host
job uploads `stage-gpu-gdi-prototype` with its successful log and this contract.
CI still has six jobs. Stale prototype logs are removed at full-suite startup.
Full F, G1/G2, native/guest workspace and actual-GL composition gates remain
required; no user EXE/assets are committed.

## Next activation boundary

Production activation is a separate G4 task. It needs a bounded multi-target/
window identity contract, command batching, GetPixel/GetDIBits/CPU-ROP and raw
DIB write barriers, owned source snapshots and measured transfer/readback cost.
ShellSession currently composes canonical CPU pixels, so hooking only Gdi::Fill
would introduce full-frame readback or duplicate CPU/GPU raster work. A texture-
aware presentation bridge and checked software fallback must be designed before
switching this prototype on. No DirectDraw/D3D, dynarec, physical acceptance,
full GPU GDI or FPS improvement follows from this package alone.


Counters `draws`, `uploads` and `readbacks` count attempted API work, including
attempts that later fail. `checkpoints` counts successful CPU commits and
`recoveries` counts fallback transitions. None measures GPU execution time. An
additional alias test borrows CPU checkpoint bytes, queues 64 later fills, then
uses the old bytes in Copy while capacity checkpointing changes that backing
store; the originally borrowed input is preserved by pre-checkpoint snapshotting.
