# Stage E4 — resource lifecycle

E3 is functionally accepted on Switch at c5e1871; exact-SHA CI run
39847105724 passed all five jobs. Stage E remains at 50% while E4 is in work.

## Implemented boundary

Presentation now supplies a process-wide monotonic resource identity plus its
existing generation in each synchronous frame. Software output histories match
both, in addition to source owner, geometry, output address and output generation.
Recreating a Presentation at the same address cannot reuse its previous identity.
The backend remains borrowed: it must outlive calls; Presentation destruction
never calls an already-destroyed backend. Explicit session Detach releases both
snapshot/tracking storage and backend caches before the borrowed backend expires.

A failed backend render may already have overwritten output pixels and updated
its dirty histories. Presentation retains its CPU snapshot, advances the resource
generation and resets the backend. A retry performs a full rebuild. Native output
also clears its successful-frame suppression flag on failure. Invalid target
validation before calling a backend leaves the previous output history intact.

The actual RunInterpreter scope now uses the shared PresentationSession helper.
It is constructed before GuestExecution, so guest continuations drain before
EndPresentation releases memory and restores all previous platform callbacks.
Loader/early return also runs the scope. This preserves existing ordering while
making it executable in host tests. Fatal platform termination which bypasses
C++ destructors is outside the scope's guarantee.

Native startup now checks framebufferCreate, framebufferMakeLinear and
threadStart results. It does not begin rendering after failed allocation or
start; successfully created framebuffer/thread resources are closed on those
failure paths. framebufferBegin/End remain synchronous paired operations, using
libnx's preserved linear shadow buffer. No asynchronous upload queue or GPU
cancellation/fence implementation is claimed.

## Validation

- test_presentation_lifecycle injects a failure *after* the software backend
  updates its history and corrupts the output. Retry must restore the complete
  expected frame. It also exercises backend switching, placement-new address
  reuse, 150 resize/clear cycles and zero retained caches after Detach.
- test_execution_stop now keeps a rendered snapshot and backend cache alive
  while actual GuestExecution continuations stop through host abort or the HLE
  ExitProcess path. Native StopGuard destructors assert the renderer is still
  alive during draining; afterwards the shared session must have released all
  storage and restored the original platform present callback.
- Existing dirty/full pixel, real x86 GUI, callback exit, paced input and Stage C
  suites remain required. Target lifecycle test passes optimized and ASan+UBSan
  builds (LSan disabled in the traced environment).

## Remaining E4 work before 60%

Snapshot/cache vectors are bounded by dimensions and MaxFrameBytes, but allocation
failure under -fno-exceptions is not yet recoverable. Provide a presentation-local
allocation strategy and deterministic fault injection before claiming the full
resource budget/failure criterion. Add acquired-frame cancellation only when the
actual output API supports cancellation; libnx framebufferEnd queues the buffer,
so an invented abort operation would not satisfy that criterion. GPU uploads and
fences belong with the real E5 backend. This package does not close E4 by volume
of code or claim hardware GPU acceleration.
