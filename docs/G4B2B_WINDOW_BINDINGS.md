# G4b.2b: native GPU resources bound to real HWND/DC lifetimes

Incoming `1d3f15588f51745c92292d073c686c2591f81a85` passed all six jobs in
[CI40204271361](https://github.com/Omeruno/switch-recompiler/actions/runs/40204271361).
This implements the bounded identity/clip/resource-lifetime slice after
[G4b.2a's shared-context handoff](G4B2_SHARED_CONTEXT.md).

**Ordinary Win32 GDI remains CPU-authoritative.** WindowGpuBindings is an explicit
native bridge driven by real USER/GDI metadata, not the selected production
raster backend. Its Fill/CpuRead are diagnostic commands; they do not pretend to
update the canonical CPU screen, retained window images or saved application
pixels. There is no EXE-name special case or game/graphics-API compatibility claim.

## Identity and read-only descriptions

Actual USER windows and GDI DC objects now carry process-global monotonic native
64-bit identities. They are not guest HWND/HDC values, guest-visible ABI changes,
persistent IDs or raw GL names. Fresh sessions, reused object/native storage and
reused guest handles do not recycle these identities. Exhaustion yields zero and
disables GPU eligibility; it does not wrap. Manual/custom untracked providers
remain outside the nonzero native identity contract.

A window DC captures its window identity at creation. The real DcProvider checks
that identity **before** clipping/capture allocation. The ordinary CPU Target
path also checks the resulting identity. An old DC cannot draw into a replacement
window which acquired the same numeric HWND. This closes a genuine alias hazard
independently of GPU activation. Creation's identity-only provider query does not
construct visible regions or allocate retained raster planes.

`Gdi::DescribeWindowTarget` returns an owned synchronous value: exact window/DC
identities, numeric handles, layout generation, full window bounds, client/whole-
window origin, viewport/window origins and effective visible/paint/selected clip.
It rejects stale/untracked DCs, screen/memory/information DCs, non-MM_TEXT maps and
nonzero layouts. The query never captures or copies pixels. No guest/window/DC/
Surface pointer is retained by the description or resource bridge.

## Bounded native resource bridge

WindowGpuBindings owns one GpuGdiTargets manager. The native caller owns both the
current USER session and current GL context/thread; no concurrent USER mutation,
callback/yield or context switch is allowed inside a synchronous bridge call.
The shared-context GL contract still applies. No GPU deletion is hooked to a
potentially different window-callback thread: retirement occurs in Prune/Acquire
or explicit owner-thread shutdown.

- One resource per native window identity; several DCs for the same window share
  it while their leases retain distinct DC identities and clip/origin state.
- Existing prototype bounds remain: four images, dimensions at most512 each,
  combined512x512 pixels. At most128 clip rectangles per acquired lease. Unsupported
  windows/contexts or allocation/budget failure return false with an empty lease;
  existing CPU GDI remains available.
- Every native command revalidates the current DC/window identities, target handle,
  layout generation, geometry, mapping origins and effective clip. Released DC,
  changed clip/mapping/layout, old resize/hidden geometry, foreign or reset resource
  handles fail before command submission. Returning to the same numeric handle
  does not validate an old identity.
- Move/hide/show can retain the image but require a fresh lease. Resize retires the
  old image; a newly acquired target starts black. This **does not implement**
  preserving dirty GPU pixels across a production resize/repaint.
- Prune retires destroyed/replaced/resized window resources on the owning thread,
  including after guest exit/abort/session replacement. Call it at lifecycle/frame
  boundaries when this bridge is used; it is not an asynchronous observer.

Native Fill maps a logical solid RGB rectangle through supported DC origins,
checks translations with64-bit arithmetic before narrowing, intersects the real
visible/paint/selected clip and window bounds, and queues window-local fills.
It does not interpret arbitrary ROP, brush, font, palette or DIB semantics.
CpuRead is an explicit synchronous target barrier; returned images/counters are
borrowed only until the next relevant operation, retirement or shutdown.
There is deliberately no production-frame/Present API in this bridge yet.

After actual context destruction call ContextLost before other GPU work. Existing
validated targets can recover/read/continue their explicit native fills on CPU;
new GPU acquisition fails. Prune/cleanup then makes no GL calls. These private
fallback pixels are not automatically committed to the application CPU screen.

## Public actual-x86 and EGL proof

`tests/guest/gpu_window_bindings.c` is a redistributable CRT-free PE32 fixture.
It calls actual RegisterClass/CreateWindow/GetDC/GetWindowDC/ReleaseDC, clipping/
viewport, MoveWindow, hide/show and destruction APIs. The inspector runs20 stages
across two fresh GuestSessions with reused numeric HWND/HDC values, plus a third
controlled-abort session which really destroys the EGL context while a window
and pending native GPU command remain alive.

The host deliberately reuses the freed HWND allocator slot at a stable boundary;
that is an ABA regression control, not a runtime program-specific workaround.
It requires the old window DC's ordinary CPU Target/SetPixel to fail without
capturing a new window plane. With the lifetime guard removed in a negative
control, the test fails at exactly that stale-DC Target assertion.

Native bound commands must use a real GPU backend before loss and compare every
pixel with independent expected clipping/color formulas. Tests check shared
window resources/distinct DCs, changed clip/viewport, occlusion, move/resize,
hide/show, unsupported mapping/size, retirement, context loss and abort cleanup.
All canonical CPU screen pixels remain unchanged by diagnostic commands. The
ordinary GDI composition/lifecycle tests separately retain production CPU/GL
output and last-frame ownership acceptance.

```sh
COMPOSITION_GL_TEST=1 sh tools/run_host_tests.sh
```

The existing host CI job runs the mandatory EGL/x86 harness and publishes
`stage-gpu-window-bindings`: log, generated public EXE and this contract. Core CI
still has six jobs. The test setup explicitly enables new USER/GDI; an initial
missing EnableUi selected legacy imports and failed before exercising bindings.
The correction is in the harness, not a RegisterClass stub or compatibility hack.

## Remaining gate

Next define/implement CPU↔GPU authority, raw DIB writes, source/ROP/read/save barriers,
resize/repaint checkpointing and fallback transfer. Then integrate owning frames/
textures with the production presenter without unconditional ShellSession CPU
memcpy/duplicate raster. Public actual-x86 dispatch/pixel/lifecycle fixtures must
precede automatic activation. Native lease ownership is not production frame
ownership. Larger image/working-set budgets also need explicit implementation
and evidence before relaxing prototype limits.

PC-game goals remain in [TARGET_GAMES_RU.md](TARGET_GAMES_RU.md); this is not a
Direct3D backend or ARM64 dynarec. No physical Switch/FPS result is claimed.
Local devkitA64/libnx is unavailable; CI builds the NRO, hardware checks deferred.
