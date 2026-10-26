# Active development handoff — switch-recompiler

Branch: `claude/wonderful-hawking-10mpr0` only. Incoming verified HEAD:
`72545db7d2785e50a95294d387b0609b379c832c` (2026-10-26).
[CI 40343116185](https://github.com/Omeruno/switch-recompiler/actions/runs/40343116185)
passed all six jobs: host/MinGW, CPU oracle, actual OpenGL/EGL reference,
ARM64/QEMU continuation ABI, Switch build and isolated JIT probe build.
This document and [ACTIVE_ROADMAP.md](ACTIVE_ROADMAP.md) are the current technical summary and implementation queue. Earlier investigative material
has been removed from the active handoff; use current source and subsystem docs,
not obsolete E1/common-dialog plans. No private hardware reports are reproduced.

## Current position after G4b.2d.2 (2026-10-26)

Bounded F0–F5 software acceptance is complete; physical E/F remains deferred.
G2 CPU SRCCOPY is active. Claude's five Luna updates and shared PanelArea fix
are preserved. G3/G4a/G4b.1 implement isolated actual-GL GPU-GDI targets,
readback/recovery, generation/lifetime checks, direct texture composition and
ordered instanced fill batches. They are NOT activated for live Paint/Win32 GDI.

G4b.2a implements bounded GL-state preservation and interleaving with the real
production renderer; see [contract/evidence](G4B2_SHARED_CONTEXT.md).
G4b.2b adds [real HWND/DC native identity/resource leases](G4B2B_WINDOW_BINDINGS.md)
and rejects CPU DC aliasing onto reused HWNDs. It is not automatic GDI dispatch.
[G4b.2c](G4B2C_CPU_COHERENCE.md) implements explicit native opt-in CPU/GPU authority,
actual Win32 read/ROP/DIB/layout/fallback barriers and weak session-owned gates.
[G4b.2d.1](G4B2D_RESIDENT_FRAMES.md) adds owning resident-resource frames with
retirement budgets, reset revocation and real fallback. Ordinary GDI dispatch is
still CPU-only. [G4b.2d.2](G4B2D2_RESIDENT_PRESENTATION.md) allows resident
textures in the real production renderer with ownership/CPU overlays/cursor.
Next: owned mixed persistent frames and live-window/shell integration,
remaining direct-host-access/fault audit and bounded ordinary GDI admission.
Current ShellSession CPU canonical memcpy cannot remain plus duplicate GPU work
and be called an optimization. Actual-x86 GDI/window/readback/lifecycle fixtures
and CPU/EGL pixel/fallback comparisons precede selective activation. No physical
test request is needed to progress this work.

Global/current scope: [PROJECT_STATUS_RU.md](PROJECT_STATUS_RU.md),
[ROADMAP.md](ROADMAP.md), [ACTIVE_ROADMAP.md](ACTIVE_ROADMAP.md). These are now
consolidated current maps, not append-only competing historical next steps.
D API/page protection/unwind/COM and Pinball x86 remain open. Installer/ZIP/RAR,
full native window actions/Luna polish, multi-Process and H DirectDraw/D3D are
separate incomplete work; minimal JIT probe42 is not a dynarec acceptance.
Keep all no-stub/correctness/lifetime rules below. Detailed package history
follows; its old future-tense queues do not supersede this current position.

## User's long-term product target

Spore, Mirror’s Edge and The Sims 3 are explicit future PC-game targets; none is
certified working. [Architecture/acceptance direction](TARGET_GAMES_RU.md).
Build common compatibility/performance paths, not a Paint-specific accelerator.
Full ARM64 dynarec, GPU API/shader translation (audit D3D9 first), audio,
memory/streaming and thread correctness are separate required milestones.
Use real Horizon/homebrew backends within available system budgets; x86 is not
native ARM64 and DirectX is not a native Switch GPU API. No guaranteed100% usage
or playable FPS without version-specific physical evidence. Hardware remains
deferred; current next implementation is owned mixed persistent frames and live-window/shell integration.

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


## Functional F: native Copy/Paste and resumable checked copy

Fetched clean be95ecb/FETCH_HEAD on the working branch, no newer work/AGENTS.md;
incoming exact CI 40092668937 passed six jobs. Installer/archive future scope
remains recorded. FileCopyJob now owns the checked-copy state machine, mount/cwd
snapshot, native paths/descriptors/stage and 8 KiB buffer. Native service performs
at most eight phases per call, with short-write/EINTR resumption and cancellation.
Data read/write, fsync, both closes and final publication remain checked. The
last destination check and rename never yield between them. Synchronous VFS/
Win32/CopyHere use the same engine, preserving the prior failure contract.

Native Explorer Copy remembers an actual file path/name; Paste accepts a new
name in the current folder. It never overwrites. Actual staged bytes are shown
in a native cancel-only progress dialog; selection/path/Start/task input is
frozen. Idle selection and independent active-guest native service advance the
job on their existing owner thread. No new thread/Process or guest callback is
used. Re-entry, guest return, selection exit/display failure and destruction
cancel unfinished stages; file clipboard paths survive ordinary guest sessions.
See F_FILE_PASTE.md for work-count/I/O-time, guest/native writer, cleanup/durability
and supported one-file-only limits. No wall-clock responsiveness or throughput
claim, hardware acceptance, directory copy or context menu is added.

Direct tests cover incremental writes, cancellation/destruction at several
phases, late destination collision and fresh restart; old injected I/O faults
exercise the same engine. Native shell checks real bytes and Copy/Paste naming,
readonly/collision/missing/invalid source/name, controller/modal and scope cancel.
Seven actual-x86 busy sessions now also copy to My Documents while the guest
heartbeat advances between chunks, compare every byte and cancel another copy
without leaked guest input. One initial new harness incorrectly called EndGuest
without SetGuestActive and then assumed Explorer restoration; corrected the test
lifecycle rather than changing runtime return behavior.

Required gates: final clean MinGW/Mesa full suite, strict compile/execute checks,
actual native screenshots, Markdown/diff checks and exact new-SHA six-job CI.
Local Switch SDK remains absent and physical tests deferred; SD stays closed.
Next concrete package: right-click/context/controller actions, then persistence,
TXT/images/F5; installer/shortcuts/ZIP/RAR remain separate planned work.


Final local Copy/Paste validation: the complete clean rootless MinGW/Mesa
`COMPOSITION_GL_TEST=1 sh tools/run_host_tests.sh` passed on the final source,
header and public-fixture tree. It includes resumable/cancel/destructor/late-name
copy cases, prior injected copy faults, native real Copy/Paste, seven busy x86
copy/heartbeat/cancel sessions, all prior filesystem/document/workspace and
core/USER/legacy lifetimes, Stage C, six E9 groups and both real EGL pixel oracles.
Strict -Wall -Wextra -Werror -fno-exceptions -fno-rtti copy job/browser/native
harness checks and the extended busy harness passed. Actual native Explorer and
copy-progress screenshots were inspected. Markdown links/fences and diff checks
passed. Only documentation changed after the full build. Verify all six CI jobs
for the exact published SHA; local SDK and physical limitations still apply.


## Functional F: native object/current-folder context menus

Fetched clean d8ad9a1/FETCH_HEAD on the requested branch, no newer changes or
AGENTS.md found; incoming exact CI 40098135433 passed six jobs. Explorer now has
row right-click object menus, blank-list current-folder menus and reachable File/
Folder titlebar buttons. Menu/Apps key is handled only with native keyboard focus;
idle ZR now matches the existing active new-USER secondary button. Supported
commands invoke the existing production actions; readonly/one-guest restrictions
remain, folder copy/Cut/archive/installer actions are not fabricated.

Menus retain at most six rows, clamp geometry/shadow to the existing Explorer
composition rectangle, capture path/type and validate before dispatch. Paging/
selection/Start/task commands freeze; outside primary click or another right
press dismisses without click-through/reopening. Paired native/guest gesture
ownership remains intact. Hover changes menu focus, enabled entries cycle by
controller/keyboard, and dialogs inherit actual targeting/default Cancel. All
context state clears on frontend/guest transitions. See F_CONTEXT_ACTIONS.md.

New real-storage tests cover context Copy/Paste/create/rename/delete/properties/
navigation, geometry, pointer pairing/hover/controller, readonly and stale/type-
changed objects. Existing seven busy-x86 sessions also exercise right menus,
Menu-key and Start/task isolation, context Copy and guest-owned right gestures
before their existing copy/heartbeat/queue/restart checks. Initial harness strict
compilation required separating a post-loop close assertion; its click helper
also initially confused browser-row IDs with context IDs. Fixed those test
issues without runtime workarounds. A preliminary full suite was stopped after
the final hover-focus fix; only the clean final-tree run counts as validation.

Required gates remain full clean host/MinGW/Mesa, strict compile/execute,
actual native screenshot/link/diff checks and exact published-SHA six-job CI.
Local Switch SDK and physical acceptance remain unavailable/deferred; SD closed.
Next concrete: bounded persisted preferences/associations plus management and
restart/error checks, then TXT/images/F5. Input refinements, retained workspace
layers and installer/shortcut/archive future scope remain explicit.


Final local context validation: complete clean rootless MinGW/Mesa
`COMPOSITION_GL_TEST=1 sh tools/run_host_tests.sh` passed on the final source,
header and public-fixture tree, including context storage/hover/focus tests,
seven busy-x86 right/Menu-key/context-Copy/isolation sessions, all previous native
file actions, incremental copy faults/cancel, filesystem/document/workspace and
core/USER/legacy lifetimes, Stage C, six E9 groups and both actual EGL oracles.
Strict -Wall -Wextra -Werror -fno-exceptions -fno-rtti native context harness,
ShellSession and extended busy harness checks passed. Final actual context
screenshot inspected; unsupported decorative glyphs were removed from titlebar
button labels. Markdown links/fences and git diff --check passed. Only docs
changed after the final build. Verify exact published-SHA six-job CI after push;
no local Switch SDK or physical acceptance is implied.


## F3c slice: real persistent sorting/associations and native management

Fetched clean 62b50f0/FETCH_HEAD on the requested branch, no newer work/AGENTS.md;
incoming exact CI 40099366270 passed six jobs. ShellPreferenceStore now reads
bounded versioned CRC32/generation records from two owned F1 profile slots.
Save checks short writes/fsync/close in the other slot, retaining the previous
valid record without assuming Horizon replacement rename. Missing/default,
recovered/corrupt/future-version/I/O and unconfirmed-save outcomes are distinct;
unknown versions/read errors block writes. One frontend writer, no NT registry
or power-loss-proof filesystem guarantee. See F3C_PREFERENCES.md for details.

Browser restores actual sort/mappings, saves accepted sort/association changes,
and exposes reassign/forget/retry. Start -> Shell settings provides those real
controls; persistence warnings remain visible and failed save does not fake a
durable result or suppress a valid document launch. Reassignment captures an
extension independently of selection. Dialog composition now includes its actual
rectangle over the desktop panel while a guest runs, retaining input ownership.
Settings cover sort and at most 32 extension/EXE mappings, not full OS settings,
last folder/theme/input/application state or arbitrary EXE compatibility.

New native tests compare real slot bytes, fresh-shell restoration, malformed/
CRC/truncated/oversized/future/readonly data, duplicate/maximal records and
partial/open/read/fsync/close/short I/O failures with descriptor cleanup. They
exercise controller/pointer management and genuine changed/removed mapping loads.
The document harness now runs a fifth actual-x86 session from a new shell using
its restored Unicode document association. The earlier association-limit test
uses an isolated profile root rather than assuming saved mappings disappeared.
Seven busy-x86 sessions inspect exact composed settings pixels and actual sort
saving. Initial test enum names collided with virtual method names; scoped them.
A strict indentation warning was fixed; neither required runtime workarounds.

Complete clean rootless MinGW/Mesa full suite passed on the final runtime/header/
fixture tree. Additional final native maximum-record/CRC/oversize/controller
assertions passed strict no-exceptions/no-RTTI compilation and execution after
that suite. Source/runtime was unchanged. Native settings screenshot inspected;
remaining strict/link/diff checks and exact new-SHA six-job CI must be verified
before completion. Local Switch SDK remains absent; physical acceptance stays
deferred and SD troubleshooting closed. Next: real TXT read/edit/save, then
supported image viewing/F5; installer/shortcuts/ZIP/RAR remain planned work.


Final preference validation gates: complete clean rootless MinGW/Mesa suite
passed, including native dual-slot/settings tests, five actual-x86 document
sessions (one from restored state in a fresh shell), seven busy-x86 native
settings/pixel/save cases and all previous lifecycle/file/context/copy/input,
Stage C, six E9 and both actual EGL oracle checks. Final extended native record/
controller assertions, preference provider/UI and extended busy harness passed
-Wall -Wextra -Werror -fno-exceptions -fno-rtti compilation/execution; ShellSession
strict compilation passed. Actual final settings screenshot inspected; Markdown
links/fences and git diff --check passed. No runtime change followed the full
suite; only extra native assertions/docs. Verify exact published-SHA six-job CI.
Local SDK/physical limitations and the separate installer/archive plan remain.


## 2026-10-25 Functional F: real bounded native TXT documents

Fetched clean a327117/FETCH_HEAD on the requested branch; no newer work or
AGENTS.md found. Incoming exact CI 40101548903 passed six jobs. Local host tools
remain usable; devkitA64/libnx is absent, not a local Switch-build claim.

TextDocument now reads at most 64 KiB of strict UTF-8 or BOM UTF-16 LE/BE, preserves
BOM/supplementary text/individual CRLF/LF/CR endings, and supports real line edits,
insertion/removal, dirty state, Save and no-overwrite Save As. It checks actual
file bytes for conflicts before and after staging. Checked exclusive stage I/O,
fsync and close precede a backup-first two-rename publication that does not need
Horizon overwrite rename. Old bytes remain in an ordinary sibling .bak, even on
success; failed publication attempts restoration only to a vacant old name.
Recovery/cleanup residues are explicit. No transaction/power-loss/native-writer-
race safety or bounded I/O latency is claimed. See F_TEXT_DOCUMENTS.md.

Native Explorer Folder/New TXT and File/Text commands expose the actual model.
Five-line view, line selection/horizontal scroll, keyboard line editing up to
500 UTF-16 units, insert/erase, Save/Save As and default-Cancel dirty-close flow
work with pointer/controller/keyboard focus. Long lines remain scrollable and
are never truncated into the smaller keyboard. Active/idle Switch line adapters
set the length/return-key limits; oversized initial Save As paths are left empty.
Native text modal state survives ordinary guest end/re-entry, with filesystem
reconfiguration refused while open. Associations/ordinary EXE Open are preserved;
this is native tooling, not extra Process/Notepad or general EDIT compatibility.

New real-storage/model/UI tests inject short/EINTR/read/write/full/flush/close/
move/publish/rollback/cleanup errors and model Horizon no-overwrite rename. They
check exact bytes, codecs/endings/limits, conflict/readonly, dirty cancel/discard
and fresh reopening. The existing public x86 document reader now consumes bytes
created and subsequently edited/saved by this model. Seven busy x86 sessions
exercise the real native TXT modal, saving, composed pixels, input isolation and
continued guest heartbeat. Initial strict-test indentation and fixture string-
length/lifecycle selection assumptions were corrected directly; successful saves
now select the actual new document after refresh. No runtime compatibility stub.

Required completion gates: full clean host/MinGW/Mesa, strict targeted checks,
actual native screenshot, Markdown/diff checks and exact published-SHA six-job
CI. No physical Switch result is inferred; Paint/GPU/fallback/input/relaunch
remain deferred and SD troubleshooting closed. Next: supported images, then F5.
Installer/desktop shortcuts/ZIP/RAR and retained native/guest planes remain open.


Final TXT validation: complete clean rootless MinGW/Mesa
`COMPOSITION_GL_TEST=1 sh tools/run_host_tests.sh` passed on the release runtime/
headers/public fixtures, including the final long-line native assertions. New
strict native TXT harness and extended seven-session busy-x86 harness passed
-Wall -Wextra -Werror -fno-exceptions -fno-rtti; ShellSession strict compilation
also passed. Five actual-x86 readers consumed a native-created, edited and saved
Unicode document; all prior filesystem/copy/context/preferences/lifetime/input,
Stage C, six E9 groups and both actual EGL oracles remained green.

A final added close/save test exposed that guest end restored the old panel while
the native editor retained its modal; EndGuest now preserves the editor's current
panel/focus as well. An intermediate suite was stopped after that correction,
then the full clean release suite above passed. Unix absence checks also reject
dangling links for generated publication names; Switch uses its native stat.
Successful save-and-close retains the backup notice in the surrounding Explorer.
No runtime source changed after the release suite. Actual native screenshot
inspected with a real-font assertion; eight Markdown docs/98 relative links,
balanced fences and git diff --check passed. Fetched unchanged a327117 again;
normal publication and exact new-SHA six-job CI verification follow. Local SDK
remains absent and physical acceptance remains deferred. Next: image viewing.


## 2026-10-25 Functional F: bounded native BMP and global status

Fetched clean 3536ec4/FETCH_HEAD, no newer work or AGENTS.md; incoming exact
CI 40134592527 passed six jobs. User asks to continue and explain overall progress
(the many file packages felt like standing still). Added PROJECT_STATUS_RU.md:
we are in functional F, with A/B/C execution base, D compatibility and E physical
gates explicit, G timing/optimization and H/I/installer/archive work still open.
No invented percentage or claim that native tools mean broad EXE compatibility.

BitmapDocument owns bounded actual F1 image reads and decoded RGB: BMP 40-byte
INFOHEADER, uncompressed BI_RGB 1/4/8/24/32, palettes/padded rows/top-/bottom-up.
Checks signed dimensions, stride/offset/palette/file fields and 16 MiB/4 Mpx/8192
limits before pixel allocation; fails on invalid indices/input rather than
fabricating pixels. Existing Process/USER DIB/icon/parser behavior is unchanged;
PNG/JPEG/RLE/bitfields/V4/V5 are unsupported, no new external decoder. Read and
close errors/short I/O/bounded EINTR are checked; old snapshot survives failure.

Explorer File/right context exposes actual BMP view, fit/native-size and arrow
panning. Draw is clipped and samples owned pixels; normal guest teardown/re-entry
retains the modal/panel/pan, Close frees image data, filesystem reconfigure is
blocked while open. Native input/one guest/explicit associations remain intact.
No storage/decode wall-clock responsiveness or total heap claim is made.

New native codec/storage/UI tests generate public BMPs and check depths/row
orientation/palettes/limits/truncation/mutated headers, actual viewport fit/pan/
clip pixels, readonly/Unicode/link/error/FD paths and controller/modal/lifetime.
Public shell_bitmap.c draws real GDI into a DIB and writes actual BMP bytes. The
new harness uses Start/Run -> guest save -> native Explorer/pixel comparison ->
fresh shell/x86 reopen. Its launch request is copied before Attach clears native
pending launch; no runtime workaround. Seven busy-x86 sessions also view/pan/
compose with exact pixel and no-input-leak checks while the guest heartbeat runs.

Next concrete F5 system gap found by audit: K32_FlushFileBuffers ignores fflush
failure, always succeeds even for invalid handles, and omits fsync. Inspect
write/close/size/truncate errors and duplicated object lifetime when fixing it.
This BMP fixture avoids that false-success API and independently checks actual
reopened bytes; no durability/error-path F5 acceptance is claimed. Full F5 and
F3/F4 remain open; this corrects any temptation to call the happy path complete.

Required completion gates: final clean host/MinGW/Mesa, strict/native/public
checks and screenshot, docs/link/diff, normal branch push and exact new-SHA six
CI jobs. Local devkitA64/libnx is absent; hardware Paint/GPU/fallback/Lite/input/
relaunch remain consolidated/deferred and SD diagnosis stays closed.


Final BMP local validation: complete clean rootless MinGW/Mesa
`COMPOSITION_GL_TEST=1 sh tools/run_host_tests.sh` passed on the final runtime/
header/public-fixture tree: new native BMP test, actual x86 GDI save/browse/
fresh-session reopen, seven busy-x86 image/text/context/input sessions and all
previous filesystem/guest lifetime/Stage C/six E9/both actual EGL oracles.
Additional final exact-4-Mpx/8192-width native assertions passed strict native
compilation/execution and AddressSanitizer + UndefinedBehaviorSanitizer with
leak detection and halt-on-UB, after the full suite; runtime/header/fixture code
was unchanged. Extended public workflow and busy harnesses also passed strict
-Wall -Wextra -Werror -fno-exceptions -fno-rtti build/execution. Real native
screenshot inspected with real-font assertion, six docs/99 relative links,
balanced fences and git diff --check passed. Incoming six-job CI was independently
reverified for 3536ec4. Final normal push/exact new-SHA CI must follow; no hardware
acceptance is implied. Next: F5 real guest file-save flush/access/error behavior.


## Whole-F closure task: first package, real guest file saving

User explicitly requests closing all F, not ending after individual small
features. Clean b0858f0/FETCH_HEAD, no AGENTS/newer work; exact incoming CI
40136047193 was verified six jobs. F_ACCEPTANCE.md now lists every baseline
programmatic gate and keeps separate deferred physical and installer/archive
extension work. Continue the whole task after this package, do not declare F
complete from corrected file APIs alone. Next: tree/shortcuts, retained planes
and integrated F acceptance.

KERNEL32 synchronous read/write uses 8 KiB buffers, checked guest ranges/counts,
explicit unsupported OVERLAPPED, short/failed native I/O and append seek errors.
FlushFileBuffers now checks actual HANDLE/type/write access, fflush and fsync,
instead of unconditional success. Size and truncate propagate actual flush/stat/
position/truncate failure. NUL/EOF and console line translation remain explicit.
Duplicate HANDLE/file-object lifetime is unchanged; CloseHandle is handle closure,
not a durable-save promise. F5_FILE_SAVE.md defines supported subset/limits.

New dispatcher regression with real Process/TEB checks actual >8 KiB bytes,
invalid/zero/massive buffers, counts/errors, readonly/invalid/stale/alias HANDLE,
size/truncate and genuine /dev/full delayed write failures. Public x86 BMP writer
now calls corrected FlushFileBuffers; actual native/fresh-x86 pixel reopening
still passes. Complete clean rootless MinGW/Mesa suite passed final runtime and
fixture tree, all previous F/C/six E9 and both real EGL oracles included. Strict
API compilation/native harness passed; exact new-SHA six-job CI follows normal
publication. Local SDK absent, physical gates deferred, SD closed.


## Whole-F closure: retained workspace layer integration

Incoming 1f0572a exact CI 40136895969 passed all six jobs. Whole-F task continues:
no final F-complete report yet. ShellSession now builds an optional bounded
ownership map and scene with real guest window surfaces plus independent native
plane/token/session generation. Switch keeps window raster retention enabled and
submits this scene to existing Presentation/GPU; canonical CPU composition remains
software/allocation/driver fallback and guest readback remains untouched. Scene
48-layer/16 MiB budget includes native, excess guest ownership reconciles to CPU.
Optional buffer failure gives a valid flat scene. See F4C_WORKSPACE_LAYERS.md.

Extended real-x86 workspace normal/abort/restart checks compare retained software
and actual EGL/Mesa output pixel-for-pixel, Start/Explorer/minimize/restore,
poisoned output padding and owned replay after whole guest destruction. Optional
scene allocation failure and all old required allocation/teardown cases pass.
Scene integration intentionally uses conservative copy/compare, not native E7
write-through epochs or a speedup claim. Complete clean rootless MinGW/Mesa suite
passed final runtime/header/fixture tree, including workspace GL and both existing
28-scene GL oracles, all file-save/native/guest/Stage C/E lifetime regressions.
Targeted strict check passed before final full suite. Normal publication and exact
new-SHA CI follow; no local SDK or physical acceptance is implied. Next within
the same whole-F task: folder tree and persisted real Desktop/Start shortcuts.


## Whole-F closure: tree/real links and integrated software acceptance

Incoming 4db5236 exact CI 40137277767 passed six jobs (previous 1f0572a exact
CI 40136895969 also six). The user requested finishing every baseline F gate.
All programmatic ledger gates are now implemented; this does not close deferred
physical acceptance or the explicitly planned post-baseline installer/archive
extension. Status is PASS_SOFTWARE_PENDING_HARDWARE, not full Windows/NT.

Added actual F1 folder tree (128 nodes/depth32/4096 scans), native controller/
pointer expansion/collapse/navigation, real persisted Desktop/Start Unicode
MS-SHLLINK subset, checked no-overwrite staging and shared validated launch with
arguments/CWD. New profile Start Menu/Programs, shell folder APIs and ProgramFiles
environment agree. Production applies requested CWD after LoadMainExe, preserving
EXE/DLL module directory. Link/header/string/path limits, unsupported ID/network/
extra blocks, generic native icons, clipboard/nonrecursive filesystem and text/
image limits remain explicit contracts, not universal shell behavior. No COM or
fake child processes. See F3C_SHORTCUTS_TREE.md.

Native tree/link tests cover actual storage, malformed/truncated Unicode records,
short/full/flush/close/publish failures, stale/missing targets/CWD, collisions,
Desktop/Start reconstruction and controller/fan-out/link checks. Independent
pylnk3 0.4.3 outside the repository parsed written metadata; the product has no
new dependency. Public x86 bitmap flow now reopens through fresh Desktop and
Start links with args/different CWD and checks actual folder/environment APIs.
Native screenshots inspected with real font.

Full clean rootless MinGW/Mesa suite passed final runtime/header/public sources,
all C/six E9 and both full EGL oracles plus native/guest workspace GL. The new
reporter emits 22 evidence groups, public fixture hashes, head SHA/dirty state
and explicit pending hardware/outside-baseline fields only after a successful
full runner. Missing evidence fails closed; stale reports are removed. CI now
uploads stage-f-software-acceptance. Local report is explicitly dirty/unpublished;
only the exact new clean CI report certifies publication.

Initial intermediate runs exposed test cleanup omitting the newly real Start
Menu directories and a missing shlobj.h include in the public fixture. They were
corrected directly; environment's ProgramFiles key was normalized for real getter
lookup. The final clean suite is the release result. Strict link/tree harness and
ASan/UBSan/leak checks passed, source compiled without exceptions/RTTI. Only
additional native screenshot assertions/report cleanup/docs followed that suite;
remaining strict/doc/diff and exact published-SHA six-job/artifact checks follow.

Next after software F: D compatibility fixtures and representative G timing/
overhead before optimization. Preserve the exact-SHA public F/NRO evidence and
consolidated deferred Paint/GPU/fallback/Lite/input/relaunch checklist. Local
devkitA64/libnx remains absent, SD closed. Do not claim physical acceptance or
universal installer/ZIP/RAR support from the programmatic baseline.


## 2026-10-25 G1: measured public PE phases after software F

Fetched clean 50ec505/FETCH_HEAD on the requested branch; incoming exact CI
40139298651 passed all six jobs and its clean F report/NRO were verified. User
reports slowdown in Paint and high-complexity Minesweeper, requests direct GPU
drawing and the next stage. Existing OpenGL already composites/presents retained
planes; GDI raster and x86 remain CPU. Do not promise GPU GDI from output alone.

New CRT-free graphics_workload.c runs CPU recurrence, 3,840 tile blits, or 2,048
strokes through real PE/interpreter/HLE. benchmark_guest checks exit/completion,
independent checksum, every destination pixel and full-screen RGB hash before
reporting load/prepare/run/teardown phases and G0/raster counters. Python runner
checks warm-ups plus five rotating-order repetitions in four instrumentation
modes, matching histograms/quanta/pixels, exact raster counts and bounded runs;
failed evidence removes/does not publish a success report. Complete samples,
source dirty state, fixture/runner hashes and host identity are in the report.
No clocks were added per instruction or production behavior changed. G1 report
is a host experiment without presentation/GPU/swap/FPS, not Switch acceptance.
See G1_MEASUREMENTS.md for nesting, guards and rounded initial observations.

A development tile variant initially exhausted G0's one-million-instruction cap
because it filled its source on every cell. It now initializes two genuine source
tiles once and reuses them, so the fixture exercises blits rather than repeated
guest source generation. Default production census limits are unchanged. Initial
targeted strict checks and repeated experiment pass; final full suite and exact
published-SHA CI are required before completion. Local Switch SDK remains absent;
CI builds NRO. No physical acceptance is inferred and SD diagnosis stays closed.

Next: opt-in console attribution of representative workloads, measured GDI or
interpreter candidate, coherent GPU GDI readback/ROP/batching contract and EGL
oracle before activation. Luna visual fidelity stays open. Pinball x86 user
archive was checked outside git: host reproducibly stops before window creation
at missing ADVAPI32!RegCreateKeyExA; other static API imports are not proof of
reachability. That is separate D compatibility work, not GPU or SD diagnosis.


G1 integration correction: the first full suite passed the dedicated G1 experiment
and all previous tests, but its generic no-argument EXE sweep also launched the
parameterized graphics_workload fixture, which correctly rejected missing mode.
It is now excluded from that sweep like other dedicated-harness fixtures; the
three explicit modes still run 72 checked invocations in the G1 experiment. A
complete fresh suite was restarted after this runner-only correction. Strict
no-exceptions/no-RTTI host harness and strict MinGW fixture compiled; a negative
PE32+ input failed and removed an old report as intended.

Final G1 local validation: the complete fresh rootless MinGW/Mesa suite passed
on the final harness/fixture/runner tree, including all 72 G1 checked invocations,
22 F evidence groups, Stage C, six E9 groups, actual workspace EGL and both
28-scene CPU/actual-GL oracles. No runtime source changed in this package. Strict
host/MinGW checks, Python syntax, negative wrong-architecture/stale-report test,
shell syntax, six-job workflow/artifact validation, Markdown links/fences and
git diff --check passed. Only documentation followed the final full suite.
Normal branch publication and exact new-SHA six-job CI/artifact verification
follow; local precommit report is explicitly dirty, not published evidence.


## 2026-10-25 G2: first measured GDI copy optimization and console phase support

Fetched clean 9d4f876/FETCH_HEAD, no newer work/AGENTS. Incoming exact CI
40141901580 passed six jobs; its clean G1 report, fixture length/SHA and all 60
measured samples were independently verified. User requests autonomous work
without new physical testing. Hardware gates remain deferred; do not ask for
console logs or flag setup as a prerequisite to independent implementation.

Gdi::BlitCore now defaults to a bounded row-based 32-bit SRCCOPY path after
normal target/clip/overlap preparation: no scaling, mirror, key or out-of-range
source. Masks unused high byte, honors negative pitch and uses original Set
for captured destinations, preserving ownership/layer/tile revisions. Other
cases retain the general loop. Session-owned native diagnostic control can
select that exact reference loop in the same executable. No EXE-name exception,
new API stub, shader or GPU-GDI claim. See G2_BLIT_AND_PHASES.md.

New native differential regression checks 1,600 reference/fast cases including
full storage/guards/unused bytes, formats/pitches, clips/bounds/translated/
scaled/mirrored/key/ROP/overlap and repeated captured writes. Strict standalone
compilation/execution and ASan/UBSan/leak checks passed. A test initially used
nonexistent Region::Add; corrected to existing UnionRect, no runtime workaround.
The public PE benchmark now supports reference/fast and phase modes, while G1
still runs unchanged four-mode controls. G2 compares six conditions per workload
with seven repeats, 126 measured samples +18 checked warm-ups. Timing is not a
pass threshold; host-only observations never certify Switch FPS.

Presentation report v8 adds optional fixed-storage quantum/native-service/idle
wall counters. First byte1 of profile-execution.flag enables them once per
new-UI session; missing/unreadable defaults off. Scopes wrap outer RunOne, not
HLE continuations across suspension; no per-opcode clock or per-frame SD log.
RunOne includes HLE/interpreter/scheduler and synchronous waits/presentation,
not exclusive CPU time. Raster/GL may nest; don't sum counters as disjoint.
Deterministic-clock tests verify disabled/reset/accounting; public actual-PE
phase mode requires one quantum sample per outer call and records overhead.
Legacy UI and existing raster instrumentation policy are unchanged.

Final complete rootless MinGW/Mesa suite and exact new-SHA six-job CI/artifact
checks are required before completion. Switch main is CI-compiled; no local
devkitA64/libnx SDK. Next: GPU-GDI coherence/target/command/readback contract
and bounded actual-EGL prototype, preserving CPU oracle and fallback before
activation. Pinball registry blocker, installer/archives and Luna remain open.


Final G2 local validation: complete fresh rootless MinGW/Mesa suite passed on
the final runtime/header/harness tree, including G1's 72 checked runs, G2's 144,
new 1,600-case differential oracle, phase counters, 22 F groups, Stage C/six E9,
workspace EGL and both 28-scene CPU/actual-GL comparisons. Initial local tile
run medians were 19.79/15.69 ms reference/fast, nested blits 4.70/0.63 ms; CPU control
5.00/4.98 ms. These are host observations, no Switch speedup claim. Strict native
blit/harness/metrics checks, ASan/UBSan/leak, negative wrong-architecture/stale
report, Python/shell syntax, Markdown/diff and six-job artifact checks passed.
A rootless AArch64 assembler initially lacked its relocated shared-library path;
setting that path restored the toolchain. The same optimized native blit oracle
passed under QEMU ARM64 and is added to the existing ARM64 CI job. This is not
a local Switch SDK/build. No runtime code followed the full suite; only docs and
the additional ARM64 CI gate. Publish normally and verify exact new-SHA six jobs,
clean G2 report and fixture hash before completion. User hardware tests deferred.


## 2026-10-25 Claude Luna updates: preserve, audit and join active workspace

User requested reviewing Claude's latest UI work and resuming our graphics plan.
Explicit branch fetch found five descendants of G2:9ca8ff7/5dba613/6034b12/
c881653/466bcfd. Fast-forwarded only; no reset or discarded change. All have
successful CI, latest466bcfd run40148776172 verified all six jobs. G2 run
40143458222 also completed six success, including the new ARM64 GDI step; its
clean126-sample report and unchanged public fixture SHA verified. CI tile ratio
fast/reference about0.812, a host observation only. See LUNA_INTEGRATION_REVIEW.md.

Reviewed procedural cached wallpaper, vector icon cache/desktop labels, larger
Explorer geometry/toolbar/task pane/status and two-column Start menu. Native
minimize/maximize caption glyphs are currently decorative; do not claim those
window operations, full XP styling or GPU GDI from theme changes. Preserved all
Claude artwork and actions. Found a real integration defect: ShellSession still
used the old218/100/1166/630 rectangle for composition, retained ownership and
input after the panel moved to156/8/1258/676. Idle UI and GPU-vs-the-same-broken-
canonical comparisons did not catch it. Added shared DesktopShell::PanelArea
and used it in drawing and all three active-guest paths.

Extended actual-x86 workspace test independently compares every full-panel
pixel with native rendering, expanded-corner native owners and right-edge input
isolation over a maximized real guest. Complete fresh rootless MinGW/Mesa suite
passed final code/test tree, including both actual-EGL workspace/composition
oracles, G1/G2, icons,22 F groups and prior lifecycle/file regressions. Actual
Start/Programs snapshots inspected. Source/doc/diff checks and exact published-
SHA six-job CI remain publication gates; local SDK absent, physical tests deferred.
Continue where G2 stopped: bounded real-EGL GPU-GDI/coherence/readback/recovery
prototype before production activation. Do not require hardware logs/flag setup
from the user. D Pinball/installer/archives and visual refinements stay open.


## G3: isolated actual-GL GDI target/readback/recovery prototype

Preserved all five Claude Luna commits and published shared panel geometry fix
b7cb82d; exact CI40188452904 verified six success. Continued independently with
GPU-GDI semantics, no request for physical tests. GpuGdiPrototype is compiled but
not referenced/activated by production Win32 GDI, frontend or presentation.
See G3_GPU_GDI_PROTOTYPE.md for the exact supported subset and caller contract.

Real GLSL/FBO solid fills and snapshot-backed1:1 RGB copies operate on one target
up to512x512. Inputs owned before queue/byte capacity checkpoints. Journal64
commands/4MiB snapshots retains flushed and pending work until successful readback
into separate scratch commits a CPU checkpoint. Draw/readback failure replays
the journal and reports CPU fallback; FlushGpu returns false. Actual context-loss
notification replays without driver calls after context destruction. Ordinary
non-GL host Initialize fails explicitly. Fixed budgets/injected allocator cover
init/command failure and cleanup. Dedicated current context only; not a shared-
context renderer or arbitrary ROP/palette/transform/multi-target implementation.

Actual EGL tests compare every pixel at1x1/31x23/512x512 with CPU GDI, source
mutation after enqueue, both caps, malformed copies, init/command allocation
failure, GL draw/readback errors, CPU continuation and real EGL context destruction
with flushed+pending commands. GPU mode/counters required before failure. Added
independent O2 strict test to existing gpu-reference job and host artifact/log;
no seventh job. No user application files or hardware timing is part of evidence.
Initial strict compile corrected misleading indentation. The upload texture is
kept distinct from the FBO target even for fills, avoiding an undefined feedback
loop. Full clean suite on final runtime/test tree is the completion gate.

Next G4: bounded multiple target/window IDs, batching, read/raw-DIB/ROP barriers,
texture-aware presentation and measured transfer/checkpoint cost before any
production activation. Current ShellSession memcpy of canonical CPU pixels
would otherwise force readback/duplicate raster. No Paint-on-GPU/FPS claim.
Luna updates remain; native min/max glyph actions, D Pinball and installer/
archive work remain explicit. Physical acceptance deferred; local SDK absent.


Final G3 validation: complete fresh rootless MinGW/Mesa suite passed on the
final runtime/header implementation, including unavailable-host behavior, real
EGL prototype/failure/context-loss oracle, G1/G2,22 F evidence groups, Stage C/
six E9 and both workspace/composition EGL paths. Additional final borrowed-input
capacity-checkpoint assertion passed strict O2/no-exceptions/no-RTTI actual-GL
execution. ASan/UBSan passed the full final prototype test; driver leak scanning
was disabled, while the injected allocator independently requires all module
allocations freed. No runtime change followed the suite, only additional test
assertion/docs/artifact cleanup. Markdown links/fences, shell/YAML/six-job and
diff checks passed. No newer remote branch changes when fetched. Normal push
and exact new-SHA six-job/artifact proof remain the final publication gates.


## G4a: multiple owned GPU targets and direct texture composition

Clean a98e87e/FETCH_HEAD, no AGENTS or newer remote work; exact incoming
CI40189429070 independently reverified six success. User requests continuation
without physical testing. Added GpuGdiTargets: four native generation-checked
handles, aggregate512x512 target pixels, create/destroy/budget/allocator cleanup,
fill/copy/read and explicit CopyTargetViaCpu barrier. Process-global monotonic
generations reject reused/foreign/reinitialized handles and never wrap. GPU
texture names remain private. See G4A_GPU_TARGETS.md for scope and budgets.

Actual-GL composition flushes targets and directly samples resident textures into
default framebuffer, opaque nearest-scaled back-to-front up to1280x720. The
composition path has no CPU snapshot/readback or texture reupload. Inherited
command capacity checkpoints and explicit read APIs still read back by design.
CPU fallback compositor reads real target images and owns its output. Readback
failure, target draw error or presenter failure disables the manager's GPU path
and recovers all images. Actual context destruction notification requires no
driver calls; native CPU operations continue. No live HWND/GDI/presenter hookup,
shared-context state restoration, arbitrary ROP/palette/alpha or batching yet.

Actual EGL oracle compares every output pixel with independent64-bit CPU mapping,
four patterned/overlapping/clipped/scaled targets,12 geometry-only frames with
zero internal readbacks/no reuploads, native1280x720, explicit CPU read barriers,
invalid/stale frames before output writes, reused/foreign/reset IDs, both target
allocation failures, input/output failure, slot/pixel caps and cross-target source
mutation. Covers target draw, presenter-only and readback errors, global fallback
and actual context loss with flushed+pending work, then continued CPU writes.
Ordinary host test explicitly rejects unavailable GPU initialization. Independent
strict O2 gate added to existing gpu-reference job and artifact/log to host job;
still six jobs. Full fresh suite and exact-SHA publication proof remain gates.

Next G4b: batching plus live-window/raw-DIB/ROP coherence and texture-aware
production integration instead of unconditional canonical CPU compose. Do not
claim Paint GPU acceleration or console FPS from this isolated semantic bridge.
Claude's Luna and G2 CPU fast path preserved. Hardware tests deferred, local
Switch SDK absent; CI compiles Switch source. No SD re-diagnosis.


Final G4a local validation: the complete fresh rootless MinGW/Mesa suite passed
on the final runtime/header tree, including G3/G4a actual EGL, native unavailable
behavior, G1/G2 measurements,22 F groups, Stage C/six E9 and both native/guest
workspace/composition GL paths. Extra final tests cover overlapping self-copy
through the explicit barrier, empty output, readback-induced global fallback,
presenter-only failure without pending target commands and destruction right
after frame submission without a completion wait. Strict O2/no-exceptions/
no-RTTI and ASan/UBSan target checks passed; driver leak scanning disabled, owned
allocator cleanup checked independently. No runtime changed after the full suite.
Final doc/shell/YAML/diff checks and exact published-SHA six-job/artifact proof
remain publication gates. User hardware checks stay deferred.


## G4b.1: bounded ordered instanced fill batches

Clean847ba20/FETCH_HEAD, no newer remote work/AGENTS; exact incoming
CI40193307039 independently reverified six success. User requests continued
autonomous graphics work, hardware gates still deferred. Added contiguous
pairwise-disjoint fill grouping in the unflushed G3 journal suffix. Overlap and
copies split groups; no sorting or recovery-record merging. At most64 entries
use fixed uniform arrays and one instanced draw, singleton/copy original path.
Locations cached at init; native same-build EnableBatching(false) control.
Extra shader IDs cleared on context loss; journal replay remains per-command.
See G4B_GPU_BATCHES.md; this is batching only, not all G4b/live GDI activation.

New native EGL benchmark checks four warm-up plus32 measured128x128 cycles per
process,64 ops/cycle, full independent pixel checks. Seven paired alternating
repetitions of tiles/mixed/overlap produce42 raw samples, source dirty/SHA and
runner hash. Deterministic draw counts2048->32 tiles,2048->96 mixed with copy
barrier,2048->2048 overlap.32 readbacks and2048 attempted commands preserved.
Wall phases separate enqueue/submit/readback; never GPU duration/Switch FPS.
Local initial comparison cycles16.55/3.98ms tiles and15.83/6.46ms mixed, host
software GL only; timing distributions are recorded with no pass threshold.

Extended CPU GDI/EGL oracle checks clipped/adjacent/disjoint/overlapping fills,
copy boundaries, exact attempt/group/command counts, repeated flush, recovery
after already submitted groups and actual context destruction after instanced
drawing with pending copy. Full fresh rootless MinGW/Mesa suite passed runtime/
header/test tree, including G3/G4a, G1/G2,22 F groups, Stage C/six E9 and prior
workspace/composition GL/lifetime gates. Strict targeted O2 and ASan/UBSan passed;
driver leak scan disabled, injected owned allocator cleanup still required.
Only docs/report validation follow. Final source/link/shell/YAML checks and exact
new-SHA six-job/clean artifact verification remain publication gates. SDK absent.

Next G4b.2: HWND/DC identity/visible ownership, raw-DIB and CPU ROP/read barriers,
shared-context state contract and texture-aware production presenter. Current
ShellSession canonical CPU memcpy cannot just remain plus duplicate GPU work.
Actual-x86 public lifecycle/pixel fixtures before activation, no claim Paint is
GPU drawn. Preserve Luna, G2, physical deferred list and separate D/installer work.


Final G4b.1 checks: full clean rootless MinGW/Mesa suite completed successfully
on final runtime/header/harness tree;42 benchmark samples and exact draw/command/
copy/readback/pixel gates passed. Final full-suite local phase sums were14.25/
4.20ms tiles and11.71/6.75ms mixed (32 cycles, host-only). Negative wrong-runner
test rejected and removed stale evidence; Python syntax, shell syntax, Markdown
relative links/fences, six-job workflow/new independent GPU gate and git diff
--check passed. Strict O2 and ASan/UBSan actual-G3 pixel/order/error/context-loss
test passed on the same runtime. No code followed the full suite, only docs.
Fetched unchanged847ba20 before normal publication. Verify exact new-SHA all six
CI jobs and clean42-sample artifact before completion. Hardware remains deferred.


## 2026-10-25 consolidated global map update (documentation only)

User asks to reconcile overall project position. Clean2c4f849/FETCH_HEAD, exact
CI40196352908 reverified six success and prior clean42-sample batching artifact
verified. Replaced accumulated contradictory next-BMP/F5/G1 snippets in user
status and active map with one current scope: F software done/pending hardware;
G2 active CPU optimization; G3/G4a/G4b.1 isolated GPU prototypes; nextG4b.2.
Reconciled global A–I plan and retained C/JIT historical acceptance through
subsystem evidence rather than presenting old queues as current. No global
percentage, FPS, full XP, installer, dynarec or DirectX completion is inferred.
Explicitly retain decorative native min/max, free drag/resize/multi-EXE and
broader-image limitations; real paths/links do not certify installers. Hardware
checks remain deferred, SD closed, local SDK absent and CI NRO build distinct.
Only documentation changed; link/fence/source/diff checks and exact published-
SHA CI are the gates. No runtime test rerun or code/fixture behavior claimed.

## Visual roadmap — 2026-10-25

Added a self-contained [interactive offline HTML roadmap](roadmap/index.html): 23 Russian cards, linked
A–I / GPU / next-work views, global search/status filters, keyboard navigation,
mobile layout and shareable card hashes. The data snapshot identifies runtime
`2c4f849` and its six-job CI separately from site revisions. G4b.2 remains next;
no runtime behavior or hardware acceptance changed. Update the embedded JSON
and hero alongside this handoff/map; see [authoring notes](roadmap/README.md).

`tools/check_roadmap_site.py` checks IDs, states, view memberships/edges and
existing subsystem documents in the host CI job. Separate Roadmap Pages workflow
publishes this branch's static folder; core CI remains six jobs. Browser checks
cover desktop/mobile rendering, tabs, search, filters, deep links and drawer
controls; no external front-end dependencies are required.

Initial API activation returned HTTP 403; the owner then enabled Pages with
GitHub Actions. API now confirms `build_type: workflow` and
https://omeruno.github.io/switch-recompiler/. The workflow validates/uploads
the site and deploys when Pages is accessible. Check the actual deployment
and core CI for the published SHA before reporting the site live.

## G4b.2a: shared-context state handoff

Clean f4a5ecf/FETCH_HEAD; no newer remote commits/AGENTS. Incoming core CI
40200041032 verified six success; Pages deployment 40200041025 succeeded after
the owner allowed this branch in github-pages. The public roadmap is live.

Added fixed-storage nested GdiGlScope around actual G3/G4 GPU operations. It
normalizes required raster/sampler/PBO/pixel-transfer state and restores caller
program/VAO/FBO/viewport/scissor/clear/mask/texture0/sampler/active-unit/store
state on success/error/cleanup. Queued operations have no scope unless they
reach a checkpoint; nested flush/draw/read use one outer snapshot. CPU fallback
and notified context loss skip GL as before. See G4B2_SHARED_CONTEXT.md for the
bounded GL3.3 caller contract and unsupported active query/TF/conditional scopes.

New independent actual-EGL oracle checks hostile state, every target/output
pixel, capacity/read/copy barriers, invalid frames and injected-error fallback.
Twelve GDI/unchanged production-renderer interleavings require correct images
and zero additional target readbacks merely for presentation. The same test
fails against incoming unguarded sources at initialization with a caller PBO.
Strict O2 and sanitizer checks plus full fresh host/MinGW/Mesa suite are gates;
no live HWND/DC fixture or Paint acceleration is inferred. Existing G3/G4a
context-loss/allocator oracles and G4b.1 count/measurement gates remain mandatory.

Added strict independent check to gpu-reference and host log artifact; core CI
still six jobs. Update the visual map alongside docs. Next: actual HWND/DC
identity/lifetime, then raw-DIB/CPU ROP coherence and production texture bridge.
No runtime activation, forced user hardware testing or new SD investigation.
Local SDK unavailable; NRO compilation is CI-only. Verify exact pushed SHA
and Pages publication before reporting completion.

Final local G4b.2a validation: the complete fresh rootless MinGW/Mesa suite
passed the final runtime implementation: new shared-context oracle, G3/G4a
context loss and allocator checks, G4b.1's42 measured samples/exact work gates,
G1/G2, actual-x86 workspace/composition/input, Stage C/six E9 groups,22 F
groups and session/dialog/file regressions. Strict O2 and ASan/UBSan shared-GL
checks passed; driver leak scanning disabled. The negative old-source control
failed at the expected initialization assertion. No implementation changed
after the full suite; subsequent changes are comments/docs/map. Published-SHA
CI and Pages deployment remain final gates, separate from physical acceptance.

## PC-game direction recorded — 2026-10-25 (documentation only)

Clean7ffbf95/FETCH_HEAD; exact core CI40202733317 verified six success. User
clarified the goal as PC games including Spore/Mirror’s Edge/The Sims3, using
Switch CPU/GPU through proper translation/native host backends. Added explicit
planned/unsupported target matrix and functional/performance gates, broad G CPU,
H graphics/shaders/audio and D dependencies. D3D9 audit follows chosen versions;
no blind mandatory large DirectDraw prerequisite. Current HWND/DC queue retained.
Updated global/active/user/visual maps without claiming a game was inspected or
launched, x64 support, hardware results, FPS or full-resource access. No runtime
changes; map/link/browser/diff checks plus exact-SHA core CI/Pages are the gates.

## G4b.2b: real window/DC identity and explicit native GPU bindings

Incoming1d3f155/FETCH_HEAD unchanged; exact core CI40204271361 six success and
updated public game-goal Pages verified. User continues autonomous development;
no hardware request. Added monotonic native window/DC identities, captured DC
window lifetime and identity-only provider query. Real provider rejects stale
identity before allocating/capturing a replacement window's raster plane. CPU
Target also rejects mismatch, fixing stale DC → reused HWND aliasing.

Gdi::DescribeWindowTarget owns frame/origin/effective visible/paint/selected clip
and lifetime/layout metadata. WindowGpuBindings manages four bounded real targets
with per-command revalidated leases, shared per-window image across DCs, checked
MM_TEXT fill translation/clipping, explicit read, resize/drop/prune/session
retirement and notified context-loss CPU continuation. No borrowed USER/GDI
pointers, asynchronous wrong-thread GL deletion or ordinary GDI activation.
See G4B2B_WINDOW_BINDINGS.md: target initialization/resize is black, not a CPU
checkpoint; canonical/production frame authority remains a gate.

New redistributable PE32 fixture and actual-EGL inspector cover20 lifecycle
stages across two fresh sessions reusing numeric HWND/HDC, independent clipping
pixels, CPU canonical unchanged and a third actual context-loss/host-abort case.
A negative control removing the CPU guard fails at stale Target. An initial
harness forgot EnableUi and selected legacy imports; fixed only the test setup,
no API stub. A prior local suite was stopped/rebuilt when lifetime prevalidation
was tightened to reject before write-capture allocation. Final full suite and
focused strict/sanitizer checks follow; only successful final-tree results count.

Next CPU/GPU/DIB/ROP/read/save/resize/fallback coherence, then owning texture
frames/production activation. Preserve Luna/G2, CPU fallback, native1280x720,
input, session cleanup and PC-game direction. SDK absent, hardware deferred.
Publish normally, verify exact new-SHA six-job CI, native-binding artifact and
Pages site before completion; never infer full G4b.2/game/H/dynarec completion.

Final local G4b.2b validation: complete fresh rootless COMPOSITION_GL_TEST=1
MinGW/Mesa suite passed final implementation/test sources, including20 x86
binding lifecycle stages plus actual context loss/host abort, G3/G4a/G4b.1/2a,
G1/G2, both actual-x86 composition/workspace/input paths, Stage C/six E9,22 F
groups and prior sessions/files/dialogs. Strict O2 host bridge/harness and
CRT-free MinGW fixture passed. Focused ASan/UBSan bridge/GDI/USER-window/GPU
modules passed (remaining runtime objects uninstrumented, driver leak scanning
disabled; sanitizer reports its known makecontext/swapcontext limitation).
The negative CPU-guard control failed at the intended reused-HWND stale Target
assertion. No implementation changes followed the successful final suite; only
docs/map/checks remain before normal publication and exact-SHA CI/Pages proof.

## G4b.2c — CPU/GPU authority and real Win32 barriers (2026-10-26)

Clean e780649/FETCH_HEAD, no newer remote changes/AGENTS. Incoming exact core
CI40221359571 verified six success; prior native-binding artifact and Pages live.
No local SDK or physical tests requested. User wants autonomous real system work
for future PC-game targets, not a Paint-specific accelerator.

Added ImportCpu full known-CPU checkpoint with empty-journal requirement and no
readback for import. WindowGpuBindings.EnableCoherence is opt-in before acquire;
bounded seeds/dirty regions track authority, import after CPU access and commit
GPU pixels via actual write-capture/ownership/tile revisions before CPU access.
Gdi::Target/DeleteDc and covered USER layout/repaint/output boundaries acquire CPU
authority. Geometry-only ResolveTarget keeps lease validation from forcing sync.
Weak gates move with Gdi/session swaps, expire safely and reject a dead owner;
namespace/native thread/context/lifetime contracts remain explicit. Default GDI
and native diagnostic bridge behavior preserved. See G4B2C_CPU_COHERENCE.md for
remaining direct-pointer/error/frame/budget gates before product activation.

Public CRT-free PE32 fixture runs20 real x86 stages across GL-error/actual-context-
loss modes, two dirty windows, GetPixel, ROP, raw CPU DIB source, window-to-DIB
copy/GetDIBits, move/resize/release/destroy and CPU continuation. Initial first-
window packet requires64 GPU fills, one CPU import, one fill batch and one CPU
readback; a second dirty window drains at the same boundary. Actual production
EGL output matches all1280x720 expected pixels after capture; this is not FPS.
Allocator failures, complete buffer release, nested/expired gates, owned import
and pending-journal rejection checked. Explicit diagnostic/FBO reads are counted
separately from the initial work-control phase.

The integrated test exposed bound-program deletion in OpenGlPresentation::Shutdown:
pending-deletion current program became invalid on scope restoration and forced
CPU fallback. Unbind the backend's own current program before deleting; preserve
another current program. Regression requires a clean program0 handoff and GPU
continuation; negative old-shutdown control fails there. A separate negative CPU-
Target-gate control fails the guest's first GetPixel assertion. No EXE/API stub.

Initial full suite failed the new count/GPU-active oracle on that shader-lifetime
bug. After the fix, complete fresh rootless MinGW/Mesa suite passed final runtime/
fixture/inspector tree: new coherence, previous G3/G4a/G4b.1/2a/2b, G1/G2, x86
workspace/composition/input, Stage C/six E9/22 F and prior lifecycle/file/dialog
regressions. Strict and focused sanitizer checks complete before publication;
only final successful sources count. Host log/public fixture/contract artifact
added to existing host job, still six core CI jobs. Map/handoff updated. Next
owning texture frames/access audit/ordinary dispatch, not full G4b.2/H/dynarec.
Exact published-SHA CI and live Pages/artifact proof remain completion gates.

Final local G4b.2c checks: fresh COMPOSITION_GL_TEST=1 host/MinGW/Mesa suite
passed on the final implementation/fixture/inspector tree. Strict O2 native
coherence harness and strict CRT-free MinGW fixture passed. Focused ASan/UBSan
instrumented changed GPU/GDI/bridge/USER/layout/output modules and passed both
modes (20 stages total); remaining runtime objects were ordinary builds, driver leak
scanning disabled and the known makecontext/swapcontext warning retained. The
first sanitizer build stopped at the preexisting unused ScrollCommon CPU
parameter in user32_api.cpp; only unused-parameter warnings were allowed for
that focused build, with no runtime warning-suppression patch. Both negative
controls failed at the intended guest exit / deleted-program assertions.

No implementation changed after the successful final suite. Map schema/docs,
local links/fences, shell syntax, six-job workflow, desktop/mobile search/filter/
deep-link/drawer browser tests and diff checks passed. Source baseline remains
explicitly incoming e780649 until the new published SHA's CI is verified.

G4b.2c CI infrastructure follow-up: f390996 core CI40331312251 passed host,
actual GL, CPU oracle, Switch and JIT builds, while ARM dependency downloads
remained around40KiB/s on azure.archive.ubuntu.com HTTP (observed10.8MB cpp
download took260s; tests had not started). Retrying only that job did not
promptly resolve setup. Changed only ARM dependency setup to the official Ubuntu
HTTPS archive, preserving suites/signature keyrings, bounded network retries/
timeouts and ten-minute step cap. Explicit qemu-aarch64 tests remain unchanged;
no successful gate is skipped. --no-install-recommends omits unused binfmt setup.

The final C++ source also passed1600 reference/fast ARM64 GDI cases locally via
rootless AArch64/QEMU. The relocated compiler needed its staged native library
path and --sysroot; this is not a local Switch SDK or substitute for exact-SHA
CI. New CI/config publication must verify all six jobs and public artifacts.


## G4b.2d.1 — resident-resource frame lifetime (2026-10-26)

Clean7edadd4/FETCH_HEAD, no newer work. Added bounded copyable native frames
with owned geometry and resource pins, no GL/pixel allocation at preparation.
Destroy retires a target immediately for ordinary handle APIs but retains it for
prepared frames, charging the existing slot/pixel budget. Last-pin collection is
on manager owner thread; frame release makes no GL calls. Shutdown/reset revoke
old frames and release all resources even if frame metadata survives. Frames
reference live contents, not immutable pixel versions. See G4B2D_RESIDENT_FRAMES.md.

Existing EGL target oracle extended without removing live-target context-loss
continuation. New tests compare twelve resident frames, retirement/copies/budget,
invalid/foreign/reset frames, rejected preparation preserving output, GL-error
fallback and separately actual context destruction with a retired image holding
submitted and pending commands. Negative immediate-deletion control fails at the
retained-budget assertion. Strict O2 and focused ASan/UBSan pass; driver leak
scan disabled, owned allocations checked independently. Full suite/publication
results must be verified before completion. No automatic GDI dispatch, production
ShellSession integration, physical FPS or game compatibility is claimed. Next
texture-aware production ownership/overlays/fallback and raw-host/fault audit.

Final local G4b.2d.1 validation: complete fresh COMPOSITION_GL_TEST=1
host/MinGW/Mesa suite passed final runtime/test sources, including the extended
resident-frame oracle, coherence x86 modes, prior G3/G4a/G4b, G1/G2, workspace/
input/session/file/dialog tests, Stage C/six E9/22 F groups. Strict O2 and full
ASan/UBSan target-module/harness checks passed final source; driver leak scan
disabled. Negative immediate-retirement control rejected at expected budget
assertion. Map schema and desktop/mobile browser checks passed. No runtime/test
changes followed this full suite. Exact new-SHA six-job CI and Pages/artifact
verification remain final publication checks, not physical Switch acceptance.

G4b.2d.1 publication follow-up:4465087 core CI40341841730 passed actual
EGL, CPU oracle, ARM64/QEMU, Switch NRO and isolated JIT NRO. Host-job dependency
setup alone remained running for over seven minutes, before any tests. Applied
the already verified official HTTPS Ubuntu source/retry/timeout policy to that
MinGW/Mesa installation too, with a ten-minute step limit. Preserve suites/keys
and all runtime test commands/six jobs. No runtime/test change or local suite
rerun is needed for this configuration-only follow-up; verify exact final SHA
core CI and the resident-frame log artifact before claiming completion.


## G4b.2d.2 — production renderer resident-layer entry (2026-10-26)

Clean72545db/FETCH_HEAD; no newer remote changes/AGENTS. Added synchronous
ConsumeFrame internal native views from owning frames, actual GPU flush without
readback and genuine recovered CPU views on fallback/notified context loss. No
manager/context mutation/yield or retained borrowed view allowed in consumer.
RenderResident maps unique ownership tokens to borrowed textures in the actual
production renderer; CPU layer surfaces may be null, avoiding their CPU mirror/
upload. Base/ownership/CPU chrome remain caller-owned synchronous inputs. Native
RecoverCpu explicitly handles renderer-only failure after rejected submission.
Default GDI and ShellSession/Presentation CPU snapshot path remain unchanged.
See G4B2D2_RESIDENT_PRESENTATION.md; persistent mixed ownership/real HWND/shell
wiring and raw-host/fault audit remain next, not full G4b.2 activation.

Creation now sets target nearest/level0 sampling: formerly only the separate
prototype presenter did so. Initial new test exposed incomplete sampling; also
corrected its test reference to independently compose ownership before the
software renderer, which intentionally renders canonical pixels. Final strict
new oracle passes twelve mixed GPU/CPU frames with scaled viewport, negative
origin, overlap/holes, chrome, cursor, asymmetric content change and exact
zero target reads/uploads, one CPU base/overlay upload. Invalid token inputs
preserve FBO/GPU; retirement/renderer teardown preserves borrowed resources.
Renderer GL error + explicit recovery compares final genuine CPU replay pixels.
Actual EGL loss test additionally consumes retired-image CPU views without GL.
Negative old-sampling control fails at first resident pixel; focused ASan/UBSan
passes (driver leak scan disabled). Earlier pre-final suite was stopped after
adding explicit renderer-failure recovery; full final-source rebuild follows.
No physical performance/acceptance or automatic ordinary GDI dispatch claimed.

Final G4b.2d.2 local validation: full fresh final-source MinGW/Mesa
COMPOSITION_GL_TEST=1 suite passed, including new resident production layers,
CPU coherence/live-x86 bindings, G3/G4a/G4b measurement/GL gates, G1/G2, workspace/
composition/input/session/file/dialog regressions, Stage C/six E9 and22 F groups.
Strict O2 shared-context/production-resident and target/context-loss tests pass.
Focused ASan/UBSan actual-renderer/target/shared harness passes final sources
(driver leak scanning disabled); negative old-sampling source fails new oracle.
No runtime/test changes follow the final suite. Map/link/browser checks and exact
published-SHA six-job CI, contract/log artifact and live Pages verification
remain completion gates. Local SDK absent; current physical checks deferred.
