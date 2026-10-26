> Current update: [F6b native window actions](F6B_NATIVE_WINDOW_ACTIONS.md)
> implements bounded native geometry, min/max/task restore, close, move/resize
> and dynamic input/composition. Historical fixed/decorative-native notes below
> describe their original milestone; current queue is in [ACTIVE_ROADMAP.md](ACTIVE_ROADMAP.md).

# F6a: real guest Win32 nonclient window gestures

Incoming `b3a8ae49d8f1e17822d236221e2a1928e19dffeb` passed
[CI40347620739, six jobs](https://github.com/Omeruno/switch-recompiler/actions/runs/40347620739).
User requests real minimize/maximize/restore/close and corner resize. This package
implements the guest Win32 interaction path; native DesktopShell panels still
need their own F6b geometry/task/input implementation. No decorative button is
reported as a completed native window feature.

## Behavior

A left press on a normal guest caption or thick-frame edge/corner enters a real
owner-thread modal gesture. Default nonclient processing sends SC_MOVE/SC_SIZE
through the application's WM_SYSCOMMAND procedure, so the application can veto.
Sizing covers all four edges and all four corners, not only bottom-right.
It queries WM_GETMINMAXINFO, captures the pointer, sends WM_ENTERSIZEMOVE,
then offers screen-coordinate rectangles in WM_MOVING/WM_SIZING. An application
may edit those rectangles. Accepted rectangles go through real SetWindowPos,
nonclient/client recalculation, WM_WINDOWPOSCHANGING/CHANGED and WM_MOVE/WM_SIZE,
invalidation and retained-layer/layout revisions. This is not texture stretching,
a screenshot transform, EXE-specific logic or a replacement application.

Tracking limits honor positive application minimum/maximum sizes within a bounded
signed16 coordinate/size contract. Defaults are112x32 minimum and32767 maximum;
nonpositive application limits retain defaults. Invalid/out-of-bound callback
rectangles are rejected without geometry mutation. Top-level and child rectangles
are converted between screen and parent-client coordinates. Windows outside this
bounded coordinate range are not admitted to interactive tracking. Normal moves
preserve their existing size even when smaller than the sizing minimum.

The regular owner queue continues dispatching unrelated messages/timers during
tracking, using the existing native continuation/guest execution checks. No guest
window pointer is retained across a callback/yield: native identity and capture
are revalidated. Button release commits; Escape restores the original rectangle.
WM_CANCELMODE/capture theft ends the gesture without releasing another window's
capture. Destroyed windows receive no exit callback; host stop unwinds capture
without executing further guest code. WM_QUIT is reposted for the outer pump.
Live windows receive WM_EXITSIZEMOVE after normal/cancelled tracking.

Caption min/max/restore/close buttons now track until left release over the same
button. Dragging out/releasing elsewhere or Escape cancels. Disabled min/max
styles do not start tracking. A successful release uses actual WM_SYSCOMMAND;
close proceeds through WM_CLOSE, which the guest may refuse. Existing real
maximize respects the taskbar work area; restore retains normal geometry and
maximized-before-minimize state. Minimization now clears window focus/capture.
Double-click detection now includes nonclient captions even without CS_DBLCLKS,
so ordinary physical caption double-click reaches the existing maximize/restore.

Platform batches may contain the outside movement/release before the queued down
has established capture. USER retains the nonclient origin's native identity
until physical left release, so those events reach its owner queue rather than
being dropped on the desktop's thread0. Explicit capture takes precedence; a
retired/reused HWND cannot inherit the origin. This routing is scoped to system
nonclient presses, not a blanket implicit capture for application client input.

## Public evidence

`tests/guest/window_gestures.c` is a CRT-free public PE32, driven by real platform
InputEvents and its actual message pump/WndProc in `test_window_gestures.cpp`.
Two fresh sessions deliver22 stages each: sequenced per-poll events and batched
events. Independent exact GetWindowRect expectations cover:

- all eight resize directions, move and real WM_SIZE delivery;
- minimum/maximum tracking, application-edited sizing rectangle;
- Escape rollback, WM_CANCELMODE and capture theft from a live callback;
- close-release cancellation and a guest veto of actual WM_CLOSE;
- physical maximize/minimize and exact restore, focus/capture release,
  maximized-before-minimize state and nonclient caption double-click;
- application SC_SIZE veto and destruction inside WM_SIZING.

A third fresh execution parks with real capture inside the modal loop, then the
host destroys GuestExecution. Native callbacks must unwind and capture must be
released before the session is discarded. This is separate from successful
44-stage completion; a parked abort is not reported as a normally exited guest.

Run `COMPOSITION_GL_TEST=1 sh tools/run_host_tests.sh`. The host CI publishes
`stage-window-gestures` with the bounded event log, generated public EXE and this
contract. Existing six core jobs and x86/CPU/EGL/session/input/file gates remain.
Host evidence is not physical Switch acceptance. Local devkitA64/libnx remains
absent; CI builds NRO and physical E/F tests stay deferred.

## Remaining scope / next

F6b: real native Explorer/Programs/About/document panel geometry, minimize/task
restore, maximize/restore, closing policy and interactive layout/input/composition
for move/resize. Existing fixed native panel controls must be replaced consistently,
not merely scaled visually. Native min/max remain decorative in this package.
System-menu/keyboard move-size selection, maximized-caption drag-to-restore,
custom resize cursors, broader virtual desktop coordinates and full Windows/MDI
semantics remain future work. This is bounded software acceptance, not complete
XP shell behavior. Preserve the G4b.2d.2 mixed-frame/live-window integration queue
and future PC-game goals; this package does not activate GPU GDI or a dynarec.
