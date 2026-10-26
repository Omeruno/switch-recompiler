# G4b.2c: opt-in CPU/GPU window authority and real Win32 barriers

Date: 2026-10-26. Incoming `e780649e18c1b25bd38ea05db157074155a22bce` passed
[CI40221359571, six jobs](https://github.com/Omeruno/switch-recompiler/actions/runs/40221359571).
This extends [native HWND/DC bindings](G4B2B_WINDOW_BINDINGS.md).

**Ordinary application GDI is still the CPU reference path.** EnableCoherence is
an explicit native experiment, not automatic GPU dispatch, an EXE-name switch,
a new default or a physical performance result. The production shader-shutdown
fix described below also applies independently of that opt-in mode.

## Authority and transfers

In coherent mode a slot records whether CPU pixels may have changed and the
bounded union of GPU-written window-local dirty rectangles. Import is allowed
only when no GPU-dirty pixels remain. Native Fill itself does not repeat its
raster work on the canonical CPU screen. Before a CPU consumer runs, every dirty
window is read/recovered and committed to the real CPU screen and retained planes.

`GpuGdiPrototype::ImportCpu` accepts a full owned CPU image only with an empty
journal. It uploads/draws the snapshot and establishes the CPU checkpoint from
that known input, with no glReadPixels. The source may be mutated/freed after
return. A pending-command replacement, bad size or allocation failure rejects
without accepting a new image. A GL failure uses the existing genuine CPU replay
and is distinguished by Backend/GpuActive; logical acceptance is not GPU success.
The additional cpuImports counter records accepted CPU checkpoints, including
CPU fallback, while uploads/draws/readbacks retain actual attempted-GL meanings.

SnapshotWindow copies current whole-window *visible* CPU pixels; unknown occluded
areas are zero in the private target. Operations remain clipped to actual visible,
paint and selected DC regions. Guest DIB/memory DC storage remains CPU-authoritative
and is never silently aliased to a GPU image. Arbitrary bitmap/Direct3D resources,
ROP, palettes and raw guest stores into GPU-owned memory are not implemented.

CheckpointCpu validates window identity, layout epoch, geometry, source image and
all dirty bounds/visibility before committing that window. It does not depend on
a DC's current selected clip, so shrinking a DC clip cannot erase earlier accepted
writes. CommitWindow uses the existing write-capture path: canonical pixels,
window-local pixels, ownership and tile revisions all receive the actual changes.
It never copies the whole GPU image over another window or unmodified screen area.

CPU access marks existing images for conservative reimport before their next GPU
operation. Repeated clean CPU reads add no GPU readback; repeated native GPU fills
without an intervening CPU operation add no reimport. Dirty storage is bounded to
128 rectangles per window; exceeding it first checkpoints accepted work.

## Opt-in barriers and lifetime

The current USER session and GL context must belong to one native owner thread;
there is no yield/callback/context switch inside the native exchange. Enable this
mode before acquiring images, and detach/flush before the owning USER session is
discarded. No native Window/DC/Surface pointer is retained across an exchange.

The optional Gdi CpuRasterGate is weakly owned and moves with the session-owned
Gdi store. Nested GuestSession swaps therefore suspend/restore the outer gate
instead of applying it to an unrelated screen. An expired installed gate fails
closed; it never invokes a destroyed owner. Multiple simultaneous gates on the
same Gdi store are rejected.

Implemented CPU boundaries:

- Gdi::Target before CPU screen/window/memory raster access, including GetPixel,
  CPU ROP and BitBlt source/destination; DeleteDc before DC removal.
- Real USER create/destroy/position/show/activation, style/parent changes and
  retained-layer reconfiguration before mutation; desktop repaint before writes.
- PresentIfNeeded before changed-frame selection, and ShellSession::Compose
  before canonical CPU copy. A failed composition gate retains the previous output.

DescribeWindowTarget uses geometry-only ResolveTarget, so validating a native GPU
lease does not accidentally trigger CPU readback or break a pending GPU batch.
SnapshotWindow/CommitWindow are internal authority-exchange helpers and bypass
the gate to avoid recursively consuming their own work.

Unmediated host Surface reads/writes must explicitly acquire CPU access. This is
not an interceptor for arbitrary pointers or memcpy. Audit remaining direct host
consumers, fault reporting and owning frame lifetimes before production activation.
Unknown layout/lifetime violations latch a native coherence fault and reject
unsafe accesses rather than overwriting a replacement window. Removing an owner
without a successful detach leaves an expired gate which blocks CPU access.

Move/resize/hide/destroy checkpoints accepted pixels before the existing USER
invalidation/repaint semantics. A resized target is retired and its next seed
comes from the CPU state. This does not promise preserving an application bitmap
without the repaint required by the existing Win32 window model.

On GL draw/read failure, actual checkpoint/journal replay supplies pixels before
CPU commit and manager-wide GpuActive becomes false. After actual context loss,
notify ContextLost first; checkpointing and existing native-lease CPU continuation
then make no GL calls. New GPU acquisition fails until explicit reinitialization.

CPU output intentionally checkpoints GPU data. It is a correctness path, not the
texture-aware optimization: retaining ShellSession's full CPU read every frame
cannot be reported as a production GPU speedup. Owning resident-texture frames and
selective ordinary GDI dispatch remain the next gate.

## Resource bounds and work accounting

Existing limits remain four images, side<=512, combined512x512 pixels. Coherence
adds packed seed buffers with a combined limit of1MiB, bounded dirty rectangles
and one small weak gate. A transient owned import snapshot adds up to1MiB on top
of the inherited target/checkpoint/journal budgets. Standard metadata/driver
allocations and caller-owned lease copies are outside these pixel-buffer bounds.
GPU admission still rejects unsupported sizes/mapping/layout/clip complexity.

CoherenceCounters records accepted imports/importPixels, successful window
commits/commitPixels and attempted CPU gates. These are work counts, not timers.
For the first window's public64-fill group the oracle requires:

- one full CPU import: one upload/draw, with no readback for the import;
- 64 disjoint fills: one additional instanced fill draw;
- first ordinary x86 GetPixel: one readback and window commit;
- subsequent reads: no additional target readbacks.

A second dirty window must be committed by the same CPU boundary. The inspector
later deliberately reads a private GPU image to verify imported CPU ROP/DIB data;
that explicit extra read and the following commit are included in its counters.
The default-FBO pixel oracle also performs test-only readback. None of these
counts claims zero GPU synchronization or measures physical FPS.

## Real x86/EGL evidence and renderer fix

`tests/guest/gpu_window_coherence.c` is a public CRT-free PE32 fixture. Native test
commands inject GPU changes, then the unmodified guest consumes them with actual
GetPixel, destination ROP, window-to-DIB BitBlt, raw guest DIB writes/source blit,
GetDIBits and layout/lifetime APIs. Ten stages run in each of two fresh sessions:
injected GL error and actual EGL context destruction with pending writes.

Independent expected colors, both dirty windows, all1280x720 CPU/output pixels
and the real production EGL renderer are checked after the first handoff. Seed
and owned-upload allocation failures must leave CPU pixels and GPU work unchanged;
the injected allocator requires all module allocations freed. Tests also cover
pending-journal import rejection, source ownership, nested sessions and expired gates.

This revealed a production lifecycle bug: OpenGlPresentation::Shutdown deleted a
still-current program, leaving a pending-deletion binding. Switching to GPU-GDI
then destroyed that program before the scope could restore it, causing an error
and unintended CPU fallback. Shutdown now unbinds its own current program before
deletion and preserves another program if that is current. The oracle requires
GL_CURRENT_PROGRAM=0 after owned shader shutdown and a continued real GPU path.

Negative controls independently remove the CPU Target gate (the guest's first
GetPixel check fails) and restore the old shader cleanup (the deletion-state
assertion fails). Earlier failing development runs are not success evidence.

```sh
COMPOSITION_GL_TEST=1 sh tools/run_host_tests.sh
```

Host CI publishes `stage-gpu-window-coherence`: bounded stage counters, test log,
public EXE and this contract. Core CI remains six jobs; previous x86/CPU/EGL/
ARM64/QEMU/session/file tests remain gates. Mesa is software GL on host, not a
Switch GPU measurement. Local devkitA64/libnx remains unavailable; CI builds NRO.
Current physical E/F/Paint/input/fallback/relaunch checks remain deferred.

## Next gate

Own GPU frame resources through presentation and fallback, avoid unconditional
canonical CPU snapshots, audit remaining raw host accesses and propagate native
coherence faults into controlled frontend shutdown. Then admit a measured bounded
ordinary GDI subset with public x86/EGL differential tests and explicit budgets.
This does not implement a dynarec, Direct3D or certify the planned PC games in
[TARGET_GAMES_RU.md](TARGET_GAMES_RU.md).
