# G4b.2e: native shell overlays and selected USER mixed output

Incoming `49b85d0faa35f5580adc52669544568d14c2c3e4` passed
[CI40406723178, six jobs](https://github.com/Omeruno/switch-recompiler/actions/runs/40406723178).
Extends [live-window mixed frames](G4B2D4_LIVE_WINDOW_FRAMES.md).
This package combines native shell ownership, the real USER presentation selector,
and output-fault propagation. It does not activate ordinary GPU GDI or install a
mixed device/swap adapter in the Switch frontend.

## Native overlay and owning frame

ShellSession::NativeOverlay returns only its native CPU chrome and an independent
ownership mask: taskbar, actual dynamic panel/dialog bounds and Start menu.
It never calls Compose, copies the guest framebuffer or acquires CPU authority.
It uses current observed task state and existing native rendering/input/lifecycle.
Views are borrowed only until the next shell call and are synchronously copied
by the mixed submission; no shell Surface/view pointer enters retained frames.
The existing CPU Compose and full Scene paths remain available.

WindowGpuBindings::SubmitMixed optionally merges that CPU overlay into its private
live-window ownership map, after constructing validated resident window owners.
Native nonzero owners override GPU owners. CPU overlay tokens must be greater
than4; duplicates/unknown/out-of-bounds owners/invalid CPU surfaces reject through
the owning-frame validator. Up to48 total CPU/resident layers; existing native
shell uses one full1280x720 CPU layer. Resource bounds stay four images, side512,
combined512x512 resident pixels, existing16MiB CPU-layer/renderer limits and the
packed base/owner snapshots. No new GPU target or global window ownership token
is fabricated for native controls.

A new accepted submission conservatively copies the CPU base/mask/overlay and
gets fresh upload identity. Unchanged retained redraw can reuse the existing
MixedPresentation data, but the new USER adapter submits when selected; it is
not an incremental CPU snapshot algorithm. No end-to-end memory-traffic/FPS claim.

## Selecting output before a CPU barrier

Platform gains a native tryPresent callback with explicit results:

- Submitted: the output adapter has accepted the frame; USER acknowledges its
  observed CPU/shell/producer revisions and presentation timestamp.
- Unavailable: acquire real CPU authority, then call the existing CPU present
  callback. Without a CPU callback, fail closed.
- Failed: latch session-owned presentationFault; do not acknowledge or retry an
  unswapped failure and do not silently fall through to a stale CPU frame.

The selected native callback runs in the actual PresentIfNeeded path before a
CPU checkpoint. Default/legacy CPU output retains coherence-before-selection.
A valid native producer has a process-wide unique identity and a monotonic GPU
write revision; native Fill advances that revision for accepted nonempty clipped
work, without rastering the CPU screen. CPU screen changes and shell revisions
are tracked separately. GPU-only changes therefore request output, while unchanged
submissions and the existing16ms nonforced throttle still suppress duplicates.
Forced presentation remains available. Real CPU consumers/layout gates are not
bypassed by this presentation-only protocol.

State acknowledgement happens only after successful native submission or the
existing void CPU callback. The latter retains its old adapter-owned failure
policy; this is not a new success contract for every legacy callback. CPU gate
failure also latches presentationFault. Fresh USER sessions start clean. Switch
OutputHealthy now observes this latch during a guest, using the existing controlled
executor stop/drain/return path instead of continuing with a silent output fault.
No local libnx compilation or physical fault-handling result is claimed.

## MixedWorkspace adapter and lifetime

MixedWorkspace owns one mixed frame and borrows coherent bindings, ShellSession
and an output callback. Attach validates owner session, shell readiness and no
existing selected producer/fault; it installs only native producer fields.
Detach restores those fields and releases the owning frame. It does not replace
shell/input/wait/text/legacy CPU output callbacks or clear an output failure.
Declare it after ShellSession and before GuestExecution; drain continuations
before detaching and before destroying bindings, shell, device/context or USER.
All operations require the same native owner thread/current GL context, no guest
callback or scheduler yield inside native submission/output.

The output callback receives bindings and the owning frame, not raw texture names
or borrowed guest pixels. It owns actual render/swap/recovery policy and must
return false for an unswapped failure. Successful GPU rendering alone is not a
physical swap/scanout certificate. Explicit genuine RenderMixedCpu recovery is
available; a failed GPU frame must never be swapped. Context loss notification
must precede no-context CPU recovery. This adapter does not recreate devices.
An unavailable owner may use the existing CPU route; invalid scene/output rejection
latches failure. The retained producer cannot attach again until that session's
fault is resolved by controlled teardown, not by clearing the flag internally.

## Evidence

Extended the existing public x86/EGL coherence inspector without replacing its
20 actual-x86 stages or original64-fill/import/readback/ROP/DIB/lifetime gates.
Before CPU consumption it uses real PresentIfNeeded and MixedWorkspace to output
live dirty windows plus native taskbar/Programs/Start/Preferences chrome. Expected
pixels are independently assembled from known guest colors and native rendering,
using actual panel/dialog bounds and explicit Start/taskbar coverage rules.
Every1280x720 output pixel is checked with the production EGL renderer.

Cases cover native panel movement, max/min/task restore, dialog movement outside
its owning panel, close, native input isolation, and a real CPU guest popup above
a GPU window. A duplicate-color GPU write advances only the native epoch and must
select another output even though canonical CPU pixels and shell state stay
unchanged. New shell revisions likewise select output; a fully unchanged call
does not. Throughout native selection/overlay work canonical CPU pixels stay
unchanged and target readback/CPU authority barrier/commit counts do not grow.
No full guest Compose is used for those frames.

Output rejection retains all USER acknowledgement fields/timestamp, latches the
workspace and session faults, blocks repeated attempts, restores callbacks on
detach and does not poison the next fresh session. The native present-progress
regression separately verifies actual software output for unavailable/selected
callbacks and their CPU gate counts, plus failure acknowledgement/no-retry.
Existing busy-queue/no-op/throttle and software/EGL workspace tests remain gates.

Strict O2/no-exceptions/no-RTTI and focused ASan/UBSan cover changed bridge/shell/
USER selector/inspector and actual GPU/renderer modules, with other runtime objects
ordinary, driver leak scanning disabled and known makecontext/swapcontext warning
retained. Independent negative controls insert a CPU barrier before the selected
producer, or omit native dialog ownership; each must fail the corresponding
selection/work-control or pixel assertion. Full COMPOSITION_GL_TEST=1 and exact published-SHA
six-job CI remain required. Existing stage-gpu-window-coherence artifact carries
its log, public PE32 fixture and this contract.

## Remaining activation work

[G4b.2f](G4B2F_SWITCH_MIXED_OUTPUT.md) now implements the explicit opt-in Switch
device adapter, genuine staged software recovery, retained cursor redraw and
context/session/device ordering. Default ordinary GDI remains CPU.
Ordinary GDI still has no automatically admitted GPU dispatch. Audit remaining raw
host pixel consumers and propagate coherence faults before admitting a bounded
common GDI subset with public x86/CPU/EGL/work-count gates. The new USER selector
is an integration prerequisite, not a completed full frontend GPU optimization.

Local devkitA64/libnx remains absent; CI builds NRO. Physical Switch/Paint/GPU/
fallback/input/relaunch checks remain deferred. No new SD diagnosis or hardware
request. Dynarec, D3D9 and the planned PC-game targets remain separate work.
