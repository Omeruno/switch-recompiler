# Active development handoff — switch-recompiler

Branch: `claude/wonderful-hawking-10mpr0` only. Incoming verified HEAD:
`b337366b48f4cb8cb2f1dd26bedbc3622e670807` (2026-10-28).
[CI 40629027692](https://github.com/Omeruno/switch-recompiler/actions/runs/40629027692)
passed all six jobs: host/MinGW, CPU oracle, actual OpenGL/EGL reference,
ARM64/QEMU continuation ABI, Switch build and isolated JIT probe build.
This document and [ACTIVE_ROADMAP.md](ACTIVE_ROADMAP.md) are the current technical summary and implementation queue. Earlier investigative material
has been removed from the active handoff; use current source and subsystem docs,
not obsolete E1/common-dialog plans. No private hardware reports are reproduced.

## Current position: D3 real bounded INI/profile implemented; next waveOut/audio backend (2026-10-28)

Bounded F0–F5 software acceptance is complete; physical E/F remains deferred.
G2 CPU SRCCOPY is active. Claude's five Luna updates and shared PanelArea fix
are preserved. G3/G4a/G4b.1 implement isolated actual-GL GPU-GDI targets,
readback/recovery, generation/lifetime checks, direct texture composition and
ordered instanced fill batches. Default GDI remains CPU; G4b.2h selectively admits ordinary solid calls only in the explicit native experiment.

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
User-prioritized [F6a guest window gestures](F6A_GUEST_WINDOW_GESTURES.md) adds
real modal caption/edge/corner/button actions. [F6b native windows](F6B_NATIVE_WINDOW_ACTIONS.md)
adds true state/geometry/reflow and exact input/composition for panels and TXT/BMP.
[G4b.2d.3](G4B2D3_MIXED_FRAME.md) adds owning mixed CPU/resident scenes,
producer-independent redraw and explicit genuine software recovery.
[G4b.2d.4](G4B2D4_LIVE_WINDOW_FRAMES.md) builds actual live-window ownership
into retained mixed frames without a CPU readback barrier.
[G4b.2e](G4B2E_MIXED_WORKSPACE.md) adds native shell overlays, an owning
MixedWorkspace producer and real USER selection before CPU checkpoint, plus
output-fault acknowledgement/drain routing.
[G4b.2f](G4B2F_SWITCH_MIXED_OUTPUT.md) adds the opt-in persistent Switch
device adapter, staged genuine CPU recovery, cached cursor redraw and v9 counters.
[G4b.2g](G4B2G_HOST_PIXEL_AUDIT.md) adds guarded host CPU access, sticky store/owner
faults, suspended-session isolation, checked fallback and lifecycle barriers.
[G4b.2h](G4B2H_ORDINARY_GDI_DISPATCH.md) adds opt-in common solid FillRect/PATCOPY dispatch, public actual-x86 CPU/EGL/count/recovery gates and paired first-frame measurements. Default off; local Mesa first-frame path is slower than CPU. [G4b.2i](G4B2I_WARM_GDI_JOURNAL.md) measures cold/warm same-resource frames and removes redundant capacity checkpoints by retiring fully covered flushed journal commands. Real final GetPixel stays synchronous; default off. [G4b.2j](G4B2J_RETAINED_WORKSPACE.md) adds ownership-epoch guarded retained snapshots, native-shell reference/reuse measurements and real partial/DIB cases. Tiles9→1 builds; diagonal partial9→6; CPU DIB remains9. [G4b.2k](G4B2K_EXACT_DAMAGE.md) adds bounded adaptive exact damage and atomic masked CPU/capture exchange. Partial mid-series commits5→0, snapshots6→1, real DIB boundaries retained. [G4b.2l](G4B2L_CAPACITY_REPLAY.md) adds bounded private CPU replay after real GPU submission; partial capacity device reads8→0 while actual GetPixel/DIB reads remain. Four-policy48-exit/432-EGL-frame and budget/alias/fault/loss gates pass. [G4b.2m](G4B2M_DIFFERENTIAL_IMPORT.md) measures full CPU seeds and uploads only the changed rectangle; DIB upload bytes655360→66112,60 x86 exits/540 EGL frames with real reads unchanged. [G4b.2n](G4B2N_SOFTWARE_ACCEPTANCE.md) consolidates17 software evidence groups/five identity-bound reports and adds actual default CPU/native32 exits/288 EGL frames including DIB and size refusal. [G4b.2o](G4B2O_CPU_ROUTING.md) implements flag2 no-resident CPU output with cached redraw/transitions/fault gates and48 exits/432 EGL frames. [D1](D1_REGISTRY_KEYS.md) implements11 A/W registry APIs, actual HKEY grants/close and four fresh x86 writer/reader sessions. [D2](D2_KEYBOARD_LAYOUT.md) adds fixed-US maps/names and actual USER scan/side-state delivery. [D3](D3_INI_PROFILES.md) implements16 actual A/W INI/profile exports, checked VFS publication and four fresh x86 writer/reader sessions. Pinball passes GetPrivateProfileIntA; next missing WINMM!waveOutGetNumDevs. Next genuine audio device/backend; registry persistence remains separate.
Current ShellSession CPU canonical memcpy cannot remain plus duplicate GPU work
and be called an optimization. Actual-x86 GDI/window/readback/lifecycle fixtures
and CPU/EGL pixel/fallback comparisons precede selective activation. No physical
test request is needed to progress this work.

Global/current scope: [PROJECT_STATUS_RU.md](PROJECT_STATUS_RU.md),
[ROADMAP.md](ROADMAP.md), [ACTIVE_ROADMAP.md](ACTIVE_ROADMAP.md). These are now
consolidated current maps, not append-only competing historical next steps.
D API/page protection/unwind/COM and Pinball x86 remain open. Installer/ZIP/RAR,
broader native window/Z-order/Luna polish, multi-Process and H DirectDraw/D3D are
separate incomplete work; minimal JIT probe42 is not a dynarec acceptance.
Keep all no-stub/correctness/lifetime rules below. Detailed package history
follows; its old future-tense queues do not supersede this current position.

## User's long-term product target

Spore, Mirror’s Edge, The Sims 3 and Lineage 2 Classic / Asterios are explicit future PC-game targets; none is
certified working. [Architecture/acceptance direction](TARGET_GAMES_RU.md).
Build common compatibility/performance paths, not a Paint-specific accelerator.
Full ARM64 dynarec, GPU API/shader translation (audit D3D9 first), audio,
memory/streaming and thread correctness are separate required milestones.
Use real Horizon/homebrew backends within available system budgets; x86 is not
native ARM64 and DirectX is not a native Switch GPU API. No guaranteed100% usage
or playable FPS without version-specific physical evidence. Hardware remains
deferred; F6b native window actions are implemented within one tool group.
Next is a genuine waveOut/audio device/backend from the actual WINMM!waveOutGetNumDevs blocker, preserving all graphics/session gates. Checked registry persistence remains open.

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

G4b.2d.2 publication setup follow-up: a8adc2b / CI40346204083 passed
host/MinGW, actual EGL, ARM64/QEMU, Switch NRO and JIT NRO; public resident-layer
log/contract/NRO and live Pages independently verified. Only CPU-oracle compiler
installation remained running beyond eight minutes before tests. Applied the
same proven official Ubuntu HTTPS source/retry/timeout policy and ten-minute
step limit there too. Preserve suites/keyrings, gcc/g++ multilib and the exact
50000-all CPU oracle command; no runtime/tests or other jobs change. Verify all
six jobs on the final published config SHA rather than claiming the cancelled
first run was fully successful. No repeated local runtime suite needed for this
configuration-only follow-up; final C++ source remains the full-suite tested one.


## F6a — real guest nonclient window gestures (2026-10-26)

Cleanb3a8ae4/FETCH_HEAD, no newer work/AGENTS. User prioritizes real window
minimize/maximize/close/corner resize alongside continued system work. Guest
min/max/restore/close commands existed, but caption/edge dragging was absent;
native shell min/max were decorative. Implemented guest package first and
explicitly retained native F6b as incomplete, without changing Luna artwork.

Added owner-thread modal NC move/size via application SC_MOVE/SC_SIZE, real
GetMinMaxInfo/enter/sizing/moving/SetWindowPos/exit messages and capture. Geometry
changes trigger existing layout/client/paint/ownership logic. Release commits,
Escape rolls back; cancellation/capture theft/destruction/quit/host stop unwind
safely with identity revalidation after callbacks/yields. Caption buttons commit
on matching release, retain WM_CLOSE veto; nonclient double-click detection and
minimize focus/capture cleanup added. No screenshot scaling or EXE switch.

Initial batched test lost an outside release before queued down had installed
capture. Added a bounded native origin identity for NC gesture input, explicit
capture taking precedence; fresh sessions cover sequenced and batched delivery.
After the initial28 stages passed, extended to all eight directions, SC_SIZE veto
and parked-modal host abort. Earlier development suite stopped before final
SC dispatch/lifetime additions; final fresh full-suite validation follows.
See F6A_GUEST_WINDOW_GESTURES.md for limits and remaining native-window work.
G4b.2 mixed persistent frames/live-window/shell/access/fault queue retained.
Hardware/SD policy unchanged; local SDK absent, CI NRO distinct from acceptance.

Final F6a verification: full fresh COMPOSITION_GL_TEST=1 host/MinGW/Mesa suite
passed final runtime/fixture/harness tree, including44 completed x86 gesture
stages (sequenced + batched input) and one separate parked-modal host abort,
all prior GPU resident/coherence/bindings/CPU measurement tests, actual-x86
workspace/composition/input, Stage C/six E9/22 F and lifecycle/file/dialog tests.
Strict O2 changed USER-window/message modules, strict O2 harness and strict
CRT-free MinGW fixture pass. Focused ASan/UBSan changed window/message modules
and harness pass with ordinary other runtime objects (leak scanning disabled,
known makecontext/swapcontext warning retained). Negative ignored-minimum
control rejects on the guest's exact-size assertion. No runtime/test edits
after successful final suite; map/docs/browser and exact published-SHA CI/Pages/
public gesture artifact checks remain completion gates. Next F6b native windows;
no physical acceptance or complete XP shell claimed.


## F6b — native window geometry, actions and adaptive layout (2026-10-26)

Clean4a91409/FETCH_HEAD before changes, no newer local work. User continues F6b.
Added owned normal/current/maximized rectangles, native task minimize/restore and
real close policy. Panels and TXT/BMP move/resize in work area; fonts/icons are
redrawn at original size, content/row counts and hit rectangles follow geometry.
One modal native tool group remains; this is not arbitrary native multiwindow
instances or extra guest Processes. Legacy static bounds remain defaults; all
production native paths use instance geometry. See F6B_NATIVE_WINDOW_ACTIONS.md.

Switch idle drag now observes visual revisions and B rollback before navigation.
Active ShellSession does the same, routes native/guest input pairs and copies/
assigns ownership to dynamic panel + modal rectangles independently. Native
minimize releases guest keyboard routing; restore owns its input again. Existing
modal/dirty-save/copy policy preserved. SetGuestActive/EndGuest/Enter/CloseDialog
cancel gestures before changing native owners. Luna assets/CPU GDI preserved.

Initial full development suite found the busy-settings oracle expecting the old
oversized fixed overlay. Corrected to exact dialog bounds plus adjacent real
guest pixel; no weakened comparison. New native geometry/files/TXT/BMP harness
and extended actual-x86 workspace independent full-frame oracle cover move/
resize/reflow/min/max/cancel/task/dirty close, document outside moved parent,
normal/abort/restart and software/actual EGL. Targeted invocation corrections
removed accidental broad generated-EXE globs/ignored-object discovery; only final
successful runs count. No user/private binaries touched.

Full fresh final-runtime/harness COMPOSITION_GL_TEST=1 MinGW/Mesa suite passed,
including native test, busy/workspace software/EGL,44 F6a stages+parked abort,
G3/G4/resident/coherence/measurement, G1/G2, Stage C/six E9/22 F and prior files/
dialogs/sessions. Strict O2 changed native modules pass. Focused ASan/UBSan
DesktopShell/layout/theme/bridge and native/workspace tests pass; remaining
objects ordinary, leak scanning disabled, known native-fiber warning retained.
Negative old fixed compose rectangle fails the independent per-pixel oracle.
Actual native screenshots inspected. No runtime/test edits follow final suite.
Map/docs/link/browser, exact published-SHA six-job CI, native artifact and live
Pages checks remain final gates. Local SDK absent; hardware/SD policies unchanged.
Next G4b.2 owned mixed frame/window/shell/access/fault integration, not full XP.

F6b C++17 lifetime follow-up: eb67ac3 CI40388441146 passed ARM64/CPU/EGL/
Switch/JIT, but host GCC13 failed browser close/focus, new maximize, busy input
and workspace minimize. Root cause: conditional temporary initializer_list
backing array in WindowHit's caption range-for. Local GCC14 temporary handling
had masked it. Replaced only that loop storage with owned std::array<int,3>.

Installed diagnostic GCC13.2 rootlessly outside git at
/workspace/setup-tools/f6b/gcc13-check/root/usr/bin/x86_64-linux-gnu-g++-13.
Verified pinned Ubuntu archive fingerprint F6ECB3762474EDA9D21B7022871920D1991BC93C,
signed Noble Release, index SHA256 and downloaded package SHA256; no TLS/signature
bypass or system compiler replacement. GCC13 old layout module emits dangling
pointer warnings and reproduces CI's exact native maximize assertion; fixed
module passes same native harness with identical ordinary common runtime objects.
This is focused module evidence, not a full local GCC13 suite. Old/fixed logs and
verified toolchain materials are in /workspace/setup-tools/f6b/.

Fresh complete fixed-final-suite.log passed final implementation/tests, including
new native windows, actual-x86 software/EGL workspace/busy input, F6a and all
prior CPU/GDI/GPU/session/file/C/E/F gates. Focused sanitizer rerun follows the
final fixed layout; unchanged other instrumented UI/bridge modules and ordinary
common-runtime objects. Verify final exact published-SHA CI six jobs and native
artifact before closing; no physical acceptance, SD or hardware request added.


## G4b.2d.3 — owned mixed presentation (2026-10-27)

Clean5f72b67/FETCH_HEAD, no newer work/AGENTS. Incoming CI40390490786
independently reverified all six jobs. Implemented MixedPresentation as explicit
native retained entry: owned base/ownership/CPU overlays and geometry plus GPU
resource pins, no producer/manager/guest/GL-view pointer retained. Atomic reject
preserves old state; authoritative owner validation rejects unknown/outside
owners. Native CPU snapshots have stable revisions; resident contents stay live.
See G4B2D3_MIXED_FRAME.md for bounds, submission flush side effects and limits.
No live HWND/ShellSession/default GDI path is activated or claimed accelerated.

Healthy redraw uses production resident renderer without target readback/upload;
explicit software boundary validates manager then recovers real checkpoint/
journal pixels outside consumer. Actual context loss requires notification first.
Foreign/reset/shutdown reject; target retirement retains sole frame pin. Shared
NewPresentationResourceId prevents ordinary/mixed texture-cache identity aliases.
A preliminary suite was stopped after this cross-path identity refinement;
only final-suite.log counts as complete final-source evidence.

Fresh full COMPOSITION_GL_TEST=1 rootless MinGW/Mesa suite passed final runtime/
headers/harness tree, including retained mixed-source destruction, twelve pixel/
work-control redraws, all five injected trivial-buffer failures, malformed owners/
tokens, normal↔mixed backend reuse, retirement, renderer failure/software recovery,
and separate actual EGL-context destruction with pending writes and no current
context CPU replay. All prior live-x86 coherence/window/workspace/busy/session/
file/GPU/measurement tests and Stage C/six E9/22 F groups passed. CPU ordinary
GDI and old fallback remain unchanged. Strict O2 no-exceptions/no-RTTI standalone
harness and ASan/UBSan full target/renderer/presentation harness pass; driver leak
scanning disabled, injected allocation cleanup checked independently. Negative
forced ID+generation collision fails the intended cross-path pixel assertion.
An initial negative variant only forced ID and did not collide generation2;
that passing diagnostic was not treated as a valid negative control.

No runtime/test edits after final successful suite. Markdown121 local links/
fences, roadmap23 cards/3 views/24 document refs, shell syntax, diff checks and
local desktop/mobile browser actions pass. Six-job CI retained with standalone
mixed-frame source added to the existing EGL gate and contract in existing host
artifact. Exact published-SHA six-job CI/Pages/artifact verification remain final
publication gates. Local Switch SDK remains absent, host tools already installed;
no new environment configuration required. Hardware/Paint/GPU/fallback/input/
relaunch remain deferred, SD closed. Next validated live-window ownership → owned
mixed frame → shell/persistent frontend, raw-host/fault audit, then ordinary GDI.


## G4b.2d.4 — live-window owning mixed submission (2026-10-27)

Clean8dc667c/FETCH_HEAD, no newer work/AGENTS. Incoming CI40403499293
reverified six success. User asks to continue and show current position plus5–10
next steps; ACTIVE_ROADMAP now contains one nine-step dependency sequence, with
immediate GPU integration distinguished from larger D/dynarec/D3D9 follow-ons.
No dates/overall percentage or game compatibility is promised.

SubmitMixed now builds a private bounded ownership plane from actual USER window
identity/layout/bounds/visible-region metadata for each dirty coherent GPU slot.
DescribeWindowImage bypasses CPU access without reading pixels or capture setup.
Full visible seed+GPU writes own those visible pixels; occluded/CPU-only windows
stay on CPU base. Fixed four resources/tokens plus one reusable screen-sized owner
scratch; overlapping dirty-slot DC-visible regions are unsupported/rejected.
MixedPresentation owns snapshots/pins; native wrappers enforce same USER session,
health and active GPU before GL. Explicit CPU route uses real recovery after
context-loss notification. No ordinary GDI or ShellSession/frontend activation.
See G4B2D4_LIVE_WINDOW_FRAMES.md for exact scope and failure/lifetime semantics.

Public coherence PE32 fixture retains initial64-fill/one-import/one-readback and
two-dirty-window gates. Adds an actual CPU popup occluding the GPU window; real
x86 stages, full1280x720 EGL and software expected pixels, unchanged canonical
CPU pixels before barrier, no extra target reads/uploads/barriers/commits for
submission/redraw, injected failed snapshot preserving the old frame, nested
session rejection, retirement pins and real GL-error/actual-context-loss fallback.
Ten stages per mode still pass with original work counts. Initial harness had
assumed its lease survived creating the popup and resources freed despite retained
pins; corrected to reacquire the genuinely stale lease and assert128x128 retained
budget after Prune, then revoke/free through shutdown+Clear. No runtime workaround.
An initial diagnostic object-count assertion incorrectly assumed >100 objects;
corrected to the actual directory inventory before targeted linking.

Fresh full COMPOSITION_GL_TEST=1 rootless MinGW/Mesa suite passed final runtime/
headers/public fixture/inspector sources, including new live mixed/CPU occlusion
checks and all prior native/window/workspace/GDI/GPU/measurements/sessions/files,
Stage C, six E9 and22 F groups. Strict O2/no-exceptions/no-RTTI binding and
inspector pass. Focused ASan/UBSan bridge/inspector and GPU/renderer pass with
ordinary other runtime objects, driver leak scanning disabled and known native-
fiber warning retained. Negative full-rectangle visibility source fails exact
CPU-popup pixel144,154 (black instead of6912cd), proving the ownership gate.
No runtime/test edits after full-suite success. Existing host artifact gains the
new contract; six core jobs/test commands unchanged. Roadmap browser/link/source/
diff checks and exact published-SHA six-job CI/Pages/public fixture/contract/NRO
verification remain final publication gates. Local devkitA64/libnx still absent;
current physical Paint/GPU/fallback/input/relaunch deferred, SD closed. Next
native ShellSession overlays into owned mixed frame, persistent frontend and
PresentIfNeeded CPU-barrier selection, raw-host/fault audit, then bounded GDI.


## G4b.2e — native overlays, real USER selection and output fault routing (2026-10-27)

Clean49b85d0/FETCH_HEAD, no newer work/AGENTS. User requests taking a larger
connected block. Implemented ShellSession native-only overlay without Compose/
guest CPU checkpoint; mixed window submission merges actual dynamic panel/dialog/
Start/taskbar ownership above resident windows. MixedWorkspace owns the frame,
borrows bindings/shell/output and installs/restores scoped native producer fields.
Real PresentIfNeeded selects that producer before a CPU checkpoint, observes GPU-
only write epochs, shell/CPU revisions and unique producer identity, and retains
legacy/default CPU authority ordering. Unavailable uses genuine CPU fallback;
Failed latches USER output fault without acknowledgement/retry. Switch guest
OutputHealthy observes the fault and uses existing stop/drain/return. See
G4B2E_MIXED_WORKSPACE.md for output callback/lifetime/fallback/budget contracts.

No ordinary GDI dispatch or mixed device/swap adapter is activated in Switch.
Native public-x86 integration and real USER output selection are implemented;
persistent Switch renderer/swap/cursor/lifecycle adapter remains the next step.
No Paint FPS, broad GDI/game compatibility or physical acceptance is inferred.
Raw-host pixel/coherence fault audit still remains before bounded admission.

Extended existing20-stage x86/EGL coherence inspector with actual selected USER
mixed output, native taskbar/Programs/Start/Preferences, panel move/max/min/restore,
dialog outside owning panel, close/input isolation and real CPU-popup occlusion.
Every1280x720 output pixel compared independently; duplicate-color GPU write must
select output with unchanged CPU screen/shell state. No target readback or CPU
barrier/commit added by native overlay/selection, original first64-fill transfer
counts preserved. Unchanged call stays no-op. Output rejection preserves CPU/
producer acknowledgement and timestamp, latches both faults, prevents retry and
is absent in the next fresh session. Existing present-progress test adds actual
software selected/unavailable output and CPU gate counts, plus failure no-ack.

Development suite passed; after strengthening GPU-only epoch, moved-dialog and
failure checks, fresh final-suite.log passed final runtime/headers/tests, including
all prior workspace/busy/window/GDI/GPU/readback/measurement/lifetime/files, Stage C,
six E9 and22 F groups. Strict O2/no-exceptions/no-RTTI native selector/controller
and final inspector pass. Focused ASan/UBSan changed shell/binding/controller/USER
selector/inspector plus GPU/renderer modules pass; other runtime objects ordinary,
driver leak scanning disabled, known native-fiber warning retained. Negative
pre-selector CPU barrier fails unchanged-submission accounting; omitted native
dialog ownership fails pixel24,20. No runtime/test edits after final full suite.

Six-job workflow unchanged except new contract in existing coherence artifact.
Map23cards/3views/26docs, Markdown local links/fences, browser desktop/mobile,
shell syntax and diff checks are final local gates. Exact published-SHA six-job
CI/Pages/coherence log/contract/public PE32/NRO must be verified after publication.
Local devkitA64/libnx absent; CI compiles Switch-only OutputHealthy change. No
hardware/Paint/GPU/fallback/input/relaunch test requested; deferred list retained,
SD closed. Next persistent Switch device/swap/cursor adapter, raw-host/fault audit,
then bounded common GDI GPU admission and measured transfer/checkpoint costs.


## G4b.2f — opt-in persistent Switch mixed output/cursor adapter (2026-10-27)

Cleanb6450f1/FETCH_HEAD, no newer work/AGENTS. User requests the next large block.
Implemented MixedOutput native device protocol: actual production GPU render then
swap; failed render never swaps; failed swap is not successful output. Fallback
first stages full genuine recovered CPU RGBA, then device destroys context,
manager receives ContextLost, software output is prepared and complete pixels
queued. Staging/replay/setup/queue failure latches fault, no repeated work or false
acknowledgement. Output staging bounded1280x720x4 persists across same-policy
guests, freed at device close. See G4B2F_SWITCH_MIXED_OUTPUT.md.

MixedWorkspace now Refresh/Redraw/FrameCurrent caches the owning frame for cursor
movement. CPU/GPU/shell/layout epochs and current USER identity invalidate it;
metadata-only layout change cannot reuse an obsolete mask. Direct failure also
latches USER output fault. Switch Blit routes cursor/visibility through this
owning source, not an old CPU canonical frame. Source refresh uses USER ack.

Actual Switch adapter owns swap/linear-framebuffer queue/fallback and metrics.
RunInterpreter's scoped mixed producer/bindings detach/shutdown after continuation
drain, before shell/USER and context/device close; device persists across guests.
Explicit first byte1 of sdmc:/switch-recompiler/gpu-gdi-native.flag enables this
native experiment only on new UI with live GPU/shell. Defaults off; L/ZL and sticky
software policy preserved. Ordinary GDI remains CPU and creates no GPU targets
through application calls. Native inspectors exercise real resident work. Do not
claim this flag accelerates Paint or ask for physical tests as a prerequisite.
Renderer report v9 adds separately scoped native output/cursor/fallback/storage
counts; legacy canonical input correlation is not given fabricated mixed serials.

Existing20-stage x86 coherence/USER/native-shell fixtures retained. Seven extra
fresh public-x86/EGL sessions park real guest windows, inject genuine GPU fills
and compare all1280x720 output pixels with independent native/guest/cursor oracle.
Each starts12 retained cursor frames, stable snapshot/base upload and zero extra
target reads. Modes cover GL error, swap rejection, staging allocation, software
setup, queue rejection, actual EGL destruction/no-current-context CPU recovery,
and metadata-only layout change forcing resubmit. Exact device event order and
successful-vs-rejected counts checked; failure prevents retry, buffers release.
These seven guests are host-aborted parked sessions, not fabricated completed exits.

Initial development suite failed only because the new inspector named a nonexistent
GuestExecution::Stop method. Removed that harness call; real destructor drains
parked continuations. Targeted seven-mode/20-stage test then passed. Fresh final
COMPOSITION_GL_TEST=1 rootless MinGW/Mesa suite passed final runtime/headers/tests,
including all prior measurement/GDI/GPU/window/native/workspace/busy/lifecycle/file,
Stage C/six E9/22 F gates. Strict O2/no-exceptions/no-RTTI native output/controller
and final inspector pass. Focused ASan/UBSan native bridge/controller/output/USER
plus actual mixed/target/renderer/inspector pass with other runtime objects ordinary;
driver leak scan disabled, known native-fiber warning retained. Negative forced
resnapshot fails retained work counts; swapped rejected render fails required
CPU recovery/output counts. No runtime/test edits after successful final suite.

Map23cards/3views/27docs, Markdown links/fences, shell syntax, browser desktop/mobile
and diff checks are final local gates. Existing coherence artifact gains the new
contract; six core jobs unchanged. Verify exact published-SHA six-job CI, Switch
NRO, coherence log/public PE32/contract and Pages before completion. Local SDK
absent; libnx/flag/framebuffer integration is CI-compiled, not host-executed or
physically accepted. Hardware/Paint/GPU/fallback/input/relaunch deferred, SD closed.
Next raw-host pixel/coherence-fault audit then bounded ordinary common GDI admission,
with real x86/CPU/EGL/count gates before performance claims. Dynarec/D3D separate.


## G4b.2g — registered host pixel/fault audit (2026-10-27)

Cleandc254b2/FETCH_HEAD, no newer work/AGENTS. User requests next large block.
Audited current production screen/Surface consumers, metadata queries, USER
layout/lifecycle, private GPU images and CPU-only DIB/native chrome paths. Full
inventory and bounded exceptions in G4B2G_HOST_PIXEL_AUDIT.md. No universal raw-
pointer protection, arbitrary host mutation or default GPU admission is claimed.

Gdi::Screen now acquires CPU authority and returns a const synchronous view;
ScreenForWrite acquires authority and dirties output for raw host writes. Existing
screen/provider replacement is guarded. CPU preparation failures latch in the Gdi
store; expired owner is observable before pixel access, and reentrant failure
cannot be masked. Fault cannot be cleared by later successful callback/detach/
rebind/reinstall. Gdi/session exchange preserves isolation. Native bindings record
first coherence cause and mark their own USER fault immediately. Foreign-session
Acquire/Prune/Checkpoint/Disable reject without poisoning a suspended owner;
Shutdown still revokes weak owner safely if teardown cannot detach normally.

TryCompose rejects lost authority explicitly; compatibility Compose is invalid,
never the old successful output surface. Direct Switch fallback/clock and CPU
output check the gate/result; Blit/OutputHealthy observe CPU faults, including
expired owners. No old flat frame is queued after an authority fault. v9 summary
adds CPU access fault and native coherence reason. BeginPaint/ScrollCommon gate
before consuming paint/child state; fallback OnGuiThreadExit gates before retiring
windows but still cleans native handles/queues on error, with output latched shut.
This preserves native draining; exposure/forced-thread compatibility is unchanged.

New test_cpu_access covers guarded views/write epoch, expiry, sticky/reentrant
failure, failed rebinding/reinstall and moved-store isolation. Four public-x86/EGL
sessions extend existing20 stages/seven device modes: suspended owner rejection
without readback/poison; host read+raw write+GPU reimport/private pixel check/full
CPU/EGL compare and actual guest GetPixel success at stage2; stale layout refusing
CPU/compose/output with untouched canonical pixels and healthy nested store;
thread-exit entry checkpointing before retirement; same retirement completing
on fault without publishing stale pixels. Cases are host-inspected/aborted, not
new full arbitrary forced-thread termination or complete guest-exit acceptance.

Earlier development rebuilds were stopped after tightening header/owner/fault
boundaries and adding lifecycle cases, avoiding stale ABI evidence. Fresh final
COMPOSITION_GL_TEST=1 rootless MinGW/Mesa suite passed final runtime/headers/tests,
including new CPU-access test/four audit cases, original GPU/window/workspace/busy/
coherence/readback/lifecycle/file/measurement gates, Stage C/six E9/22 F. Strict O2
no-exceptions/no-RTTI changed GDI/binding/shell/thread/message and final inspector
pass. Focused ASan/UBSan changed GDI/bridge/USER/shell/mixed/GPU/renderer/inspector
pass with ordinary other runtime objects; driver leak scan disabled, known native-
fiber warning retained and preexisting unused ScrollCommon CPU parameter warning
allowed for that focused build only. Negative nonsticky preparation fails CPU
fault invariant; poisoned suspended Prune fails owner-health assertion. No runtime/
test edits after successful final suite. Existing coherence artifact adds native
CPU-access log and this contract; six core jobs unchanged.

Current maps consolidated to ordinary GDI admission as next, removing old resident-
frame/presenter/audit future-tense queue remnants. Roadmap23cards/3views/28docs,
Markdown/link/fence/shell/diff/browser checks and exact published-SHA six-job CI/
Pages/coherence+CPU logs/public PE32/contract/NRO remain final publication gates.
Local devkitA64/libnx absent; direct Switch fallback/fault diagnostics CI compiled,
not physical acceptance. Paint/GPU/fallback/input/relaunch deferred; SD closed.
Next bounded common solid-window GDI dispatch, eligibility before CPU Target and
explicit unsupported/accepted/fault handling with public x86/CPU/EGL/count gates.

## G4b.2h — ordinary solid dispatch and paired first-frame costs (2026-10-27)

Cleana0a2771/FETCH_HEAD, no newer work/AGENTS. User authorizes several linked
steps. Implemented bounded ordinary common FillRect/solid PATCOPY admission before
CPU Target via weak session-owned backend and explicit refused/accepted/failed
result. Native bindings validate top-level identity/map/clip/size/budget, seed
before raster, never duplicate accepted raster on CPU and fail closed on partial
failure/expired owner. FrameRect/FillRgn/FrameRgn propagate child failure. CPU gate
cannot detach while solid backend is installed. Existing flag1 now enables this
subset with native mixed output; default CPU, L/ZL preserved. v10 counters count
logical admission separately; host GDI timing label includes native preparation.
See G4B2H_ORDINARY_GDI_DISPATCH.md for full eligibility/recovery/report contract.

New CRT-free public x86 PE issues its own64 FillRect/PATCOPY calls, not injected
native raster. Sixteen normal-exit0 sessions: seven same-build/PE CPU/GPU pairs
plus allocation-refusal and actual-context-destruction recovery. Fixed first
phase gates64 operations/one import+upload/one64-fill batch+seed draw/no readback
for output, unchanged canonical CPU bytes, then one GetPixel checkpoint and real
ROP/DIB/raw stores/GetDIBits. Four full1280x720 CPU/EGL phases, selected clip/
origins, hatch, child/screen/memory, side/combined pixels/fifth image refusal,
resize/stale identity and resource cleanup. CPU gate tests cover failed/expired
solid owner despite live CPU owner, aggregate failure and no retry. Existing
coherence/device/host-audit gates unchanged.

Fresh complete COMPOSITION_GL_TEST=1 host/MinGW/Mesa final-suite.log passed final
runtime/header/fixture/inspector source; all prior C/E9/F22/native/lifecycle/file/
measurement/graphics gates pass. Strict O2/no-exceptions/no-RTTI changed GDI,
binding, GDI32 and final inspector pass. Focused ASan/UBSan these modules plus
actual GPU/renderer/inspector pass with ordinary remaining runtime objects;
driver leak scan disabled. Negative deliberately rastering on CPU after accepted
GPU work fails first-phase import/readback count, not accepted as success.
Benchmark report gained cache-policy metadata after full suite; final measurement
script rerun against that suite's exact runner/PE passed. Later runtime edits only
correct diagnostic wording/comments; no semantic change after full-suite success.

Paired first-frame measurement separates actual guest raster, owning submit,
production driver output submit and guest read/ROP/DIB phase, excludes independent
oracle FBO read and guest sleeps; no GPU execution/scanout/FPS claim. Fresh images/
renderer include import/lazy shader preparation; not a complete application-
startup or warm-workload benchmark. Local Mesa GPU first-frame path is slower
than CPU despite batch reduction. No default activation or Paint acceleration
claim. Next cold/warm repeated same-resource experiment plus eligibility/ownership/
copy breakdown, then optimize the measured common-path bottleneck. G4b.2 remains
open; D/Pinball, full dynarec and D3D9 are separate work.

Host SDK ready for this workflow, local devkitA64/libnx absent; CI must compile
Switch-only flag/diagnostics. No new environment configuration/dependency needed.
Hardware Paint/GPU/fallback/input/relaunch deferred, SD closed. Maps now show
bounded admission and initial measurement implemented, cold/warm refinement next.
Roadmap/link/browser/shell/diff and exact published-SHA six-job CI, Pages and
public fixture/log/report/contract/NRO verification are final publication gates.

## G4b.2i — warm measurements, covered retirement and Lineage target (2026-10-27)

Cleand07a19f/FETCH_HEAD, no newer work/AGENTS. User requests next paired blocks
and Lineage2 Classic / Asterios in the game plan. Added target to TARGET_GAMES_RU,
all current maps and visual game/product cards. Exact server/client/chronicle,
PE architecture, graphics, network/auth/reconnect, launcher/protection and long
sessions need future audit; no client compatibility or game performance claimed.

First new public x86 workload measured nine frames in one persistent128×128
window/DC:64 actual ordinary FillRect/PATCOPY calls each, no inter-frame guest
pixel reads. Reference generated eight capacity readbacks before the first real
GetPixel. Implemented capacity-pressure retirement only for already GPU-flushed
opaque fills/copies wholly covered by a new accepting fill. Stable owned-command
compaction adjusts flushed prefix and copy bytes, frees obsolete snapshot storage;
no allocation/GL/yield/failure between retirement and accepting append. Pending/
partial work retains ordered replay and genuine capacity checkpoints. Fixed
64-command/4MiB and existing window/target budgets unchanged. Native same-build
control compares retirement off/on; EnableSolidDispatch enables it only in the
existing flag1 experiment, target shutdown resets policy. Default GDI still CPU.
See G4B2I_WARM_GDI_JOURNAL.md for eligibility, ordering and recovery proof.

Dedicated warm inspector:12 actual normal-exit0 sessions, CPU/reference GPU/
retired GPU with rotated order; initial triplet excluded, three cold plus24 warm
samples per mode. All108 output frames compare every1280×720 production-EGL pixel
to independent tile/background oracle. One seed/upload;9×64 calls/9 fill batches,
576 batched commands plus seed draw. Retirement512 removes capacity reads8→0;
real final GetPixel still reads once (reference totals9), commits actual pixels
once, then guest cleanup/exit. Final read time measured separately. Earlier
ordinary16-session and coherence/device/host-fault gates remain unchanged.

Native five-case oracle verifies partial coverage checkpoint, wholly covered copy
bytes freed/custom allocator zero, pending suffix partial-overlap order, full
unflushed journal refusing retirement, real GL error replay and actual EGL-context
destruction/no-current-context recovery. Strict O2/no-exceptions/no-RTTI native
and live warm inspectors pass. Focused ASan/UBSan core/manager/bridge/production
renderer/live inspector pass with other runtime objects ordinary and driver leak
scanning disabled; existing native-fiber warning remains. Deliberately marking
pending suffix already flushed fails independent pixel oracle as intended.

First development full suite passed preceding gates. Initial final full run
executed all runtime/native/warm fixtures successfully but failed the new Python
report collector: integer target-read count shadowed its sample list. Renamed
that variable, added read-sample identity gate and reran the corrected collector;
no runtime workaround. Fresh fixed-final-suite.log passed final runtime/header/fixture/inspector/report
sources: all new and prior native/GDI/GPU/lifecycle/file/measurement, Stage C,
six E9 and22 F groups. No runtime/test changes after this full-suite success. Reports record host/cache policy, exact source/PE hashes, cold/warm phase
samples and actual read counts; failed runner removes stale reports. No timing
threshold, Switch FPS or GPU execution/scanout duration is inferred. Current
local timings have cold preparation much larger than warm frames; next measure
owning snapshot/ownership/flush and partial/copy workloads before another shared
optimization or wider activation. Do not claim the whole G4b.2/game stack complete.

No new dependency/environment configuration needed; host MinGW/Mesa prepared,
local devkitA64/libnx absent, actual Switch code compiled by CI only. No physical
test request or SD rediagnosis. Hardware Paint/GPU/fallback/input/relaunch remain
deferred. New logs/report/public PE/contract join existing host CI artifact; six
core jobs unchanged. Roadmap23cards/3views/30docs, local links/fences, shell/diff/
browser and exact published-SHA six-job CI/Pages/NRO/measurement artifact checks
are final publication gates. All maps identify G4b.2i implemented and owning
snapshot/flush/partial/copy costs next; D/Pinball/dynarec/D3D9 remain separate.

## G4b.2j — retained native workspace snapshots (2026-10-27)

Cleanafe6815/FETCH_HEAD, no newer work/AGENTS. User requests continued engineering
work, then asks to resume during finalization; this is the same package, not a
new implementation queue. Baseline actual-x86/native-shell nine-frame workload
made9 snapshots/99,665,280 materialized bytes with unchanged CPU/layout/shell.
Implemented unique native ownership epoch separate from GPU pixel revision:
resident-entry, checkpoint/ownership clear, retirement, shutdown and fresh manager
coherence invalidate; writes within existing resident targets keep ownership.
MixedWorkspace reuses only owning immutable CPU/mask/native snapshots with exact
CPU/layout/shell/ownership match and current healthy owner. Actual output still
flushes/renders/swaps live resources. FrameCurrent also checks GPU pixel epoch.
Shell NativeRevision observes task completion without rebuilding its mask; foreign
USER rejection occurs before suspended shell metadata observation. No guest/raw
surface pointer substitutes for owned state. Snapshot/reference policy has a
native same-build control; optional pipeline activation/default CPU/L/ZL unchanged.
v11 captures successful reused snapshots and materialized snapshot bytes; original
submissions/cursor/output/fallback/fault counters retain their scopes.

New inspector uses original CRT-free warm source and explicit PARTIAL/COPY build
variants.24 actual normal-exit0 sessions/216 full1280x720 production-EGL+native
shell pixel frames, rotated reference/reuse and cold/warm intervals. Tiles9→1
snapshot builds (99,665,280→11,073,920 bytes), real final GetPixel1 unchanged.
Diagonal8x8 partials exposed128-rectangle dirty-region pressure; five mid-series
CPU commits remain, builds9→6, pre-guest reads11/final12. Test initially assumed
only journal-capacity reads; diagnostic proved real dirty-region checkpoints,
so preserved them and gated exact counts rather than weakening pixels/budgets.
Actual raw guest DIB+SRCCOPY/sentinel requires all9 builds, pre-guest reads17/
final18/imports10/final commits10; no CPU-copy acceleration is claimed. Existing
G4b.2h/i ordinary/warm/journal/GL/context/state/CPU/device/fault/lifecycle gates
remain unchanged. Snapshot bytes include initialized revision arrays, not DMA.

Additional parked actual-x86 two-window native oracle: new resident owner with
unchanged CPU/layout, CPU checkpoint, guarded CPU-only mutation with stable
ownership, reimport, native Start/clock, layout-only update, suspended-session
rejection/restoration, manager reset with identical CPU/shell/layout, rejected
reuse output/no retry. Host-inspected/drained, not a normal-exit acceptance.
Negative ownership guard omission fails pixel300,50; CPU guard omission fails
pixel1000,110. Both prove actual stale-pixel detection. Strict O2/no-exceptions/
no-RTTI changed bridge/shell/controller plus actual GPU/renderer/live inspector
passes. Focused ASan/UBSan same modules/inspector pass with other common runtime
objects ordinary; driver leak scanning disabled, known native-fiber warning.

Development suite passed old gates. Fresh final-suite.log passed final runtime,
headers/public variants/inspector/report sources, all new/previous native/GDI/GPU/
window/workspace/lifecycle/file/measurement groups, Stage C/six E9/22 F groups.
No runtime/test changes after full-suite success. Report captures exact source/PE
identity, raw216 frames, work counts and host/cache policy. Refresh includes
output, and pending GPU flush moves into output on reuse; compare total cycle,
not prepare alone. No noisy timing threshold, physical FPS or universal speedup.
Next measured candidate: bounded dirty-region accumulation/coalescing with actual
CPU/visibility/capture-write gates; do not hide necessary CPU DIB checkpoints.
D/Pinball, full dynarec/D3D9, installer/archive and broader shell remain separate.
Lineage2 Classic/Asterios remains in the game plan without compatibility claim.

Host MinGW/Mesa ready, no new dependencies/configuration required. Local
Switch SDK absent; actual v11/libnx changes are CI compiled, not host-executed.
Hardware Paint/GPU/fallback/input/relaunch deferred, SD closed. New variant PEs/
report/log/contract in existing coherence artifact; six core jobs unchanged.
Current maps identify G4b.2j implemented and dirty-region costs next. Source/link/
fence/shell/diff, map23cards/3views/31docs and desktop/mobile browser checks plus
exact published-SHA CI six jobs/Pages/NRO/report+PE+contract are publication gates.

## G4b.2k — adaptive exact damage and atomic masked CPU/capture commit (2026-10-27)

Clean3233642/FETCH_HEAD, no newer work/AGENTS. User requests several connected
large steps. Implemented bounded exact damage, atomic CPU/capture exchange, and
expanded same-build reference/retained/exact measurement plus lifetime/fault cases.
Default CPU GDI, native experiment flag1, L/ZL, Luna and previous session/input/
read/ROP/DIB/fallback contracts unchanged. No per-EXE path or physical FPS claim.

Initial always-masked experiment removed partial dirty-region checkpoints but
added unnecessary metadata to simple tiles. Final adaptive tracker keeps sparse
Region<=128 first, stops candidate union at first excess, then converts ONLY old
accepted writes into an owned bitset. Mark new pixels only after actual Fill
success; no bounding-box expansion, no CPU color raster duplication per operation.
Fixed512 side/combined512²/four-target budgets derive live mask cap32771 bytes.
Buffers reuse after clearing and release on retirement/shutdown. Allocation/budget
refusal preserves the original Region/checkpoint path. Same-build native control
requires current healthy owner; existing masked writes survive policy changes
until the real checkpoint. See G4B2K_EXACT_DAMAGE.md for transaction and limits.

Gdi CommitWindowMask validates entire identity/layout/frame/dimensions/pitch/
alignment/byte count/padding/marked visibility set before capture or mutation.
Only exact marked pixels change canonical/retained pixels and capture ownership.
Failed late capture writes nothing; one nonempty transaction bumps CPU revision
once. GPU/private bindings include both Region and mask in resident/seed/prune/
checkpoint decisions, retaining ownership epochs and live snapshot reuse. v12
records promotion/refusal/masked commit/pixel/peak memory counts through shutdown.

Expanded public native-shell matrix:36 actual ordinary-x86 normal exits/324 full
1280x720 independent EGL pixel frames. Three rotated same-build policies isolate
historical full snapshot/Region, incoming retained/Region and retained/exact damage;
initial triplets excluded. Partial mid-series CPU commits5→0, snapshot builds6→1,
materialized bytes66,443,520→11,073,920, pre-guest target reads11→8/final12→9.
Remaining8 journal-capacity reads are real; tiles unchanged, no mask allocation.
Real raw-DIB+SRCCOPY still needs9 builds/pre-guest17/final18/imports10/CPU commits10.
Bitmap2048 bytes only after partial fragmentation; zero for simple tile/DIB cases.
Previous ordinary/warm/journal counts, selector/device/recovery/snapshot boundaries
stay gates. Report schema2 records3 policies,324 raw frames, mask bytes and hashes.

New public odd37x29/child/occluder/selected-hole/window+viewport-origin/sparse
Win32 fixture:4 normal exits cover CPU reference, masked/reused-bit checks,
permanent allocation refusal and actual EGL destruction software recovery; one lost-layout
fault case parked and drained, not a normal exit. Exact pixel counts and retained
owners of untouched pixels match CPU;135 mask bytes, allocator zero at shutdown.
Repeated sparse cycles plus full clipped row-byte spans verify odd bit boundaries,
zeroing and union semantics. Foreign-session control rejects without poisoning
owner. Native unit checks malformed length/padding/pitch/alignment, invisible
hole/identity/layout and late capture atomic rejection, exact4 changed pixels/
retained owners. Negative missing visibility fails hole rejection; stale reused
bits fail accepted-pixel count on the next fragmented cycle.

Earlier development and intermediate full suites passed preceding runtime gates.
Fresh final-boundary-suite.log passed final runtime/headers/public variants/native
unit/report/matrix and all prior GDI/GPU/window/GL/state/lifecycle/file/measurement,
Stage C/six E9/22 F groups. Full-row/odd-mask oracle was additionally strengthened
and latest focused strict O2/no-exceptions/no-RTTI and ASan/UBSan core/bridge/GPU/
renderer/live mask cases pass; workspace strict/ASan324-frame matrix also passes.
Other common runtime objects ordinary in focused sanitizer builds, driver leak
scan disabled, known native-fiber warning. No runtime changes after final full
success. No timing threshold, universal speedup, GPU timestamp or scanout claim.

Next: remaining journal-capacity synchronization on shifted partial writes and
CPU import/checkpoint cost; evaluate bounded replay/checkpoint alternatives with
actual GL/CPU/read/ROP/DIB/lifetime gates before changing them. Do not hide real
application reads or CPU DIB barriers. D/Pinball/full dynarec/D3D9/installer/archive
and games (including Lineage2 Classic/Asterios) remain separate unsupported goals.

No new dependency/configuration required; host MinGW/Mesa prepared, local Switch
SDK absent. Actual v12/libnx compilation is CI-only; physical Paint/GPU/fallback/
input/relaunch deferred, SD closed. New public PE/native+GL logs/contract join
existing coherence artifact, six jobs unchanged. Maps show G4b.2k implemented,
journal/checkpoint costs next. Roadmap23cards/3views/32docs/link/fence/shell/diff/
browser and exact published-SHA six-job CI/Pages/NRO/report+publicPE+contract are
final publication gates. Preserve newer work and normal branch push only.

## G4b.2l — bounded capacity replay, ownership/fault guards and four-policy measurement (2026-10-27)

Clean146647c/FETCH_HEAD, no newer work/AGENTS. User requests several linked large
blocks before sleep, continuing tomorrow. Completed capacity-checkpoint alternative,
input/order/budget/loss protection, and public measurement/integration as one
coherent package. Preserve this exact handoff/queue for the next launch; no alarm
or autonomous chat wakeup is installed or implied. No hardware test requested.

Existing shifted partial scenario still had8 journal-capacity device readbacks.
Room can now flush all pending GPU work FIRST, then replay owned opaque Fill/Copy
journal into its private already-allocated CPU checkpoint. Does not mutate
canonical/window/capture/ownership epochs or upload/render a duplicate canonical
CPU frame. Real extra CPU replay replaces synchronization/full-image checkpoint
copy; counters record that cost. Optional work cap262144 clipped pixel applications
counts overdraw; larger work retains reference readback. Fixed64-command/4MiB
input/target limits remain. Failed flush already recovers complete old journal,
then incoming Fill/Copy applies once in CPU mode. Incoming Copy still snapshots
BEFORE Room, including source alias of the checkpoint being changed. No new
allocation or guest callback in successful replay. Native existing experiment
selects policy; isolated prototypes/reference controls remain off; manager reset
clears policy. See G4B2L_CAPACITY_REPLAY.md for complete bounds/ownership/scope.

Real GetPixel/ROP/DIB/host/lifecycle reads still physically read device when GPU
healthy. CPU fallback after actual failure/loss remains genuine checkpoint+journal
recovery. Previous retirement-only warm comparison explicitly disables this new
policy, retaining its original gates. v13 separates capacity events/initiating-
target reads/private CPU replays/pixel applications/budget refusals/failed replay
submission, aggregated before retirement and captured through bindings shutdown.
No successful-replay claim on fallback recovery; no physical FPS interpretation.

Public native-shell matrix: four rotated same-build/PE policies,48 normal-exit0
sessions/432 independent full1280x720 EGL pixels. Partial: capacity reads8→0,
8 private CPU checkpoints/32768 pixel applications, final real GetPixel stays1,
no mid-series canonical commits,1 owning snapshot. CPU DIB: pre-guest reads17→9,
8 private checkpoints/129032 pixel applications, actual final read1,total10;
imports/canonical commits10 and snapshot builds9 unchanged. Tiles keep covered
retirement/no capacity work. Native aggregate counts match actual target stats.
Schema3 records all4 policies/raw432 frames/work/CPU replay/source/PE hashes and
host/cache identity. No noisy timing pass threshold or universal timing claim.

New native oracle: pending and already-submitted64 commands, independent CPU/
actual GPU-read pixels, owned-copy preservation after caller mutation, allocator
zeroing, actual EGL loss after private checkpoint, GL error/incoming-once fallback,
over-budget65x65 overdraw preserving readback,4MiB owned input pressure, failed
incoming allocation leaving old journal intact and clipped source alias captured
before capacity checkpoint. Negative skipped GPU submission fails actual pixels;
negative disabled work bound fails required refusal/readback gate. No private
binary or CPU-only false GPU acceptance. Prior mask/odd-child/occluder/capture/
context/fault/device/snapshot/read/ROP/DIB/lifetime tests retained.

Fresh final-suite.log passed final runtime/headers/native oracle/inspector/report
and all previous GDI/GPU/shared-state/window/workspace/lifecycle/file/measurement,
Stage C/six E9/22 F groups. Strict O2/no-exceptions/no-RTTI native core and full
workspace inspector pass. Focused ASan/UBSan same core/native/workspace modules
pass with other common runtime objects ordinary; driver leak scanning disabled,
known native-fiber warning. Both meaningful negative controls fail as intended.
No runtime/test changes after final full-suite success. Next measure actual
CPU-import/reseed bytes/cost before a bounded transfer optimization, preserving
real read/ROP/DIB/alias/capture/lifetime/fallback gates. D/Pinball, dynarec/D3D9,
installer/archive/game targets including Lineage2/Asterios remain separate.

Host tools ready, no new dependency/environment configuration; local Switch SDK
absent. Actual v13/libnx compilation remains CI-only. Physical Paint/GPU/fallback/
input/relaunch deferred; SD closed. Existing host artifact gains native capacity
log/contract; six core jobs unchanged. Maps show G4b.2l implemented/import costs
next. Roadmap23cards/3views/33docs, links/fences/shell/diff/browser and exact new
published-SHA6-job CI/Pages/NRO/report/publicPE/contract are final publication gates.

## G4b.2m — measured CPU reseeding, differential transfer and five-policy acceptance (2026-10-27)

Clean88be5ef/FETCH_HEAD, no newer work/AGENTS. Incoming exact CI40564792157
reverified all six jobs. User requests next large linked block. Host tools ready;
no new dependencies/environment configuration. Local devkitA64/libnx absent,
actual v14/Switch compilation remains CI-only. Hardware Paint/GPU/fallback/input/
relaunch deferred, SD closed; no physical test requested or result invented.

First instrumented and ran the original four-policy public inspector unchanged:
48 normal exits/432 EGL frames. Nine-frame DIB scenario imports10 full128² CPU
images/upload655360 bytes. Implemented bounded differential import against the
verified private checkpoint, retaining full CPU snapshots/input contract and
all real read/ROP/DIB/layout/lifetime barriers. First import remains full; later
healthy-GPU/empty-journal imports scan normalized RGB, accept unchanged without
upload, or pack one changed bounding rectangle into preallocated scratch then
use existing owned Copy/Flush. Outside rectangle compares equal; its unchanged
interior copies faithfully. No canonical/capture/ownership coarsening, raw-pointer
interception, new pixel buffer or GL resource. Dense input may still upload full
image with extra scan/pack cost. See G4B2M_DIFFERENTIAL_IMPORT.md.

Owned input precedes GL/recovery; refusal accepts nothing, GL error recovers
checkpoint+rectangle once, successful partial import updates private checkpoint
before journal release, actual loss replays later suffix. Manager policy applies
existing/new images and resets on Shutdown; healthy current-USER control retains
full-import reference. Only existing explicit native solid experiment selects it;
default CPU GDI/L/ZL/Luna remain. v14 reports seed attempts, snapshot/import wall
ns, attempted upload bytes, comparisons/unchanged imports through teardown.
Prototype additionally records actual attempted read payload, full input/packed
pixels. Timings include CPU scan/pack/copy/driver submit, not GPU duration/DMA.

Five rotated same-build/PE policies now60 normal-exit0 sessions/540 independent
full1280x720 production-EGL/native-shell frames; parked transitions remain separate.
DIB uploads655360→66112 bytes (initial full +9 sixteen-pixel rectangles),147456
comparison pixels/144 packed.10 full seeds/canonical commits,9 owning snapshots,
9 required pre-final reads +1 actual final read,8 capacity replays/129032 work
remain unchanged. Tiles/partial retain their previous counts. Schema4 records
all540 raw frames, bytes/input/scan/pack counts and cumulative seed phase times,
with per-frame deltas and phase medians; source/PE/host/cache identity retained.
No timing threshold or universal speedup: local focused seed import median about
52→29us, complete DIB cycle about5102→5849us with broad overlapping variation.
Host software GL/FBO oracle synchronization and concurrent validation load are
not physical Switch performance. End-to-end admission economics remains next.

New actual-EGL oracle reference/differential at1²/31x23/512²: pending rejection,
unused high byte/unchanged import, checkpoint alias, separated changes/rectangle
interior, single-pixel offset, source mutation after acceptance, allocation refusal
with old GPU pixels intact, full+partial GL error recovery, actual destroyed
context after partial import+pending fill, hostile PBO/pixel-store/viewport/scissor
restoration and allocator zero. Negative missing source x-offset fails real GPU
pixels; negative skipped partial checkpoint update fails actual-loss pixels.
Suspended-session policy change rejects without poisoning owner.

Fresh final-suite.log passed final runtime/headers/report/540-frame inspector and
all prior GDI/GPU/window/workspace/read/ROP/DIB/state/lifecycle/file/measurement,
Stage C/six E9/22 F groups. Final native/parked-owner assertions additionally pass
focused strict O2/no-exceptions/no-RTTI and ASan/UBSan native core + full workspace
inspector. Other common runtime objects ordinary in focused sanitizer build,
driver leak scan disabled, known fiber warning retained. Both negative controls
fail intended pixel gates. No runtime changes after full-suite success; final
report-only phase-delta extension reruns successfully against final strict runner.

Existing coherence artifact gains native log/contract; six core jobs unchanged.
Maps now mark G4b.2m implemented; next consolidated G4b.2 software acceptance and
complete CPU/native workload comparison, including admission refusal/CPU-DIB
costs, before prioritizing next interpreter/dynarec or graphics block. Default
activation/physical performance remain open. D/Pinball/full dynarec/D3D9,
installer/archive and game targets including Lineage2/Asterios remain separate.
Map23cards/3views/34docs, desktop/mobile browser/link/fence/shell/diff checks pass;
normal push and exact published-SHA6-job CI/Pages/NRO/report/publicPE/contract
verification are final publication gates. No alarm/autonomous wakeup is installed.

## G4b.2n — consolidated software ledger and actual CPU/native cycles (2026-10-28)

Clean a49b227/FETCH_HEAD, no newer work/AGENTS; incoming exact CI40578702073
reverified all six jobs. User requests next large block. No production runtime
source or default activation changes: this consolidates native integration
acceptance and measures the actual alternative CPU route before further tuning.
Host tools prepared, no new dependencies; local devkitA64/libnx absent, NRO CI-only.
Hardware Paint/GPU/fallback/input/Lite/relaunch deferred; SD diagnosis closed.

Added public600x128 size-refusal variant of existing warm fixture; generated PE
is excluded from generic no-argument sweep and runs in its dedicated harness.
New inspector rotates four same-build/PE CPU/native workload pairs,32 real normal
exit0 sessions/288 independent1280x720 production-EGL frames: tiles, shifted partial,
raw-DIB/SRCCOPY+sentinel, and oversized refusal. CPU installs no native coherence/
solid gate and uses actual ShellSession.TryCompose/Scene→Presentation.Submit/Render;
not a CPU image through empty mixed frames. Native uses current solid policy and
MixedWorkspace.Refresh. Exact pixels, candidate/accepted/refused/import/commit/
transfer/snapshot/reuse counts, actual final GetPixel and normal exit required.
Size refusal has576 candidates/refusals, zero admitted work/target allocation.

Raster sums only timed RunOne calls including real HLE/DIB/checkpoint work;
host bookkeeping/expected pixels/idle waits are excluded. Refresh includes native
prepare and actual driver submission; compare raster+Refresh. Terminal GetPixel
and guest exit tail are separate and included in sequence totals. No module load,
frontend setup/teardown, FBO oracle-read, physical swap/scanout or GPU duration.
Test-only clock overhead identical between routes. Schema1 records all288 frames,
32 read/exit sessions, cold/warm/sequence medians, input/runner/source/host/cache
identity; sample0 pairs excluded, no timing pass threshold. Local final warm CPU/
native cycle medians(us): tiles5187/2879, partial5107/3159, DIB5083/6518,
refusal6075/6200. Host variation is not Switch performance or default admission.

record_gpu_acceptance.py runs only after the successful complete MinGW/Mesa runner
and existing F report. Runner/reporter remove old ledger first.17 log groups
require named modes (including all seven device/four host audits), five subordinate
reports require expected schema/status/sample count and current HEAD/dirty state,
matching generated fixture/actual native runner hashes. Eight fixtures validate
actual i386 PE32 headers. Full source/test/tool code manifest supplements local
dirty-SHA evidence. Outputs PASS_BOUNDED_NATIVE_EXPERIMENT_PENDING_HARDWARE,
explicit default CPU, current activation/bounds and pending/outside scopes. Clean
exact-published-SHA CI report is the publication evidence. All logs/reports/public
PEs/contract join existing coherence artifact; still six core jobs. See
G4B2N_SOFTWARE_ACCEPTANCE.md. No parked case is counted as a normal exit.

Fresh final-suite.log passed final fixture/harness/scripts, new32-session/288-frame
matrix,17-group ledger, all prior window/native/CPU/GPU/shared-state/coherence/
read/ROP/DIB/context/lifecycle/file/measurement tests, Stage C/six E9/22 F groups.
Strict O2/no-exceptions/no-RTTI new inspector and seven coherent/GPU/renderer modules
pass. Focused ASan/UBSan same inspector/modules pass with ordinary other runtime
objects; driver leak scan disabled, known native-fiber warning retained. Negative
omitted CPU renderer fails actual framebuffer pixel0,0. Wrong-architecture input
fails measurement and removes stale report. Negative missing device6 result,
stale subordinate source SHA and mismatched PE hash each reject with no stale
ledger; restored genuine evidence reconstructs byte-identical original ledger.
No runtime/test/script changes after full-suite success, only documentation.

Measured next decision: retain default CPU/native opt-in; when no resident GPU
owners exist, evaluate ordinary CPU USER output selection instead of materializing
empty mixed frames. Next admission-aware no-resident routing needs real USER/
authority/acknowledgement/shell/cursor/ownership/lifetime/failure gates. This does
not fix DIB synchronization by itself. Bounded G4b.2 software acceptance now
consolidated, physical performance/default/broader GDI still open. D/Pinball,
full ARM64 dynarec/D3D9/installer/archive/game acceptance remain separate.
Maps show G4b.2n/next no-resident routing,23 cards/3 views/35 docs; links/fences/
shell/Python/diff and desktop/mobile browser checks pass. Normal branch push and
exact published-SHA six-job CI, clean ledger+five reports+8 PEs+contract/NRO and
Pages publication are final gates. No alarm/autonomous chat wakeup is installed.

## G4b.2o — CPU/native routing, cached output and real transition/loss gates (2026-10-28)

Clean4b39a4f/FETCH_HEAD, no newer work/AGENTS. User asks for large connected
blocks. Incoming CI40616821785 six success. Its timing ratios differ from local
G4b.2n (including DIB/refusal); no universal winner or default activation inferred.
Implemented explicit native alternative, retaining default CPU/flag1 reference.
First byte2 selects no-resident CPU routing with the same bounded solid admission.

MixedWorkspace checks owner/GDI/coherence/USER health before a metadata-only
resident-write query. With no GPU authority pending, checked ShellSession CPU
composition/scene is synchronously owned by a CpuOutput sink. It builds no mixed
snapshot, releases old mixed pins, owns cached CPU redraw and tracks all epochs.
New GPU writes switch back to genuine resident output without a CPU checkpoint;
a real CPU boundary commits before switching to CPU again. Callback failure
latches controller/USER fault without output acknowledgement/retry. Suspended
owners reject policy/refresh. Detach clears sink-owned storage without GL/USER.

Switch flag2 adapter uses actual canonical Presentation and a nonrecursive
BlitCanonical render/swap/software path. CPU refresh forces output after native
frames even if an older CPU serial matched. Existing genuine fallback commits
CPU authority before destroying the device and notifies ContextLost afterwards.
Existing teardown order preserved. Reportv15 counts CPU-route submissions/frames.
See G4B2O_CPU_ROUTING.md. Local SDK absent; main/libnx is exact-SHA CI-only.

Three same-build public modes CPU/native/routed now48 normal-exit0 sessions/432
independent1280x720 EGL frames with all previous tile/partial/DIB/refusal/read/
count gates. Refused600x128 routed case:576 genuine CPU refusals, no targets and
zero mixed snapshots,9 ordinary owning CPU submissions. Other workloads keep
native import/checkpoint/real-read semantics. Schema2 records all modes/ratios;
no timing threshold. Routed refusal is locally slower than reference native, so
flag2 remains an explicit comparison, never an assumed speedup. No Switch FPS.

Three additional parked real-x86 cases test actual USER CPU→GPU→CPU, real device
read, all output pixels, raw CPU write, clock, cached redraw without another
CPU barrier, suspended owner rejection, failed submit/redraw/no ack/no retry,
and actual EGL destruction followed by cached software pixels with no GL.
Parked guests are drained, not counted as normal exits. Negative ignoring pending
resident writes fails required actual-device readback counts. Final strict O2/
no-exceptions/no-RTTI and focused ASan/UBSan inspector plus seven coherent/GPU/
renderer modules pass; other common runtime objects ordinary, driver leak scan
disabled and known fiber warning retained. Early negative-control setup was not
a valid negative; only the final resident-guard mutation fails its intended gate.

Fresh final-loss-suite.log passed final runtime/headers/tests/scripts, all new
48-session/432-frame/three parked modes,17-group ledger/five bound reports, prior
GDI/GPU/window/shared-state/CPU-DIB/lifecycle/files/measurements, Stage C/six E9/
22 F. No implementation/test changes after final full success, only docs/map.
Maps advance to independent D registry creation/open/close semantics from the
known Pinball blocker; G4b.2 default/physical/broader-GDI gates stay open. Hardware
Paint/GPU/fallback/input/relaunch deferred, SD closed, no new dependencies needed.
Roadmap23cards/3views/36docs, doc/shell/Python/diff/browser and exact new-SHA six
CI jobs/Pages/NRO/clean ledger+report+publicPE+contract remain publication gates.

## D1 — real bounded registry family and verified Pinball startup advancement (2026-10-28)

User requests large blocks in one run. First package25ec8be was normal-pushed;
CI40620546240 verified all six success, Pages40620546090 success, exact clean
17-group ledger/48-exit432-frame report/8 public PEs/code manifest/contract/v15
NRO independently audited. Preserve flag1 reference/default CPU; routing flag2
remains a measured explicit alternative, not a universal performance claim.
Continued into independent D compatibility on clean published25ec8be; no newer
remote work/AGENTS found. Local SDK absent; host tools prepared, no new dependencies.

New registry_module.cpp replaces old create/open/query/set/close registrations
with11 real A/W stdcall exports, adding RegCreateKeyExA/RegSetValueExA/RegOpenKeyA.
Common backend stores intermediate keys, existing-only opens, case/relative/root/
default values, created/opened disposition and typed bytes. HKEY stores include
grants, QUERY/SET enforcement and actual close invalidation, no closed-slot reuse.
65536 allocations/session and4096-byte paths/component255 are explicit bounds;
refusal creates nothing. Root pseudo-handles remain valid. LegacySession exchanges
transient path/grant handles while preserving host values and suspended owners.

Checked mapped guest ranges/sizes before mutation, DWORD/QWORD widths, ASCII A/W
names and A-string↔canonical UTF16 conversion with exact byte counts/default values,
size-only/MORE_DATA behavior and last-error preservation. Unsupported name/ACP
encoding rejects1113; reserved/control/size/output/options/type/security/class/
WOW64 requests reject explicitly. W string bytes remain available; no silent
UTF16 truncation in registry APIs. Existing legacy USER class/title helper was
restored after focused compilation exposed its remaining callers; its behavior
is preserved. Existing enumeration/deletion successful stubs now return120;
full enumeration/deletion/security/ACL/full encoding are NOT implemented.

SaveToFile now reports actual stream/flush/close failure, including /dev/full.
Its old SD provider still has non-atomic/no-fsync/line-format/name/default-value/
large-value and checked-load gaps. RegSet success is live memory acceptance,
existing file save best effort; no durable registry/RegFlushKey/power-loss-safe
or complete SD acceptance. Checked versioned persistence preserving the old hive
is a separate explicit follow-on. See D1_REGISTRY_KEYS.md for complete boundaries.

Public CRT-free registry_keys.exe executes four fresh normal-exit0 sessions with
native inspection of stored DWORD and exact UTF16 bytes across writer/reader/
rewrite/reader. Real A/W lookup/default/ancestors/rights/close/stale/sizes/errors/
unsupported cases checked. Suspended-session grants/values, allocation-budget
refusal with no created key and real /dev/full failure are native gates. Historical
owned test10 had requested sam0 then expected writes; its in-memory instruction
fixture now requests QUERY|SET before the same real x86 stdcall/ESP/value checks.
No production permission bypass or per-EXE special case was introduced.

Private Pinball x86 probe outside git passes original missing RegCreateKeyExA,
performs real registry calls and creates a helper USER HWND, then stops at missing
USER32!GetKeyNameTextA (unknown-signature fatal). No playable game/main game frame,
Switch acceptance or FPS claim; no private EXE/frame/trace in public artifacts.
Next concrete block is common key names and VK/scancode translation, including
review of existing MapVirtualKey behavior, with public A/W/input fixtures.

Fresh final-root-suite.log passed final runtime/headers/public fixture/tests,
4 registry sessions + parent/root-empty identity/grants/close/budget/flush checks, all prior CPU/GDI/
GPU/shared-state/window/routing/read/ROP/DIB/lifecycle/file/measurement regressions,
Stage C/six E9/22 F and17-group GPU ledger/five reports. Strict O2/no-exceptions/
no-RTTI registry core/LegacySession/new inspector pass; host-function removal also
compiles without exceptions/RTTI. Focused ASan/UBSan new registry/legacy/inspector
passes with other runtime objects ordinary; driver/fiber leak scanning disabled,
known native-fiber warning retained. Negative no-close fails stale-handle validity;
negative ignored grants fails read-only mutation rejection. No implementation/test
changes after final full success, only docs/map/workflow evidence configuration.

Stage D is current on the visual map; GPU integration remains implemented bounded
experiment with physical/default/broader activation gates open. Updated global/
active/user maps and actual Pinball blocker; full dynarec/D3D9/audio/game acceptance,
installers/archive and richer shell remain separate. Roadmap23cards/3views/37docs;
links/fences/shell/diff/browser and new exact-SHA six-job CI/Pages/NRO/D-registry
public PE/log/contract/source-manifest audit are publication gates. Native D evidence
uses new stage-d-registry artifact in the existing host job; still six core jobs.
Hardware/Paint/GPU/fallback/input/relaunch deferred, SD diagnosis remains closed.


## D2 — common fixed-US keyboard translation and real USER scan/state delivery (2026-10-28)

Started from clean published854fe18; exact CI40625056179 six success and no
newer fetched work. No local devkitA64/libnx; independent host work continues.
New authored keyboard_layout table supplies MapVirtualKeyA/W modes0–4,
GetKeyNameTextA/W and physical USER events for existing04090409 profile.
Numpad/navigation virtual collisions are distinct from physical E0 positions.
Names use scan/extended/no-side fields; checked A/W buffers, NUL/truncation,
nonpositive capacities, errors and LastError preservation. No copied third-party
implementation, layout switching/IME/AltGr/localization or Windows oracle claim.

Host-only InputEvent scanCode added; full common rebuild required. Generic
modifier wParam carries actual sided scans; async observation is not rewound by
deferred queue removal. Queue keys update only on removal, retaining aggregate
state while another side is held. Legacy packed scan producers remain supported.
Public CRT-free keyboard_layout.exe and real interpreter inspector run four
fresh normal-exit0 sessions with19 keyboard events each. Native known mappings/
invalid-range tests and shell deferred-ring/E0/generic-right-side/async tests
complement actual A/W guest calls, errors, repeat, PM_NOREMOVE, numpad/navigation,
main/numpad Enter and left-release/right-held Shift state. Identity MapVirtualKey
and zero-message-scan negative controls fail intended public guest assertions.
See D2_KEYBOARD_LAYOUT.md for full contract and validation boundaries.

Private Pinball host trace stays outside git/artifacts. It passes key-name calls
and creates USER windows, then fatal missing KERNEL32!GetPrivateProfileIntA.
No accepted gameplay/main frame, Switch launch or FPS claim. Next block is real
bounded VFS-backed INI/profile A/W read/write/integer/default/enumeration/error/
size/persistence behavior; audit existing default/write-success stubs instead
of adding a signature-only bypass. Registry disk migration/encoding/enum/delete
remains open. Global/active/user/visual maps advance to this concrete dependency.

Full host/MinGW/Mesa suite passed with existing C/six E9/22 F/17 GPU groups
and five source-bound reports. Final extended native deferred-ring checks and
guest system-message/async-state assertions also passed focused reruns; exact
CI reruns all final fixtures. Strict O2 and focused ASan/UBSan changed-module
checks passed; known native-fiber warning and ordinary other objects retained.
Public negative controls fail their intended guest assertions. Logs stay outside git. New stage-d-keyboard evidence joins the existing host
job without adding core jobs. Exact published SHA six-job CI/Pages/NRO/public PE/
logs/contract/clean source-manifest verification remains the publication gate.
Hardware/Paint/GPU/fallback/input/relaunch deferred; SD diagnosis remains closed.

The GPU ledger decision now refers to ACTIVE_ROADMAP.md for independent D work,
rather than embedding a stale next-API claim in a graphics acceptance report.
This evidence metadata change preserves every GPU gate and default policy.


## D3 — genuine bounded VFS-backed INI/profile family (2026-10-28)

Incoming clean b337366; exact CI40629027692 six success/Pages40629097803,
NRO0/v15 and D2/D1/GPU source-bound artifacts previously verified. No newer
remote work. Host toolchain already prepared; local devkitA64/libnx absent.

New runtime IniProfile and common profile_module replace default-only legacy
GetPrivateProfileIntW/StringW and kernel32_extra write-success/default stubs.
Sixteen stdcall A/W exports cover private/global string/integer reads and writes,
section names and section key=value reads. Shared VFS maps bare names and profile
APIs to C:\WINDOWS, respects C/S/R/readonly/links/case, supports actual ASCII and
UTF16LE BOM files with checked guest input/output and explicit encoding errors.
Lists have double-NUL/truncation; scalar strings/quotes/defaults/case/integer
sign/hex/overflow/trailing behavior, real creation/update/key/section deletion.
Limits1MiB/file,32768/line/input,16384lines,4096path/case budget,32stage attempts.
No cache/retained guest pointers; accepted edits use exclusive sibling staging,
actual short/EINTR writes, fsync, close and rename with original-file preservation
on failure. No destination-unlink fallback or power-loss/directory-sync claim.
ASCII case folding/ACP subset, encoding/grammar/duplicate/metadata limitations
and unsupported IniFileMapping/profile structs are explicit in D3_INI_PROFILES.md.

Public ini_profile.exe + test_profile_api execute four fresh normal-exit0
writer/reader/rewrite/reader sessions and inspect real scores/comments/other
sections/Windows-directory files outside guest memory. UTF16LE non-ASCII W
reads/writes, A/unrepresentable write rejection, lists/errors/truncation/readonly
and all16 exports are guest assertions. Native provider tests inject actual
read/write/zero-write/flush/read-close/write-close/rename failures and short/EINTR
I/O, checking original bytes, bounds, readonly and malformed encodings. The early
development suite correctly failed a harness error: VFS is Process-local, so
pre-GuestSession configuration did not configure the guest. Final inspector
configures each fresh Process and loads its owned public PE inside that mount;
no production isolation bypass. Linear filtering avoids repeated vector erase
and section/key queries are folded once.

Final full COMPOSITION_GL_TEST=1 suite passed final code/public fixtures and
prior C/six E9/22 F/17 GPU evidence groups/five source-bound reports. Strict
O2/no-exceptions/no-RTTI provider/API/native/guest inspector checks pass. Focused
ASan/UBSan provider/API/inspector passes with other common objects ordinary,
driver leak scanning disabled and known native-fiber warning retained. Negative
write-success without storage and ignored fsync failed their intended public
guest/native assertions; logs/mutations stay outside git.

Private host Pinball trace passes actual profile calls and reaches fatal missing
WINMM!waveOutGetNumDevs. No gameplay/main-frame/console/FPS acceptance. Next is
an audio-device/waveOut audit and genuine supported backend with public PCM/
format/device/callback/buffer/teardown tests, not fabricated device count or
silent successful writes. Existing PlaySoundW implementation is separate.
Registry persistence migration/full encoding/enum/delete and dynarec/D3D9/richer
shell remain open. Hardware/Paint/GPU/fallback/input/relaunch deferred, SD closed.

Global/active/user/visual maps advance to actual audio dependency. New
stage-d-profiles artifact is in the existing host job; still six core CI jobs.
Roadmap23cards/3views/39docs. Normal push plus exact new-SHA six-job CI/Pages/NRO/
public profile PE/log/contract and clean GPU source-manifest audit are publication
gates. Incoming verified SHA is metadata baseline, not a self-referencing commit.
