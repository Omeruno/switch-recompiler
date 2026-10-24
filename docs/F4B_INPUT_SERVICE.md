# F4b.1: native input service independent of the guest message pump

Software package following `df54bdc` (exact CI 40039274735: six successful jobs).
User priority is functional system behavior; visual Luna work is deferred. This
package does not complete F4 or the wider filesystem/document environment.

## Execution and input ownership

Switch services ShellSession on its existing worker, between GuestExecution
quanta, with an 8 ms monotonic polling interval. There is no extra input/render
thread or concurrent owner of USER/GDI/EGL. GuestExecution normally yields after
256 checkpoint operations, including nested interpreted callbacks. A host call
which never returns/yields still cannot be preempted; this is not a physical
8 ms latency guarantee or a complete OS scheduler.

ServiceHost polls the original native source nonblocking, handles native Start/
Explorer/input/rendering, and puts unconsumed guest events in a fixed 256-event
ring. The ordinary USER wait adapter drains that ring. Guest hit testing,
WndProc, focus/activation and message dispatch run through the existing USER
pump and its actual guest context. Host polling never calls guest procedures.
Native events are filtered once at observation, not reclassified at later
message delivery after shell focus has changed.

Only adjacent mouse motion coalesces, retaining the newest coordinates/time and
physical snapshot; button/key transitions remain ordered. Full noncoalescible
input latches an explicit InputOverflow error. The frontend stops the guest and
uses the normal continuation/session drain and return path. It does not silently
drop a key release or pretend delivery. Quit observed between quanta requests
host cancellation; Quit in the ordinary USER wait remains an event handled by
its existing WM_CLOSE route. Source closure is an explicit lifecycle condition.
All queued input/status/task intents belong to this ShellSession and disappear
on exit/abort; platform hooks are restored before its buffers are freed.

## Physical state versus queued messages

ObservePhysicalInput updates async key/button state at observation, before the
application chooses to read messages. GetCursorPos also sees the latest native
pointer position, including over the shell. Deferred events carry host-only
coordinates, modifier/button flags, Alt context and previous-key state. USER
uses those snapshots for message point/wParam/lParam and repeat/system-key bits;
it does not replay old input into the latest async state. Queue-local GetKeyState
continues to advance when messages are removed. The guest MSG ABI is unchanged.
Left/right Shift/Control/Alt aggregate state remains pressed until both sides
are released. Native-reserved commands retain their input isolation from guests.
This does not implement new keyboard-device/layout APIs or complete input NT semantics.

## Task commands are requests to the real window owner

The task button validates a bounded group of at most 64 live windows and posts
WM_SYSCOMMAND/SC_MINIMIZE or SC_RESTORE to each owner's actual queue. It never
calls ShowWindow/WndProc on the unbound host stack or substitutes a worker CPU.
The app receives these through normal PeekMessage/GetMessage and DispatchMessage.

Only one group intent can be pending. Repeated clicks while the app is busy do
not flood posted messages. The button displays an awaiting indicator until the
actual window styles reflect the request or the windows disappear. A busy app
is not forcibly resized/minimized, and an app that refuses the command is not
reported as minimized. Start and real folder navigation remain available. Closed
or independently restored windows are removed from minimized tracking. PLUS can
still explicitly cancel a guest which does not respond.

This does not implement hung-window ghosting, full multi-process multitasking,
a persistent output device across launches, or retained native/guest GPU layers.
F4a flat composition/fallback and launch/teardown output ownership remain in use.

## Reproducible public tests

- `test_host_input`: physical left/right modifiers, deferred Alt/repeat/point
  snapshots, five full ring fill/drain wraps, USER Quit and source closure.
- `shell_busy.c` / `test_shell_busy`: real interpreted x86 with a window while
  executing a no-host-call spin; native Start and real Explorer work with zero
  guest message delivery/callbacks. Checks visible output submission, 1000 motion
  samples coalesced to one, exact key/button pairs after pumping resumes,
  owner-thread minimize/restore, a second mode polling GetAsyncKeyState and
  GetCursorPos without a USER pump, a window created by a worker while main is
  parked in WaitForSingleObject, overflow cancellation, Quit, pending-command
  host abort, cleanup and subsequent normal restart (seven sessions).
- Existing two-window workspace regression now dispatches posted commands through
  its real x86 WndProc before inspecting geometry; it no longer assumes a native
  task click synchronously changes a guest's window state.

Run the complete clean `COMPOSITION_GL_TEST=1 sh tools/run_host_tests.sh` with
MinGW/Mesa, strict no-exceptions/no-RTTI native compilation and exact published-SHA
CI including Switch. Local Switch SDK is unavailable; physical Paint/GPU/fallback/
Lite/input/relaunch checks stay deferred. No SD diagnostic or hardware result is
implied by these host runs.

## Next functional packages

1. Persistent frontend output/desktop lifetime through selection, load, guest exit
   and error, plus retained native/guest layer composition with the CPU/GL oracle.
2. Common real file operations: folder creation, copy/move/rename, explicit delete
   confirmation and stat-backed properties, shared with F1/Win32 semantics.
3. Consistent left/right click, context menu, selection/scroll/drag and controller
   routing for those actions; then bounded persistent settings/associations.
4. Real TXT read/edit/save and supported image reading; public save -> find ->
   reopen scenario (F5). Return to visual Luna refinement after functional gates.

Missing memory-protection/unwind and application API compatibility remain D
work; G optimization requires measured evidence. This package is not full Windows.
