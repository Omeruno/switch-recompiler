# G4b.2d.1: owning resident GPU resource frames

Incoming verified source: `7edadd4d4ec9d4ab776a1def5fdf59297bd1a3fc`,
[CI40335457694, six jobs](https://github.com/Omeruno/switch-recompiler/actions/runs/40335457694).
This is the resource-lifetime prerequisite for the remaining production bridge,
not ordinary GDI activation or a replacement for ShellSession composition.

## Contract

`GpuGdiTargets::PrepareFrame` copies bounded layer geometry and takes native
resource pins. It validates all input before replacing the caller's old frame;
rejection preserves that frame. Preparation allocates no pixel buffers, flushes
no commands and makes no GL calls. Frames are copyable native resource references,
not immutable pixel snapshots: subsequent accepted writes to a still-live target
are visible when the frame is submitted. They contain no HWND/DC/guest pointers
or public GL texture names. Empty prepared frames clear to black; default,
foreign-manager and pre-reset frames reject before output writes.

`Destroy` immediately rejects further handle-based writes/reads/frame preparation,
but a prepared frame can still submit the retired image. Retired images remain
charged against the same four-slot / combined512x512-pixel limits. The manager
collects them on its owner thread at Destroy/Create once the last frame releases
its pin. A frame destructor only drops native reference counts: it does not call
GL or keep the manager alive. Metadata uses one shared pin per target plus fixed
four-layer storage per caller-owned frame; the number of frame copies is the
caller's responsibility. No extra image clone is allocated for a frame.

Shutdown destroys all resources on the context owner thread and revokes all old
frames, even if the caller retains copies. Reinitialization never revives them.
ContextLost must still follow actual context destruction before cleanup or other
operations. It recovers both live and retired images without GL calls; an owned
frame can then ComposeCpu from real checkpoint/journal replay. A GL presentation
error disables GPU use manager-wide and follows the same genuine CPU fallback.
A failed GPU frame must not be swapped. CPU composition remains an explicit read
barrier; the returned surface is borrowed until next composition/reset.

As elsewhere in G3/G4, preparation/submission/target mutation and manager cleanup
require the same native owner thread/current GL context. This is neither an
asynchronous multi-thread submission queue nor a fence for CPU-mapped storage.
OpenGL retains resources used by already-issued commands; frame pins protect
resources between preparation and submission. Pending frames see live image
contents by contract, rather than pretending to own historical content versions.

## Evidence

The existing public actual-EGL target oracle now checks:

- twelve prepared-frame presentations with every output pixel compared against
  independent expected colors, zero target readbacks/uploads for fill-only work;
- copied geometry, live-content semantics, frame copies, retirement and immediate
  rejection of stale handle operations, with full pixel/slot budget held;
- rejected preparation preserving the previous valid frame, foreign/default/empty
  frames, manager shutdown/destruction and reset revocation;
- last-pin release allowing owner-thread collection/reuse without a frame-side
  GL destructor; module allocator live count returns to zero;
- presentation error followed by genuine CPU composition of retired resources;
- a separate actual EGL context destruction with both previously submitted and
  pending writes on a retired pinned image, then all-pixel CPU replay comparison.

The existing live-target context-loss/continued-CPU-write test is retained.
Negative control removes only the retirement pin check: it fails at the retained
pixel-budget assertion. Strict O2 and ASan/UBSan targeted checks pass (driver leak
scan disabled; injected module allocator independently requires full cleanup).
The full COMPOSITION_GL_TEST=1 host suite and exact published-SHA six-job CI
remain package gates. The existing GPU-target log artifact includes these tests.

## Next

[G4b.2d.2](G4B2D2_RESIDENT_PRESENTATION.md) now implements the explicit
resident-layer entry in the production renderer. The remaining complete mixed
frame and shell/window integration is described there.

Original next-gate scope:

Integrate these resources with a texture-aware production frame that preserves
window ownership, native Luna/overlays, cursor and fallback without the current
unconditional canonical CPU snapshot. Audit raw host surface accesses and route
coherence errors into controlled frontend shutdown before selective ordinary GDI
admission. Window bindings still do not export a production frame here. Current
host Mesa evidence is neither physical Switch performance nor PC-game acceptance.
Local devkitA64/libnx remains unavailable; CI builds NRO. Hardware E/F tests remain
deferred, and the CPU GDI/default rendering path is unchanged by this package.
