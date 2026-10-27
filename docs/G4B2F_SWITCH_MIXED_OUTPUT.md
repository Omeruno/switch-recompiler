# G4b.2f: persistent mixed output device adapter and retained cursor redraw

Incoming `b6450f1cdc2bdff79e10a902e747ef73b3517f21` passed
[CI40409687618, six jobs](https://github.com/Omeruno/switch-recompiler/actions/runs/40409687618).
Extends [native shell/USER selection](G4B2E_MIXED_WORKSPACE.md). The Switch
adapter is an explicit native experiment, not automatic ordinary GDI admission.

## Device protocol and real fallback

MixedOutput owns reusable RGBA software staging and borrows a device, actual GL
renderer and software renderer. On a healthy device it renders the owning mixed
frame and only then attempts GPU swap. A rejected render never calls SwapGpu.
A failed swap is not counted as a successful GPU frame. No output/pixel view is
retained by the device beyond the synchronous callback.

For render/swap failure it first allocates bounded staging and uses genuine
RenderMixedCpu recovery from checkpoint/journal pixels. Only a complete successful
CPU rendering permits LeaveGpu, which must actually destroy the old context.
The adapter then notifies bindings.ContextLost, ensures software output and queues
the complete RGBA frame. Failed staging/replay leaves the context available for
controlled cleanup; failed software setup/queue latches failure without successful
frame acknowledgement. Repeated calls after failure perform no output work.
A known external context destruction must be followed by ContextLost before
calling the adapter; the no-current-context software route makes no GL calls.

Device order is explicit: render → swap, or full CPU recovery → destroy context →
notify manager → software setup → queue. Software staging is at most1280x720x4
(3,686,400 bytes) and persists across same-policy guests until device close.
Reset clears counters/fault per guest and retains staging; Release frees it.
Resident and mixed-frame budgets remain unchanged. This does not remove inherited
capacity checkpoints, explicit GDI CPU reads or all CPU frame copies.

## Retained cursor redraw

MixedWorkspace gains Refresh/Redraw and FrameCurrent. A retained frame is reused
only while native USER session, CPU change counter, GPU write revision, shell
revision and GDI layout generation still match. Cursor-only output therefore
reuses its owned CPU layers/mask and native resource pins without a new snapshot.
A source/layout change submits a fresh owning frame. CPU layout generation is
metadata only, never a coherence/readback barrier.

Both direct refresh/redraw failures latch the session output fault as well as the
workspace fault. Calls reject reentrant output, and failure does not silently
retry or clear faults. Frames, submissions and retained redraws have separate
counters. Context/manager/session ownership stays on one native worker thread.

## Switch frontend integration and opt-in boundary

The persistent new-UI frontend owns one MixedOutput device adapter. A session
owns WindowGpuBindings and MixedWorkspace after ShellSession, before the executor.
After continuations drain (including load/execute errors/abort), detach frame/
producer first, shut down bindings with the live context, then release shell/USER.
Device close performs the same ordering before GL/backend/context shutdown and
releases persistent software staging. Same-policy guest returns reuse the device.

The native experiment is enabled only when the first byte of
`sdmc:/switch-recompiler/gpu-gdi-native.flag` is ASCII `1`, the new USER path is in
use, a shell is attached and GPU output is active. The flag is read once per
session, defaults off and is not written by the project. L legacy and ZL software
keep their explicit overrides; an already-software device uses ordinary CPU
presentation. No new user hardware test or flag configuration is required for
independent host development.

When enabled, the actual Switch adapter is wired to selected USER output, native
clock changes and cursor redraw. It uses the existing actual SwitchGpuOutput
render/swap device and libnx linear framebuffer for complete software staging.
A failed software framebuffer acquisition/layout is not copied/queued as a
successful frame; the existing controlled output-failure path closes the device.
The native path bypasses ShellSession::Compose and old canonical presentation;
it cannot fall back to a stale retained flat guest frame for cursor movement.
Changed-source refresh passes through USER acknowledgement; unchanged cursor
redraw directly reuses the owning mixed frame. Cursor visibility participates in
no-op checks. Software fallback remains sticky under the existing output policy.

Ordinary GDI dispatch is still CPU. On Switch this flag currently exercises the
new output plumbing with CPU GDI and native overlays; no ordinary application
call automatically creates/fills GPU targets. Public native inspectors exercise
actual resident resources. Enabling this adapter is not enabling GPU-rastered
Paint or a measured production optimization. Remaining raw-host/fault audit and
bounded common GDI admission must precede such activation.

## Metrics

Renderer summary becomes v9, preserving existing fields and adding native mixed
requested/enabled, submissions/redraws, successful GPU/CPU frames, swap attempts,
fallbacks/failures and persistent staging bytes. Counters acknowledge accepted
output, not scanout/FPS. Existing framebuffer/GPU renderer counters include their
actual native work; legacy Presentation counters exclude mixed snapshots. Old
CPU-canonical input-to-frame correlation is not fed a fabricated mixed serial.
Native input correlation remains unmeasured until its own protocol/gates.

## Public x86/EGL evidence

The existing coherence inspector preserves its original20 x86 stages and native
shell/USER/ROP/DIB/transfer tests. Seven additional fresh x86/EGL sessions park
the real public guest at its first two windows, accept native GPU fills, and use
this production MixedOutput/MixedWorkspace protocol with actual production GL
and software renderers. Device swap/queue hooks inspect pixels and event order;
they do not certify physical Switch swap/scanout.

Each case first checks twelve full1280x720 frames with moving cursor and actual
native taskbar above the two resident windows. Expected output is independently
composed from known guest/native pixels and SoftwarePresentation cursor output.
All output pixels match; cursor redraw adds no snapshot/base upload/target readback.
Resident seed uploads occur before that retained phase. No FBO test readback is
hidden as a zero-synchronization/FPS benchmark.

The seven cases then cover: renderer GL error, swap rejection, staging allocation
failure, software setup failure, software queue rejection, actual destroyed EGL
context with no-current-context CPU replay, and metadata-only layout-generation
change forcing a new submission even with an unchanged CPU change counter.
Trace assertions require no swap for rejected rendering, exact Leave/Ensure/Queue
order, genuine software pixels, no acknowledgement/retry for faults, and full
injected staging release. Successful fallback continues with another cursor frame.
Parked native continuations drain through the real executor destructor; no fake
ExitProcess or completed guest exit is reported for these host-aborted sessions.

Strict O2/no-exceptions/no-RTTI and focused ASan/UBSan cover changed native output/
controller and actual target/mixed/renderer/inspector paths with ordinary other
runtime objects; driver leak scanning disabled, known native-fiber warning retained.
Negative controls force a new snapshot for every redraw, or swap a rejected render;
each must fail the corresponding work/ordering oracle. Full COMPOSITION_GL_TEST=1
and exact published-SHA six-job CI remain release gates. Existing coherence artifact
includes log, public PE32 fixture and this contract; six core jobs unchanged.

Local devkitA64/libnx is absent. The Switch-only device/flag/framebuffer wiring
requires exact-SHA CI compilation and deferred physical acceptance; the host EGL
protocol does not execute libnx. Paint/GPU/fallback/input/relaunch hardware checks
remain consolidated/deferred, SD diagnosis stays closed.

## Next

[G4b.2g](G4B2G_HOST_PIXEL_AUDIT.md) audits registered host pixel consumers, adds
guarded CPU views/sticky owner faults, checked fallback and lifecycle barriers.
Next admit a bounded ordinary GDI subset through common dispatch.
Preserve software/ROP/DIB/layout/session behavior with public x86/CPU/EGL/count
regressions and measure the complete producer/transfer/checkpoint path before
claiming performance. No EXE-name shortcut, dynarec, Direct3D or PC-game acceptance.
