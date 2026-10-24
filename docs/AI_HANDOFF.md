# Active development handoff — switch-recompiler

Branch: `claude/wonderful-hawking-10mpr0` only. Incoming verified HEAD:
`6ac9aa071f168a26e0c75b4f634f7efa606c7f2e` (2026-10-24).
[CI 40087919426](https://github.com/Omeruno/switch-recompiler/actions/runs/40087919426)
passed all six jobs: host/MinGW, CPU oracle, actual OpenGL/EGL reference,
ARM64/QEMU continuation ABI, Switch build and isolated JIT probe build.
This document and [ACTIVE_ROADMAP.md](ACTIVE_ROADMAP.md) are the current technical summary and implementation queue. Earlier investigative material
has been removed from the active handoff; use current source and subsystem docs,
not obsolete E1/common-dialog plans. No private hardware reports are reproduced.

## Current priority after the 2026-10-23 audit

Latest steering: prioritize real system functionality and verifiable operations;
return to visual Luna polish later. F1/F2/F3a/F3b/F4a are implemented; F4b.1 adds
independent native input service, bounded deferred guest events, physical async
state and owner-queue window commands. F4b.2 now keeps one frontend worker/device across guest sessions. Shared real file mutations/properties now exist in VFS and the browser model
([scope](F_FILE_OPERATIONS.md)). [Native file actions](F_FILE_ACTIONS.md),
name entry, delete confirmation and properties are now connected.
[Checked byte-copy provider/model](F_FILE_COPY.md) is implemented; next: native Copy/Paste, coherent L/R
click and controller actions, preferences/TXT/images and F5 save/find/reopen.
See [F4B_INPUT_SERVICE.md](F4B_INPUT_SERVICE.md), [output lifetime](F4B_OUTPUT_LIFETIME.md), ACTIVE_ROADMAP.md and
F_SHELL_PLAN.md. This supersedes older next-Luna/next-G priorities. D memory
protection/unwind/API compatibility remain separate open work. No fake successful
operation, forced guest callback or guessed application compatibility is allowed.

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
commits still need the consolidated Paint smoke. New USER/GDI is now the default for every EXE including Paint; L selects the
legacy compatibility path, ZL software output. Preserve those explicit overrides; do not infer acceptance of a new build from an older confirmation.

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

Switch CI for 59393c1 caught a frontend declaration-order error: RunInterpreter
now uses g_pad before its old declaration. The follow-up moves the one PadState
declaration above the interpreter; no input/lifetime behavior changes. Four other
CI jobs had already passed. Verify the corrected published SHA, not the failed
59393c1 Switch build. Local full host validation remains applicable because it
does not compile main.cpp; Switch build is the direct check for this correction.

## 2026-10-23 G0: measurement foundation

Incoming 2742eeac77d6fcde354336f680846804138c3661 was fetched without resetting
newer work; GitHub API independently confirms all six jobs in run 39940606718.
No AGENTS.md was found. This checkout initially held the old import on local
`work`; the clean checkout was switched to the requested branch at its remote tip.

Added opt-in fixed-storage instruction/host entry census, independent million-
event budgets and explicit truncation, ordinary-return winrun report and a public
native x86 fixture with exact counts and semantic checks. There are no per-event
clocks, allocations, logs or guest pointers. This is frequency, not CPU time.
The Switch frontend does not enable the collector. See STAGE_G_PROFILING.md for
fault/REP/host entry denominators, build-local IDs and reproducible instructions.

The current implementation order/gates are recorded in ACTIVE_ROADMAP.md.
Next: representative public PE workload with checked results, phase measurements
and enabled/disabled overhead, then select one evidence-backed optimization.
Do not choose a fast path from synthetic counts or infer Switch speedups.
SD troubleshooting is closed; the consolidated physical checklist stays deferred.
Validation and exact new published-SHA CI are recorded after completion below.

G0 local validation: fresh complete `COMPOSITION_GL_TEST=1 sh
tools/run_host_tests.sh` passed with GCC 14 Win32 MinGW and Mesa, including
all six E9 groups, Stage C, both 28-scene CPU/actual-GL comparisons, input,
dialogs and core/new-USER/legacy sequential sessions. The new native regression
also passed -Wall -Wextra -Werror -fno-exceptions -fno-rtti. Runner smoke verified
identical enabled/disabled guest stdout and exit 0, two identical DLL fixture
profiles (1426 instructions/111 hosts), CRT fixture success with profiling,
and failed-load exit 3/zero-count report. No code optimization was selected.

The first MinGW full run used POSIX alternatives: crt_basic/crt_cpp failed because
that compiler introduces unsupported libwinpthread pthread_once. Rootless package
extraction was corrected to explicit Win32 GCC/G++; a fresh full fixture rebuild
then passed. This is toolchain selection, not a runtime fix. The initial native
fixture also assumed last-error context without a bound TEB; its dispatcher
checks now use context-independent lstrlenA/W rather than expanding HLE behavior.

Tools live outside git in /workspace/setup-tools/deps with a cxx-host wrapper;
APT signed-index/checksum validation and TLS were retained. The old devkitPro
HTTP 403 is no longer present (installer URL returns 200), but devkitA64/libnx
are absent and the official installer needs root; this machine has no sudo.
Local Switch compilation is unverified; use exact-SHA Switch CI. No current
Paint, physical GPU/fallback/input, SD or console relaunch test was performed.
Exact new-SHA CI is pending publication at commit time; check GitHub, not the
incoming green run, for this package's outcome.

## 2026-10-23: project audit and restoration of the Stage F priority

Audited clean fetched 03b9fd4; no newer branch commit or AGENTS.md was found.
Compared all A–I stage boundaries with runtime/files, Process loader, kernel32
file/system/volume APIs, shell32/UxTheme, actual Switch picker/worker/output and
existing tests. Native probes confirmed real host file persistence and case
lookup, per-EXE root isolation, aliasing drive letters, and unenforced mapped-
page permission metadata. The latter remains an explicit D/A gap requiring a
separate privileged-host/guest-access design; no runtime source was changed.

The missing user milestone is F1–F5: a persistent common SD-backed filesystem,
native desktop/taskbar/Start, real-directory explorer and one-Process guest
integration. Merely skinning the console picker cannot satisfy it. Current
RunGuest joins the worker and output changes owner across launches, so an active
taskbar needs real compositor/input ownership work. Existing SD files/registry
must be preserved; proposed mounts are not yet implemented or a migration.

Corrected active ROADMAP/ARCHITECTURE/README and early F0 documents that still
claimed no GPU or no return loop. Historical implementation records remain
labelled. XP.css MIT (376f42e) and Tango public-domain icons (41b8f6a) were checked
as pinned visual sources; no assets or new dependencies were imported. Native
blue/green theme and actual menu/file actions have distinct F2–F5 acceptance.
See the audit and shell plan for sources, missing contracts and package gates.

Incoming 03b9fd4 CI run 39955956133 still had five successful jobs and ARM64
installation in progress at this audit check; do not call that 6/6. Full local
MinGW/Mesa and local ARM64/QEMU results were previously verified. This package
changes documents only: source/link/diff validation applies; no repeated runtime
build or new physical test is claimed. Check the exact documentation SHA's CI
after publishing. Physical checks remain consolidated/deferred; SD is restored.

Audit document validation: 14 changed/new Markdown files, 52 relative file links
resolved, balanced fenced blocks and clean git diff --check. The two targeted
native probes passed outside the checkout. No source/test/dependency/asset files
were changed, and the unchanged runtime suite was not rerun for this doc package.

## 2026-10-23 F1: persistent SD-backed filesystem foundation

Incoming 1d1bd6f was fetched clean, no newer branch commit or AGENTS.md was found.
Its exact CI run 39959927599 has now completed all six jobs successfully.
F1 configures C=sdmc:/switch-recompiler/drive_c, S=sdmc:/ and read-only R=romfs:/
for each production Switch guest. It creates only missing profile/system/temp/
Programs directories and leaves existing EXEs, documents and registry in place.
Picker discovery includes C Programs within existing budgets. EXE directory and
CWD remain distinct from persistent C; old relative paths and sibling DLLs stay
beside the EXE. Old saved absolute C paths may need reselection through S; no
ambiguous fallback or automatic migration is introduced. Host winrun preserves
its standalone default and offers explicit system/sd/romfs root options.

VFS has separate drive cwd, invalid-drive/UNC/stream/link/read-only handling and
bounded path lengths. Mutating Win32/CRT calls check write intent first. Loader
paths/command line, env/profile/special folders and actual statvfs-backed free
space share the mapping. Volume names identify providers, not invented physical
NTFS/FAT formats; ROMFS capacity remains unmeasured (reported zero). HLE modules
remain virtual; no proprietary Windows DLLs are installed. See F1_FILESYSTEM.md.

Native mount tests cover actual host capacity, C-inside-S round trips, per-drive
cwd, case/Unicode, root boundaries, invalid/linked/read-only paths and repeated
non-destructive setup. A public x86 fixture runs four fresh sessions from two
EXE directories with a mixed-case real sibling DLL, normal writer/reader and
host-aborted writer/reader, plus failed load. Host inspection checks real Unicode
document bytes. Win32/CRT readonly failures, A/W drive/space/profile APIs and
scripted real Unicode Save/Open dialogs are checked, including refusing R save.

The dialog extension exposed an old ANSI conversion of UTF-8 paths and byte-
based W extension offsets; these are corrected to UTF-16 paths/code-unit offsets.
Persistent bare DLL lookup now uses the mounted case-insensitive resolver;
standalone legacy lookup stays compatible. Initial compilation of the targeted
harness needed its Window pointer qualifier corrected; no runtime workaround.
An early targeted link mixed old VFS ABI objects with new ones and was rejected;
only the complete clean rebuilds and final-tree checks count as validation.

Next package: F2 native desktop/theme with pinned licensed artwork and host-owned
shell state, then F3/F4/F5 according to F_SHELL_PLAN.md. F1 does not claim a
working desktop/Start/explorer or physical Switch/Paint acceptance. GPU/fallback/
input/relaunch hardware checks stay deferred; SD is not re-diagnosed.
Exact new published-SHA CI must be checked after publication.

Final F1 local validation: complete clean `COMPOSITION_GL_TEST=1 sh
tools/run_host_tests.sh` passed on the final runtime/test tree after the Unicode
Save/Open/readonly-cancel and mixed-case DLL checks were added. It executed the
new native mount test and four-session public x86 harness, all six E9 groups,
Stage C, both CPU/actual-GL composition sizes, input/dialog and existing sequential
session regressions. files.cpp and kernel32_volumes.cpp passed standalone -Wall
-Wextra -Werror -fno-exceptions -fno-rtti compilation. CLI --system-root/--sd-root/
--romfs-root smoke ran a freshly built public CRT fixture successfully and created
the persistent profile. Switch-only main/picker/statvfs integration still requires
the exact new-SHA Switch CI gate; local Switch SDK is unavailable. No hardware
result is inferred from these host runs.


## 2026-10-23 execution plan and checkpoint request

F1 published commit d7a15b79650e337429573651c02774fca96dc787 is verified:
CI run 39965068140 completed all six jobs successfully, including Switch build.
The working branch was clean at that HEAD when planning began. Added the granular
F2 → F3 → F4 → F5 → measured G execution queue to F_SHELL_PLAN.md and linked it
from ACTIVE_ROADMAP.md. Next code package remains F2; no shell implementation or
new runtime acceptance is claimed by this documentation package.

User requested a checkpoint every 40 minutes. No alarm/scheduler tool is available
in this session; no recurring alarm or automatic agent wake-up has been created.
Use the interval as an active-session checkpoint guideline, preserving progress,
checks, blockers and the next concrete step. Do not promise unattended resumption.
Local Switch SDK remains unavailable; hardware checks remain deferred, SD closed.
Validation for this package: Markdown local links/fences and git diff --check;
runtime source is unchanged. Check CI for the published documentation SHA.


## 2026-10-24 F2: native idle desktop/theme

Incoming d6a63510af2eab7c409acefd189d6fc04e1e9933 was fetched with a clean tree;
CI run 39967576903 passed all six jobs. No newer branch commit was overwritten.
Implemented host-owned DesktopShell with a fixed 1280x720 canvas, blue desktop/
taskbar, green Start, device clock, bounded catalog/selection/panel state, actual
Programs/About/Exit actions and native executable panel. My Computer/Documents/
Run are disabled pending F3. Imported three pinned Tango public-domain PNGs and
COPYING declaration with original/decoded/generated hashes; stdlib tooling checks
the committed native RGBA pixels. Existing Liberation/OFL fonts are retained.
No XP.css code/artwork or proprietary Microsoft assets were imported.

Switch selection now uses that idle desktop with its own software presentation,
main-thread input and viewport mapping, closing frames/output before the existing
guest worker starts. Initial sole-EXE autolaunch, per-launch L/ZL, bounded catalog,
console fallback, guest teardown and sticky applet shutdown are preserved. Entry
quarantines held buttons/touch; navigation cancels/quarantines pointer gestures.
State retains EXE/panel through return if the EXE still exists in the next scan.
Guest worker join remains blocking: this does not implement F4 active-guest
compositor/input ownership, minimize/restore or simultaneous applications.

Native strict harness checks menu/disabled/pressed/cancel states, public temporary
EXE inventory paging/Unicode paths, selection/launch/return and scaled presentation/
touch/pitch/letterbox guards. Actual native screenshots were visually inspected.
One initial test incorrectly used Accept on an empty Programs panel whose focus
was Close; it was corrected to click the disabled Open control directly. No
runtime behavior was changed to satisfy that test assumption. See F2_DESKTOP.md.

Next code package: F3 real bounded directory provider over F1 plus supported
Start/Run/explorer actions. Then F4/F5. Local Switch SDK is still unavailable;
exact published-SHA Switch CI is the compilation gate. No physical shell, Paint,
GPU/fallback/input/relaunch acceptance is claimed; SD troubleshooting stays closed.
Final full-suite outcome and exact published-SHA CI must be checked before calling
this package validated; source-only host results do not compile libnx main.

Final F2 local validation: complete clean `COMPOSITION_GL_TEST=1 sh
tools/run_host_tests.sh` passed with the rootless MinGW/Mesa toolchain, including
new shell/artwork checks, persistent filesystem sessions, all six E9 groups,
Stage C, both CPU/actual-GL composition sizes, input/dialog and sequential core/
USER/legacy sessions. The standalone shell harness also passed -Wall -Wextra
-Werror -fno-exceptions -fno-rtti with the final native renderer and regenerated
screenshots. Markdown relative links/fences and git diff --check passed. Only
libnx adapter input-quarantine/comments and documentation changed after the full
host compile; main.cpp is excluded from that host suite and is checked by Switch
CI. Exact published-SHA CI remains to be verified after push.


F2 focus follow-up: initial published 437de20 passed Switch build, full host,
ARM64/QEMU, GL and JIT jobs in run 40026831580; its CPU oracle job was still
installing GCC at this check, so it was not called 6/6. Additional navigation
review found that dismissing Start from About/Programs could reset focus to the
underlying desktop Programs icon. Start now saves/restores the prior focus on
keyboard toggle, Back and outside-click dismissal. The public native regression
covers Close focus in About and selected-row focus in Programs. Re-run the full
suite and verify the corrected published SHA; earlier five jobs are not that
new SHA's gate. No physical result is added.


Final focus correction validation: the complete clean MinGW/Mesa host suite
passed again on the corrected source/test tree (including all six E9 software
groups, Stage C, filesystem/core/USER/legacy sessions, actual GL composition,
input and dialogs). The strict standalone shell harness passed with the new
About/Programs focus restoration cases. Exact corrected-SHA CI must be checked
following publication; the preceding 437de20 CPU job was still installing GCC.


## 2026-10-24 F3a: real directory explorer and direct path launch

Fetched clean 12c11372896cded9b19cd98bc41cab79cf84c351; exact CI run 40027919660
passed all six jobs. No newer changes or AGENTS.md were found/overwritten.
ShellBrowser now owns separate host VFS metadata over the actual persistent F1
C/S/R roots, with real My Computer/My Documents, directory/file/size list,
back/up/address/refresh, directory-first name/size sorting, pages, quick roots,
readonly labels, retained selection/history and explicit errors/truncation.
Y cycles controls for controller-only access. Original F2 initial sole-EXE
launch, L/ZL mode override, console fallback and joined guest teardown remain.

Start/catalog, Explorer and path-only Run share a guest/native ShellLaunchRequest
validator. Run accepts one EXE or directory path (optional surrounding quotes),
not an argument-bearing command line/PATH search/host execution. Failed/missing/
invalid/linked/unsupported paths clear pending launch and preserve the view.
Documents remain visible without associations; Open is disabled and keyboard
attempts report the limit. No fake Paint/open support or complete F3 is claimed.

Enumeration caps retained entries at 256, scanned non-dot entries at 4096 and
history at 32; no recursion or retained native directory/file handles. The
retained subset is deterministic within the scanned prefix; sorting operates on
that subset. Code review caught that directory enumeration alone did not bound
F1 case-insensitive resolver searches. Vfs ToHost/ToGuest/SetCurrentDirectory now
accept an optional shared case-search entry budget for shell callers, failing
with empty/EOVERFLOW; ordinary guest callers remain unbudgeted/compatible.
F1 additive startup configuration remains a separate operation. A failed budgeted
readonly-path probe closes DIR and preserves the error rather than guessing.

Native regression covers actual C/S/R fixture data, Unicode/case/readonly,
unknown/UNC/stream/linked/oversize paths, no native chdir, item/scan/history
limits, sorted subset, stale requests, cancellation/focus and common launch
parity. Strict isolated build passed; early test-only auto declaration/indentation
compile issues were corrected. Native screenshots of computer/documents/explorer
were inspected. Initial renders were refined to avoid calling a failed/filtered
empty listing an empty folder and to label volume quantities as available space.

The existing public filesystem EXE now enters four writer/reader/abort/reader
sessions through native catalog/Run/Explorer, preserving its sibling DLL, actual
Unicode saved bytes, modal A/W dialog and readonly assertions. The host shell
survives GuestSession teardown and returns to its former directory. Full suite
and final strict-render checks must complete on the final tree before publishing.
See F3_EXPLORER.md. Local Switch SDK remains unavailable: libnx keyboard/output/
input transitions require exact published-SHA Switch CI and deferred hardware
checks, not inference from host tests. SD diagnosis stays closed.

Next package: F3b argument-bearing launch transport and explicit document app
associations with public fixture; remaining supported Start/settings and tree
refinements. F4 active-guest taskbar/compositor/input ownership and F5 persistence/
save-find-reopen remain open. Do not jump to G optimization or claim F complete.


Final navigation review also covers a vanished My Documents directory selected
from Start: failure returns focus to the previous visible panel/control instead
of leaving focus on a closed menu item. A strict public regression removes that
fixture folder and checks preserved view/focus/error. The preceding full suites
passed; a clean release-suite rerun on this final focus/test tree is the final
local gate. Readonly/empty-list wording and reduced-budget exhaustion tests are
included in that tree; no earlier/stale object result substitutes for it.

Final F3a local validation: complete clean `COMPOSITION_GL_TEST=1 sh
tools/run_host_tests.sh` passed on the release source/test tree after final
vanished-folder focus and budget/error refinements. New native browser and F2
regressions, four actual-x86 Start/Run/Explorer filesystem sessions, all six E9
software groups, Stage C, CPU/actual-GL composition at both sizes, input/dialog
and core/USER/legacy lifecycle checks passed. Strict isolated browser build also
passed -Wall -Wextra -Werror -fno-exceptions -fno-rtti. Changed Markdown local
links/fences and git diff --check passed. The incoming branch was fetched again
at unchanged 12c1137 before publishing; no force/reset is used. Exact new-SHA CI
must be checked following push; local host builds do not compile libnx main.


## 2026-10-24 F3b: document application choice and full argument transport

Incoming clean 12f0114fd53e6f5d042a76a75d3feca5b41c5338 matches the fetched branch;
CI run 40031884729 passed all six jobs. No newer changes/AGENTS.md were found.
Added typed arguments/encoded guest command line to ShellLaunchRequest; production
selection clears/copies that transport per launch and passes it into Process.
Run supports explicit EXE + arguments, directory/document paths and strict quote
errors; no PATH/native shell execution or guessed executable-prefix search.

Explorer Open on an unassociated document prompts for an explicit guest EXE;
Choose application can replace it. Extension mappings are bounded (32 types,
1–16 ASCII extension chars), remembered across GuestSession teardown in the
host shell and cleared on homebrew exit/successful root reconfiguration. No
on-disk preferences or guessed/verified Paint association is claimed. Generated
document arguments are absolute guest paths; missing applications/documents fail
and clear requests. Cancellation preserves mappings and view. No-extension files
can use one-shot Open With. Remaining settings/tree work is F3c, not F complete.

The shared bounded Windows quote/backslash codec covers 64 arguments/16 KiB,
empty/space/quote/slash/Unicode values. Process::GuestCommandLine unifies bootstrap
and Kernel32 strings while preserving legacy native-exe callers. Found and fixed
bootstrap CRT's former 63-byte/argc=1 limitation with bounded 128 KiB storage and
full pointers/arrays; existing data-import slots stay stable. MSVCRT wide argv
now decodes UTF-8, and CommandLineToArgvW returns parsed UTF-16 in one LocalFree-
compatible process heap block. winrun quotes each host argument. This is bridge
UTF-8 and tested canonical Windows quoting, not complete ACP/all CRT quirks.

Native codec/browser tests cover quoting, limits, chooser/cancel, remembered and
uppercase types, full tables and missing apps. The public shell_document MinGW
reader passed four actual-x86 Run/chooser/remembered/reopen sessions in a targeted
harness, checking A/W command line >63 chars, narrow/wide CRT argv, bootstrap
arrays, CommandLineToArgvW/LocalFree and real Unicode/space/emoji document bytes.
It reads an existing public document; it does not certify Paint save/reopen.

Initial runtime build caught a missing files.h declaration in the new command-
line API source; fixed directly. The first fixture build used MinGW-private
startup declarations unavailable in its public headers; the test now declares
the existing __wgetmainargs ABI explicitly. No runtime compatibility workaround
was added for that compiler issue. The complete clean release suite on the final
source/fixture tree is required before publication. See F3_DOCUMENTS.md.
Local Switch SDK remains unavailable, so exact published-SHA Switch CI is the
libnx compile gate. Physical keyboard/Paint/GPU/fallback/input/relaunch checks
stay deferred; no SD diagnosis is reopened. Next: F3c preferences/association
management and remaining supported Start/settings/tree actions, then F4/F5.

Final F3b local validation: complete clean MinGW/Mesa
`COMPOSITION_GL_TEST=1 sh tools/run_host_tests.sh` passed on the final source and
public fixture tree. This includes the four new actual-x86 document sessions,
existing four filesystem sessions, core/USER/legacy lifetime regressions, Stage C,
all six E9 software groups, CPU/actual-GL composition at both sizes and input/
dialogs. Native quote/browser tests and strict command API/bootstrap compilation
passed -Wall -Wextra -Werror -fno-exceptions -fno-rtti. The native associated-file
render was inspected; Markdown relative links/fences and git diff --check passed.
Fetched unchanged 12f0114 again before commit. Exact published-SHA CI remains to
be verified after push; no local Switch SDK or physical acceptance is implied.


## F4a: Paint default and shared active workspace (latest user steering)

Fetched clean 9c6e4d4; its exact CI run 40036571514 passed all six jobs. No newer
branch work was overwritten. User requested new UI Paint without L, real EXE
windows over the desktop and visuals closer to XP. This deliberately brings F4
ahead of F3c; preferences/settings/tree remain open, not implicitly completed.

UseNewGuestUi now selects new USER/GDI for all EXEs, L explicitly requests legacy,
ZL requests software output. No application EXE is replaced. The worker-owned
ShellSession temporarily binds the host shell to exposed USER desktop painting,
pre-queue native input routing and topmost native output chrome. Same logical
1280x720 desktop, live Start/Explorer/About and one task button; second launch/
association changes/Run/homebrew exit are blocked while a guest runs. The single
task minimizes/restores visible top-level handles, with preserved normal/maximized
placement. Work-area APIs/maximize reserve the 40-pixel taskbar. Gestures retain
press/release ownership across native/guest boundaries. Prior shell panel/path,
platform hooks and work area are restored after draining on exit/error/abort.

Native visuals use plain XP-like blue background, highlighted blue titlebars,
red close control and icon-bearing Start with the existing licensed Tango/OFL art.
No Microsoft assets or new external art was imported; guest controls still have
their classic theme. This does not claim pixel-identical XP or arbitrary EXE support.

F4a uses three fallible 1280x720 RGB buffers (11,059,200 bytes) and conservative
flat CPU native/guest composition before GPU/software presentation. Independent
E6 window layers remain available/tested outside this adapter; F4b must integrate
retained native/guest layers and the full ownership oracle. No speedup claim.
See F4_WORKSPACE.md for exact boundaries and the public two-window x86 regression.

Full clean MinGW/Mesa suite, strict native checks, screenshots and exact new-SHA CI
are required before publication/acceptance. Local Switch SDK remains absent;
physical Paint/GPU/fallback/Lite/input/relaunch acceptance remains deferred. SD
troubleshooting stays closed. Next package: F4b retained workspace layers and
interaction/visual refinement, then F3c preferences/settings/tree, then F5.

Native overlay revisions request output separately from CPU GDI screen changes;
Start/clock rendering does not manufacture guest raster change/input-correlation
samples. Allocation failure is injected at each of the three buffers, and failed
EXE load also checks hook/workspace restoration. The first x86 harness iteration
incorrectly invoked window callbacks outside an active guest context; it now
performs scripted actions inside the production platform wait callback. No
runtime workaround for that harness error was introduced.

Shell interaction still depends on the active guest USER pump. CPU-only/non-pumping
workloads retain periodic PLUS abort, but bounded native-shell responsiveness
without that pump remains an explicit F4b requirement.

Final F4a validation: complete clean rootless MinGW/Mesa
`COMPOSITION_GL_TEST=1 sh tools/run_host_tests.sh` passed on the final source,
header and public fixture tree. New workspace normal/abort/restart, allocation
fault/failed-load, input/revision/work-area and second-launch guards passed;
all existing F1/F3b, core/USER/legacy session, Stage C, six E9 groups, both
CPU/actual-GL composition sizes and dialogs/input remained green. ShellSession,
DesktopShell and the new host harness also passed strict -Wall -Wextra -Werror
-fno-exceptions -fno-rtti compilation. Actual native x86 workspace screenshot was
inspected; the harness rejects emergency bitmap font fallback. Markdown links/
fences and git diff --check passed. An earlier intermediate full run had only
test-harness compile errors; another was stopped when native revision fields
changed, avoiding mixed-header objects. Neither is the final validation result.
Fetched unchanged 9c6e4d4 before publication. Verify the exact new published SHA's
six CI jobs after push; no local Switch SDK or physical acceptance is implied.


## F4b.1: independent native input and real owner-window requests

Incoming df54bdc was clean and matches the fetched working branch. Its CI run
40039274735 completed all six jobs successfully; the preceding 5/6 report was
only an earlier check during MinGW installation. No AGENTS.md/newer work found.

The user requests functional system work before further visual polish. Switch
now services ShellSession on the same worker between executor quanta, gated at
8 ms of monotonic time. It handles native input/presentation without calling
guest WndProc. A fixed 256-event ring retains guest input until the ordinary
USER pump; only adjacent motion coalesces. Overflow is explicit and cancels via
normal drain/return, never silently losing a release and continuing as success.
Physical async keys/cursor update at observation. Host-only metadata preserves
message modifiers/Alt/repeat/point/time so late delivery cannot rewind physical
state. Left/right modifier aggregation preserves the still-held side.

Task minimize/restore validates <=64 owners and posts actual WM_SYSCOMMAND to
their queues. It does not call guest code from the unbound host stack. One group
intent is pending at a time, with native awaiting feedback; only actual observed
window state confirms it. A busy/refusing app remains pending, while native
Start/Explorer continue. No process/thread is fabricated, forcibly minimized or
reported as responding because native controls work. USER-path Quit preserves
WM_CLOSE; independent host Quit/source closure/overflow have explicit statuses.

The public busy fixture covers seven x86 sessions including pure CPU spin,
async-only polling, a worker-owned window with main parked in HANDLE wait,
normal resume/command dispatch, pending abort, overflow, Quit and fresh restart.
A native input test checks modifier snapshots, repeat/points, ring wrap and EOF.
The old two-window test dispatches real x86 posted system commands before
inspecting geometry. Its immediate native-change assumption is intentionally
removed. A first new harness bug re-read an HWND after the guest destroyed it;
fixed in the harness, with no runtime workaround. Early test compile/name and
format warnings were corrected; only the final clean suite counts as validation.

Still open: output teardown/reinitialization between sessions, retained native/
guest GPU planes, folder/file mutations/properties, context menus and document
tools. A non-yielding host API can still stall service; no physical latency or
Switch/Paint acceptance is claimed. Local Switch SDK remains unavailable, so
exact new-SHA Switch CI is required. Hardware checks remain deferred; SD closed.
Concrete next: persistent frontend/output lifetime, then real file operations.

Final F4b.1 local validation: complete clean MinGW/Mesa
`COMPOSITION_GL_TEST=1 sh tools/run_host_tests.sh` passed on the final source,
headers and public fixture tree, including the pending-label readability fix.
New native physical-input/ring tests, seven actual-x86 busy-shell sessions,
two-window workspace, F1/F3b filesystem/document sessions, core/USER/legacy
lifetimes, Stage C, all six E9 groups, both CPU/actual-GL composition sizes and
input/dialog regressions passed. New ShellSession, USER input module, native
renderer and test harnesses compiled with -Wall -Wextra -Werror -fno-exceptions
-fno-rtti. Markdown links/fences and git diff --check passed. Fetched unchanged
df54bdc again before publication. Earlier intermediate runs had harness compile/
post-destruction inspection failures; only the final clean run is the package
result. Verify all six CI jobs for the exact published SHA after push. Local
Switch SDK/hardware limitations above still apply.


## F4b.2: persistent frontend/output ownership

Fetched clean 450f1d3, matching the requested branch; no newer work/AGENTS.md
found. Its six CI results were verified previously and again through the public
run page at the exact full commit link. This environment's GitHub API initially
returned 401 (Bad credentials), while Git/public web access worked. The user
already grants full repository access; do not request permission again. API
access subsequently recovered, and the exact incoming SHA's six CI jobs were
verified again via API. No token is exposed or stored in the repository.

One 2 MiB frontend worker now owns native selection, load, guest execution, output
and final shutdown, preserving the large-stack safety requirement. RunWorkspace
and OutputLifetime separate per-guest begin/drain/end/return from device Close.
Same-policy launches reuse EGL/framebuffer/shaders/presenter; only explicit L/ZL
mode changes or real device failure require reopening. Software fallback persists
across same-policy launches, with retry through policy change/frontend restart.
Native desktop and loading/return frames use the same actual output and owned
Presentation, preserving cursor position and startup input quarantine. Legacy
direct-framebuffer output explicitly invalidates presentation history on return.
No fake app surface/second Process or console transition is used for normal return.

Guest End still writes renderer metrics and clears guest layer snapshots, while
device/context ownership remains with the frontend. GPU ResetStats preserves
resources and discards old ended timer samples without waiting, so idle/previous
guest measurements do not enter a new scope. Native idle frames do not complete
guest input samples. Output failure is latched and recovered through the existing
safe cleanup/picker fallback. See F4B_OUTPUT_LIFETIME.md for exact boundaries.

New public x86 output-lifetime harness checks one device across normal/load-error/
abort/restart, real GDI pixels, whole desktop return, redraw after guest memory
destruction, modes/partial-open failures and shutdown. Real EGL/Mesa scene tests
check reset counters with retained textures/revisions, no old queries and exact
pixel output. Full host/MinGW/Mesa and strict checks are required before publish;
Switch-only main still needs exact-SHA CI. Local SDK package/installer endpoints
return HTTP 403; do not claim local Switch compilation. Physical consolidated
Paint/GPU/fallback/input/relaunch acceptance remains deferred; SD stays closed.

Native/guest separate retained GPU planes still remain open; this package retains
the F4a flat native composition. Next: common real file operations/properties,
then input/context actions, persistence and TXT/images/F5. Visual polish deferred.


Final F4b.2 local validation: complete clean `COMPOSITION_GL_TEST=1 sh
tools/run_host_tests.sh` passed on the final common runtime/test/fixture tree.
The persistent-output x86 normal/load-error/abort/restart and shutdown cases,
GPU counter/cache/old-query reset pixel oracle at both sizes, seven busy-shell
sessions and all previous native/core/USER/legacy/F1/F3b/Stage C/E/input/dialog
regressions passed. Strict no-exceptions/no-RTTI compilation passed for the new
controller harness and common GPU module. Markdown local links/fences and
git diff --check passed. Only Switch adapter return/console cleanup and comments
were refined after the first successful suite; the final clean rerun passed.
Local tests do not compile libnx main: verify exact published-SHA Switch CI and
all six jobs after publication. Physical checks remain deferred.


Switch CI for initial F4b.2 commit 82ae340 caught a scope error in legacy
DrawGdiScreenIntoFramebuffer: a broad guard replacement referred to the new
renderer namespace's g_outputFailed flag from the separate legacy function.
The correction restores that function's original no-framebuffer return; new
renderer failure latching remains scoped to newui::Blit. The complete common
runtime/fixture host suite is unchanged and remains passed; main.cpp is excluded
from it. Verify the corrected exact SHA's Switch build and all six jobs, not the
failed 82ae340. No physical acceptance is added.


## Functional F: shared real file operations and properties

Fetched clean 975f393/FETCH_HEAD on the requested branch; no newer changes or
AGENTS.md found. Exact incoming CI 40085203929 is successful. Rootless host
MinGW/Mesa tools remain available; devkitA64/libnx is still absent locally.

VFS now owns actual create/delete-file/remove-empty-directory/move/property
operations. Win32 A/W and CRT mutations share them with ShellBrowser. DeleteFile
and CRT remove no longer accidentally delete an empty directory through POSIX
remove. Default rename rejects collisions; explicit replacement is file-only.
Mounted roots/ancestors are protected even through the overlapping SD alias.
Properties use actual stat kind/file bytes/access+modification times and separate
mount readonly/owner-mode-bit metadata, with no fabricated birth time or recursive
folder size. Errors and optional cumulative path-lookup budget propagate.

Browser model operations revalidate selection and distinguish actual mutation
failure from success followed by listing-refresh failure. **No new DesktopShell
buttons/context menu/properties dialog are connected yet.** Public native tests
inspect real bytes and rejection paths; the existing four-session x86 filesystem
fixture now also tests actual Win32/CRT mutations and wrong-type/collision errors.
See F_FILE_OPERATIONS.md for limitations, including case-only rename, non-atomic
no-replace versus native writers and existing cwd/handle sharing boundaries.
CopyFile's old unchecked I/O remains open; implement checked copy before shell
exposure. Next concrete package: usable native name/actions/confirmation/properties
UI with pointer/controller routing, followed by persistence/TXT/images/F5.
Host full-suite and exact new-SHA CI results must be verified before completion;
no local Switch build or physical acceptance is inferred. SD diagnosis is closed.


Final local validation for this foundation: complete clean rootless
MinGW/Mesa `COMPOSITION_GL_TEST=1 sh tools/run_host_tests.sh` passed, including
shared file operations, all four extended actual-x86 mounted filesystem sessions,
output/input/workspace, core/USER/legacy sessions, Stage C, six E9 groups and
both real EGL pixel comparisons. Final extended native mutation assertions also
passed strict -Wall -Wextra -Werror -fno-exceptions -fno-rtti compilation and
execution. The first standalone command incorrectly named a nonexistent
command_line.cpp (its implementation is header-only); corrected command passed.
Markdown local links/fences and git diff --check passed. No local Switch or
physical result is claimed. Verify exact published-SHA six-job CI after push.


## Functional F: native file actions and modal properties/deletion

Fetched clean b46b378/FETCH_HEAD on the requested branch; incoming exact CI
40086771006 passed all six jobs. No AGENTS.md/newer work found. Sidebar actions
now expose actual create/rename/delete and stat-backed properties through the
common provider. Name cancellation is inert; permanent delete defaults to Cancel.
Modal/input freezes path/selection/paging/Start, captures path/type and revalidates
before deletion. Errors remain visible, success-with-stale-list is separate, and
properties query actual storage anew. No recursive delete/copy/context menu is
exposed. See F_FILE_ACTIONS.md for the exact contract and race limits.

ShellSession consumes modal input and blocks task commands without guest callbacks
or another Process. Idle keyboard labels distinguish new folder/rename. Active
Switch text input now uses the shared UTF-8 path codec instead of dropping non-BMP
characters, and restores output after cancellation as well as acceptance. Local
SDK remains absent; only exact new-SHA Switch CI checks these libnx paths.

The new native harness uses real storage and actual controls/controller focus,
fresh properties/screenshots, invalid/collision/readonly/nonempty errors,
default cancel, frozen target and disappeared/type-changed objects. The existing
seven-session x86 busy fixture also performs native file actions with no guest
message pump and checks no leaked input, modal Start/task guards and real bytes.
First native test compilation incorrectly assumed OwnedSurface::Allocate returns
bool; corrected to check the surface after allocation. An intermediate assertion
used an incorrect expected string length; final byte-size checks use the actual
18/19-byte content. No runtime workaround was added for either harness issue.

Full host/MinGW/Mesa validation and exact published-SHA six-job CI remain the
completion gates. Hardware checks are still deferred and SD diagnosis closed.
Concrete next: shared checked copy with error/partial-file semantics before a
Copy action, then context/input, preferences/TXT/images/F5; visual polish deferred.


Final local native-action validation: the complete clean rootless MinGW/Mesa
`COMPOSITION_GL_TEST=1 sh tools/run_host_tests.sh` passed on the final common
runtime/header/fixture tree, including the new file-action test and extended
seven-session actual-x86 busy-shell test, all earlier mount/document/core/USER/
legacy/workspace checks, Stage C, six E9 groups and both real EGL pixel oracles.
Strict no-exceptions/no-RTTI native action and ShellSession compilation passed;
final native Explorer/properties/delete screenshots were inspected. Only the
Switch-only keyboard codec/cancel-redraw correction followed the common compile;
main.cpp is excluded from host tests and needs exact new-SHA Switch CI. Markdown
links/fences and git diff --check passed. No local SDK/hardware acceptance is
implied; exact published-SHA CI must be checked after publication.


## Functional F: checked byte-copy provider and installer/archive plan

Fetched clean 6ac9aa0/FETCH_HEAD on the working branch, no newer changes or
AGENTS.md. Exact incoming CI 40087919426 passed all six jobs. User requests future
EXE installation to proper persistent folders with real desktop shortcuts, plus
ZIP/RAR/other extraction. Recorded F_INSTALL_AND_ARCHIVES.md and linked the
active/global/shell maps: after baseline F5, actual installer/shortcut/child-
process/API compatibility and archive extraction gates, not universal support.
No dependencies/download manager or installer implementation is added now.

VFS checked CopyFile now serves KERNEL32 A/W and ShellBrowser CopyHere. An
exclusive sibling stage, 8 KiB buffer, checked short/EINTR data I/O, fsync and
both closes precede native publication. Same-object C/S/case/hard-link aliases,
readonly destinations/roots/links/types and collisions are rejected. Failures
preserve old source/destination and first errno; cleanup attempts only the owned
stage. Stage cleanup failure/crash residue, native-writer races, source mutation,
metadata and synchronous latency limits remain explicit in F_FILE_COPY.md.
Switch replacement-rename support is not assumed: unsupported replacement fails
without deleting the old file. Native Copy/Paste controls remain unimplemented;
next package is destination/clipboard UI plus coherent context/controller input.

New real-file tests inject open/create/read/short-write/zero-write/fsync/both-
close/publication/collision/late-destination/cleanup errors and check bytes,
first errors, descriptor lifetime and staged residue. The four-session public
x86 fixture exercises actual CopyFileA/W, Unicode, collision/self-copy/readonly
failures and copied bytes. Initial native expectation incorrectly assumed full
Cyrillic case folding, beyond F1's current resolver: changed it to exact Unicode
names with ASCII-case extension. No runtime Unicode-collation claim is added.
An initial full suite was stopped when the browser-copy model was added; only
the later clean final-tree suite is the validation result.

Final local validation: complete clean rootless MinGW/Mesa
`COMPOSITION_GL_TEST=1 sh tools/run_host_tests.sh` passed on the final code/header/
fixture tree, including new copy faults/browser copy, extended four x86 filesystem
sessions, native file actions, busy guest/workspace/lifetime regressions, Stage C,
six E9 groups and both real EGL pixel oracles. Strict no-exceptions/no-RTTI copy
harness/module compilation and execution passed; Markdown links/fences/diff checks
also passed before publication. Exact new-SHA six-job CI must then be verified. No local Switch SDK
or physical acceptance is inferred; hardware list stays deferred and SD closed.
