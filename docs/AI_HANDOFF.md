# Active development handoff — switch-recompiler

Branch: `claude/wonderful-hawking-10mpr0` only. Incoming verified HEAD:
`ae21f9670d848ba2a9d2a820ba44574a65f2913f` (2026-10-23).
[CI 39914017936](https://github.com/Omeruno/switch-recompiler/actions/runs/39914017936)
passed all six jobs: host/MinGW, CPU oracle, actual OpenGL/EGL reference,
ARM64/QEMU continuation ABI, Switch build and isolated JIT probe build.
This document and [ACTIVE_ROADMAP.md](ACTIVE_ROADMAP.md) are the current technical summary and implementation queue. Earlier investigative material
has been removed from the active handoff; use current source and subsystem docs,
not obsolete E1/common-dialog plans. No private hardware reports are reproduced.

## Working agreement

- Before every change, fetch/check branch HEAD, recent commits and this handoff;
  do not duplicate an active developer or overwrite newer work.
- Keep coherent commits on the specified branch; no force push. When publishing
  through Git data APIs, verify the tree matches the tested local tree and use
  a fresh expected-head lease. Git API metadata may give an equivalent local
  tree a different commit SHA.
- Run `sh tools/run_host_tests.sh` before publishing code. Use
  `COMPOSITION_GL_TEST=1` with MinGW/Mesa for the complete guest/GL suite.
  Check all six CI jobs against the exact published SHA. An earlier green SHA
  is not proof for a new change; avoid repeatedly rerunning already passed gates.
- Preserve CPU GDI/ROP/readback, software fallback, native 1280x720 Lite output,
  input routing and existing Paint behavior. UI/GDI cannot borrow guest/window
  pointers after synchronous submission or outlive continuation draining.
- Do not commit proprietary EXE/DLLs, keys or private user reports. User-supplied
  application binaries belong outside public git; public fixtures are generated
  from the repository's test sources.
- Hardware checks are deferred as one consolidated list. Continue independent
  software work without waiting for user confirmation. Never invent physical
  Switch acceptance, timing, input-to-photon latency or hardware percentages.
- Update this document and the map with actual outcomes and one concrete next
  step. Historical architecture notes may describe v1, not the current runtime.

## Current runtime and compatibility boundary

Bounded Stage C is accepted: one Process, scheduled guest threads, per-thread
TEB/TLS, waits/HANDLE rights/lifetime, APCs/timers, GUI ownership and controlled
startup/exit/cancellation. See [STAGE_C_ACCEPTANCE.md](STAGE_C_ACCEPTANCE.md).
Cross-process security/namespace, complete NT semantics and arbitrary forced
thread termination are not included in that acceptance.

Stage D loader/API work includes dynamic DLL/TLS lifetime and rollback, attach
failure handling and bounded SEH/unwind support. Extend it for reproducible
application incompatibilities; do not claim full Windows compatibility. Follow
[ROADMAP.md](ROADMAP.md) and the latest implementation/tests for remaining scope.

The isolated ARM64 executable-memory probe was previously reported to return 42
on Switch. Do not request that same minimal probe again as a prerequisite.
It does not certify a full dynarec: code-cache rewrite/invalidation, instruction
cache/lifetime and fallback need their own integration checks.

Paint's earlier Open/Save functionality has user confirmation, but newer graphics
commits still need the consolidated Paint smoke. Preserve the current launch-mode
selection; do not infer acceptance of a new build from an older confirmation.

## Stage E implementation state

- E0–E4 introduced backend/software reference, retained damage/output history,
  bounded resource ownership and cleanup. Accepted Stage E remains at its last
  physically verified milestone; implemented code alone does not award 100%.
- E5 production Switch OpenGL output has a software fallback. GPU execution is
  distinct from CPU Win32/GDI semantics.
- E6 (`98de7c1`): window-owned raster surfaces and independent GL textures with
  an ownership plane. Screen DC/ROP and readback retain canonical CPU pixels.
  The 28-scene live-x86 CPU/full-frame/GL oracle covers exposure, overlap,
  activation, child geometry, direct screen access and raw DIB mutation.
- E7 (`fed7668`): window and ownership revisions avoid unchanged-tile snapshot
  work; optional allocation failure falls back conservatively. Host benchmark
  evidence and work-count tests are in
  [STAGE_E_COMPOSITION.md](STAGE_E_COMPOSITION.md).
- E8 (`291e071`): report v6 separates CPU upload/draw submission, bounded
  asynchronous GL intervals, per-plane attempted transfers and logical texture
  memory. Four query slots never require blocking result reads or glFinish.
  [CI 39896269017](https://github.com/Omeruno/switch-recompiler/actions/runs/39896269017)
  passed all six jobs for that exact SHA.
- E9 (`e86a377`): `stage_e_acceptance.exe` links the unchanged test bodies for
  six software groups in one guest Process. It writes a fail-closed summary with
  fixture identity and explicit pending hardware/real-Paint/performance gates.
  CI builds/runs/publishes this redistributable fixture. Instructions:
  [STAGE_E_ACCEPTANCE.md](STAGE_E_ACCEPTANCE.md).

Do not confuse CPU GetPixel success with physical GPU/scanout equivalence.
The deferred checklist and all measurement scope/limitations live in
[STAGE_E_PERFORMANCE.md](STAGE_E_PERFORMANCE.md).

## 2026-10-23 E8 continuation: raster and observed-input correlation

Report v7 adds outermost GDI duration counts/sums/maxima in eight categories and
input queue wait, observation-to-next-changed-output and removal-to-next-changed-
output correlation. Real steady-clock stamps are independent of guest
fast-forward. Host-only Msg/InputEvent metadata leaves the guest MSG ABI and
Win32 time fields unchanged. Motion coalescing retains the newest stamp;
PM_NOREMOVE does not create a pending sample.

A 64-entry ring holds only time/change/serial values, with drop/invalid/pending
counts. Successful output consumes samples only after newer GDI changes and a
different normalized content serial. Cursor/no-op and failed output leave them
pending; successful software fallback can complete them. No causal DispatchMessage
or physical input-to-photon accuracy is claimed. Raster scopes do not yield and
include synchronous preparation; raw guest/direct Surface writes remain outside.

Validation: native metrics tests compare measured/unmeasured pixels, exact nested
accounting, disabled clocks, bounded storage, peek/removal, output failure and
virtual-clock jumps. The complete clean MinGW/Mesa suite passed, including both
real-x86 input/capture sizes with tracking/coalescing, all six E9 groups, both
28-scene CPU/GL composition runs and scripted file/color dialogs. Header smoke
passed -Wall -Wextra -Werror -fno-exceptions -fno-rtti.

A preceding local build attempt linked against one empty object file. Separate
compilation produced all expected symbols; the complete clean isolated build
passed without a runtime fix. Its cause is not established. The initial native-
only invocation explicitly skipped unavailable guest tools; only the later clean
MinGW/Mesa invocation is the complete local-suite result. New exact-SHA CI still
must be checked after publication. No physical console result is added here.

## 2026-10-23 F0: bounded picker integration

E8 continuation d967733be7600af290f5206ae2a3bdb10942889c passed all six CI jobs
in run 39901899983. The active map is ACTIVE_ROADMAP.md; historical diagnostic
material is intentionally not duplicated in this technical handoff.

Continued independently into the existing startup picker: ExecutableCatalog
bounds EXE paths, directory entries, path length and recursion, retaining ROMFS/
SD priority and normal SD scan depth. The real adapter uses it, paginates 18
items, reports incomplete/unreadable discovery and supports B/PLUS/applet exit
inside the input wait loop. Cancellation cleans services before graphics/guest
startup. Normal single-file autolaunch, L mode and ZL fallback selection remain.
Native catalog/budget/navigation tests passed with -fno-exceptions/-fno-rtti.
See F0_LAUNCHER.md for behavior, scope and the actual next lifecycle requirement.

This is not a desktop/taskbar or multi-launch shell. Next: a host regression
for two sequential one-Process sessions, then explicit repeatable Process/GUI/
renderer initialization and teardown before enabling return-to-launcher.
Physical picker behavior joins the existing consolidated deferred smoke list.
Final complete local suite and exact-SHA CI must be recorded below/after publish.

Final local F0 validation: complete `COMPOSITION_GL_TEST=1 sh
tools/run_host_tests.sh` passed with freshly generated MinGW guests and Mesa,
including the new catalog regression, E8 metrics, both tracked input/capture
sizes, all six E9 groups, both full-frame CPU/GL composition sizes and scripted
file/color dialogs. Standalone catalog/navigation test also passed with
-Wall -Wextra -Werror -fno-exceptions -fno-rtti. The actual libnx menu is a Switch
build/physical gate; local tests do not certify its pixels or button behavior.

## 2026-10-23 F0: owned core guest sessions

The incoming picker commit c58871a passed all six CI jobs in run 39902527062.
GuestSession now owns fresh Process/memory in Switch RunInterpreter and winrun.
Default/additional heaps, VFS, CRT globals/atexit/random/string/time/locale caches,
environment/command lines, dynamic TLS/FLS and descriptor/stream/find state are
Process-owned. Native context bindings and legacy scalar guest addresses are
captured/cleared/restored across scope lifetime. Standalone no-Process HLE tests
retain historical fallback behavior. No production relaunch loop is enabled.

The new real-x86 regression deliberately reuses native Process/memory storage and
guest addresses through normal exit/restart/host-abort/restart. It verifies fresh
heap/CRT/TLS/environment/FD state, file flush/delete, discarded abort atexit
callbacks and drained continuations. It also covers failed-load destruction and
nested native ownership/binding restoration. Initial fixture diagnosis incorrectly
expected no pre-main exception filter; MinGW installs one, so the test permits it
while rejecting the deliberately dirty previous-session marker. Runtime behavior
was not changed to satisfy that incorrect expectation.

See F0_SESSION_CORE.md for ownership/order and limits. GUI/legacy singletons are
still a gate: next isolate class/window/menu/image/GDI and legacy control/MFC
resources, then add sequential real-x86 GUI/dialog exit+abort coverage before
connecting return-to-picker. Do not claim general full-session reset from core
state isolation. Hardware E8/E9, current Paint, picker and later relaunch smoke
remain in the one consolidated STAGE_E_PERFORMANCE.md checklist.

Final local validation: the complete clean `COMPOSITION_GL_TEST=1 sh
tools/run_host_tests.sh` passed with freshly generated MinGW guests and Mesa.
This includes the new core-session regression, all six E9 groups, both 28-scene
CPU/actual-GL composition runs, real-x86 input/capture and scripted file/color
dialogs. GuestSession compiled with -Wall -Wextra -Werror -fno-exceptions
-fno-rtti; the other migrated HLE modules also passed no-exception/no-RTTI
compilation. Exact published-SHA CI must be checked after publication. No Switch
or proprietary Paint result was produced in this block.

## 2026-10-23 F0: new USER/GDI lifetime

Incoming 67dee08 passed all six CI jobs in run 39908725480. GuestSession now
exchanges owned USER State and active new/legacy GDI stores, discarding resources
before guest memory and restoring native outer context. USER State itself must
never move: retained layer captures point into its slots. GDI system colors,
USER brush/cascade/clipboard/poll state are scoped. Menus/popups/accelerators,
icons, hooks, scrollbars, deferred position/cursor metadata, standard/common
file/color dialog metadata and status bars are Process-owned. Native host thunk
registry and named-message namespace intentionally remain host infrastructure.

The new public x86 fixture leaves resources and callbacks for teardown. A single
host process reuses GuestSession storage through four painted window sessions,
four DialogBox sessions and four ChooseColor sessions, alternating normal or
callback ExitProcess and parked host cancellation. The regression checks fresh
class/hook/DIB/color/error state, complete continuation/focus/capture cleanup,
failed-load cleanup, stable nested capture ownership and pixel-identical renderer
redraw after the old memory/windows/layers disappear. See F0_GUI_SESSIONS.md.

Next independent block: legacy UI/MFC globals, window registry/message loop,
frontend image/resource/input bindings and a real sequential legacy guest fixture.
The legacy GDI store is scoped but legacy application state is not yet isolated.
No production launch-return-launch loop is enabled. Connect the loop only after
that remaining gate. Hardware/Paint/picker/fallback checks stay consolidated in
STAGE_E_PERFORMANCE.md; no physical result is claimed.

Final local validation for this block: complete clean
`COMPOSITION_GL_TEST=1 sh tools/run_host_tests.sh` passed after the status-bar
migration and GUI failed-load regression. All 12 sequential GUI sessions, four
core restarts, six E9 software groups, CPU/actual-GL composition at both sizes,
input/capture and scripted file/color dialogs passed. New scope compiled with
-Wall -Wextra -Werror -fno-exceptions -fno-rtti; migrated USER/dialog modules
also compiled without exceptions/RTTI. Exact new published-SHA CI still needs
checking after publication; earlier six green jobs certify incoming 67dee08 only.

## 2026-10-23 F0: legacy/MFC and frontend bindings

Incoming ae21f96 passed all six CI jobs in run 39914017936. LegacySession exchanges
legacy windows/classes, queue/paint/quit/timers, image/resource bindings, dynamic
MFC stubs, menu/dialog/about and pending Paint CDC/View/tool/color fields. Native
outer context and callback/layer/stack bindings are restored on teardown. Private
MFC guest-address maps, empty CString, current MSG/focus/capture, CDC vtable,
frame class/system brush caches and fallback nesting depth are Process-owned.
Registry values stay host-owned; transient open HKEY handles are exchanged.

RunInterpreter scopes its pump/stack marker, chooses the UI layer each launch
and resets legacy frontend rectangles/menu/tool/color/cursor/touch/click ownership
and throttle state on entry/exit. Host devices remain frontend-owned. The public
x86 legacy fixture covers nine sequential normal/abort/callback ExitProcess/
parked DialogBox/ShellAbout cases at reused document/native addresses, valid
CString/CDC state, document/view isolation, WndProc dispatch, menu resources,
failed loading and restoration of outer metadata/registry values/handles.
An initial unsupported legacy SetFocus call in the fixture was replaced by the
already implemented MFC CWnd::SetFocus; runtime compatibility was not expanded.
See F0_LEGACY_SESSIONS.md for exact scope and limitations.

Next independent step: real frontend launch-return-picker integration with mode/
output rebind, failed-load recovery and startup-held button/touch quarantine for
both UI paths. The NRO still exits after one guest; no production launch loop,
physical relaunch, real Paint or hardware performance acceptance is claimed.
Keep the one combined Switch checklist in STAGE_E_PERFORMANCE.md.

Final local validation: a fresh complete `COMPOSITION_GL_TEST=1 sh
tools/run_host_tests.sh` passed with MinGW/Mesa after registry-handle isolation.
It includes all nine legacy cases, four core restarts, 12 USER/dialog/color
sessions, Stage C/E acceptance, both CPU/actual-GL composition sizes, input/
capture and scripted file/color dialogs. LegacySession passed -Wall -Wextra
-Werror -fno-exceptions -fno-rtti; legacy host HLE also compiled without RTTI/
exceptions. The registry assertion first ran against an already-compiled older
LegacySession object; only the later fresh full build is the final-tree result.
Switch-only frontend reset/input and current Paint remain physical/build gates;
exact new published-SHA CI must be checked after publication.

## 2026-10-23 F0: production frontend launch/return loop

Incoming 421b099 passed all six jobs in CI run 39934544654. Switch main now uses
the host-tested RunLauncher controller: select, per-launch output/mode binding,
one joined worker, output teardown, console restoration, selection again. A sole
EXE autolaunches only on the initial selection. Display/thread/load/DLL failure
returns to a usable picker, with a bounded status; no automatic broken-EXE retry.
Applet shutdown is sticky across picker, both UI pumps and periodic execution
polling, and never restores the console after shutdown. The same periodic path
allows PLUS abort of CPU-only guests. Held buttons are quarantined on picker
entry, and legacy joins new UI in startup button/touch quarantine. Host audio and
ROMFS remain alive across guests; no pending audio storage is freed during a
launch transition. EGL teardown remains on the owning worker before its join;
main cleanup is idempotent and also covers partial startup failure.

Fresh complete `COMPOSITION_GL_TEST=1 sh tools/run_host_tests.sh` passed: new
controller failure/shutdown/input test, four core, 12 USER/dialog/color and nine
legacy/MFC sequential sessions, all six E9 groups, Stage C, both CPU/actual-GL
composition sizes, input/capture and scripted file/color dialogs. The standalone
controller test also passed -Wall -Wextra -Werror -fno-exceptions -fno-rtti.
The full host suite does not compile libnx main: exact published-SHA Switch CI
and physical repeated console/EGL/legacy/Paint transitions remain separate gates.
See F0_LAUNCH_LOOP.md and the consolidated STAGE_E_PERFORMANCE.md checklist.

Next independent software block: Stage G bounded interpreter opcode/host-call
workload profiling with repeatable public fixture measurements, before choosing
an optimization. No Switch speedup, total memory stability, physical relaunch,
real Paint or E8/E9 hardware acceptance is claimed. Continue software work while
the physical checklist remains deferred.
