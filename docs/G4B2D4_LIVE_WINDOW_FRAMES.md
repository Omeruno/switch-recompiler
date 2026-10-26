# G4b.2d.4: live coherent windows into an owned mixed frame

Incoming verified `8dc667cf246c766cf2a3754c4b68ca80e5577e41`,
[CI40403499293, six jobs](https://github.com/Omeruno/switch-recompiler/actions/runs/40403499293).
Extends [owned mixed presentation](G4B2D3_MIXED_FRAME.md) and
[window authority](G4B2C_CPU_COHERENCE.md). Explicit native coherent entry only;
ShellSession/frontend and ordinary Win32 GDI are not switched by this package.

## Building a live frame

WindowGpuBindings::SubmitMixed requires its current coherent USER session and a
healthy authority gate. It builds a private ownership mask from actual USER whole-
window visible regions for every dirty GPU slot. Gdi::DescribeWindowImage validates
native window identity, global layout generation, exact bounds and bounded visible
rectangles without a CPU coherence barrier, pixel read or retained capture setup.
It never trusts an old numeric HWND/DC or caller-supplied rectangle as visibility.
A missed layout/lifetime boundary latches a coherence fault and rejects output;
a different current guest session rejects without corrupting the suspended owner.

A seeded dirty slot owns all currently visible pixels of that window, not just
its most recent DC paint/selected clip. Its full visible seed plus accepted GPU
commands provides those pixels. Occluded seed pixels are unknown/zero and are
never selected. Unbound/clean windows and exposed desktop remain in the ordinary
CPU base. No retained CPU window-layer pointer or global WindowLayers token is
needed. Dirty GPU pixels can remain stale in the CPU base underneath resident
owners: submission does not call CheckpointCpu or build a complete CPU composite.
It still copies the CPU base and ownership for a new owned snapshot; no eliminated
CPU memory-traffic/FPS claim is made for the complete frontend.

The builder uses fixed four-resource/four-layer/token arrays and one reusable
1280x720 ownership scratch (at most3,686,400 bytes), plus inherited resource and
mixed-snapshot budgets. More than128 visible rectangles rejects via the metadata
contract. Intersecting visible regions of two dirty GPU slots are unsupported
(e.g. certain unclipped parent/child DC relationships): reject without publishing
or inventing Z order. The new native path is not arbitrary window/DC admission.
Zero dirty slots can produce a valid CPU-base-only frame.

PrepareFrame pins the actual manager resources; MixedPresentation then owns the
CPU base/mask and geometry. Rejected allocation/validation preserves the prior
accepted frame; temporary builder scratch may change. Inherited resource probing
may flush/recover GPU commands, so rejection does not roll back live target work.
No guest callbacks, scheduling yield or context switch occur inside exchange.

## Output and lifetime

RenderMixed borrows the manager only for synchronous actual production GL output,
requiring the same coherent USER session, healthy gate and active GPU manager.
RenderMixedCpu uses the explicit actual CPU recovery route from the owning frame.
After real context destruction notify ContextLost first; software output then
works with no current GL context. Recovering presentation does not itself commit
GDI authority back to the screen; normal CPU reads/layout gates still checkpoint
accepted dirty pixels. A rejected GPU output must not be swapped.

The frame retains native resource pins and immutable captured CPU/mask/geometry,
not HWND/DC/guest pointers. Resource contents remain live. Moving/resize/destroy
still uses existing real CPU/lifecycle barriers. A new submission uses fresh
metadata. An already accepted frame describes its captured geometry/visibility;
it is not a live observer rebuilding that geometry on cursor redraw. Retired
images stay charged to the manager budget while a frame holds them. Shutdown
revokes manager resources even if the owned frame metadata survives. The binding
wrappers reject another current guest session; they do not retain it for replay.

## Public x86/EGL evidence

Extended gpu_window_coherence.c and its existing inspector, preserving the initial
64-fill/one-import/one-readback and two-dirty-window work gates. Ten real x86 stages
run in each of two fresh modes: injected GL error and actual EGL context loss.

Before the guest's first GetPixel, submit two dirty live windows and compare all
1280x720 production-EGL pixels against independent expected colors. Three redraws
add no target readback/upload or CPU barrier/commit. An injected next snapshot
allocation failure preserves the accepted frame and authority health. The guest
then consumes GPU pixels through unchanged real GetPixel/ROP/DIB/layout APIs.

A new real CPU-only top-level popup overlaps the first window. Its actual FillRect
pixels must survive mixed GPU output while the occluded GPU seed stays zero.
The inspector refreshes its lease after the actual new window changes layout;
stale leases are deliberately rejected. Independent full-frame expected pixels
include both native-injected GPU changes and that actual CPU popup. Canonical
CPU pixels remain unchanged until a normal guest CPU barrier.

A final captured live-window frame renders through explicit software recovery
in both failure modes, comparing all output pixels to the actual committed CPU
screen. After guest windows are destroyed, Prune retires the last image but the
frame still holds exactly128x128 pixels of budget; shutdown plus Clear frees all
injected buffers. Earlier test assumptions about still-valid leases and immediate
resource freeing were corrected to actual layout/pin contracts, without weakening
pixel or work gates. Existing 20-stage semantics and original transfer counts pass.

Strict O2/no-exceptions/no-RTTI native binding/inspector and strict MinGW fixture
are package gates. Focused ASan/UBSan instruments the changed bridge/inspector and
GPU/renderer modules with other runtime objects ordinary; leak scanning disabled
and known makecontext/swapcontext warning retained. Negative control substitutes
the full window rectangle for its actual visible region: the CPU-popup pixel
comparison must fail. Full COMPOSITION_GL_TEST=1 and exact published-SHA six-job
CI remain release gates. Existing stage-gpu-window-coherence artifact includes
public PE32 fixture, log and this contract; no seventh core job.

Local Switch SDK remains absent; CI builds NRO. Physical Paint/GPU/fallback/input/
relaunch checks remain deferred. Host Mesa is not a physical Switch FPS result.

## Next

[G4b.2e](G4B2E_MIXED_WORKSPACE.md) adds independent native ShellSession overlays
and actual USER mixed-producer selection before a CPU checkpoint. Next wire the
persistent Switch device/swap/cursor adapter; keep genuine CPU consumers gated. Complete raw-host/fault routing
and public x86/EGL/transfer gates before bounded ordinary GDI dispatch. Default
Paint/Win32 GDI remains CPU; dynarec and Direct3D remain separate future work.
