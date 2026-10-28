# Active implementation map — 2026-10-28

[Визуальная карта проекта](https://omeruno.github.io/switch-recompiler/) · [Автономный HTML](roadmap/index.html) · [Как открыть и публиковать](roadmap/README.md)

**Current position: F6a/F6b bounded guest/native window actions implemented.
G4b.2o routing, D1–D4c function/event audio callbacks implemented; next actual frame position/pause/restart.** Runtime baseline `dcb2e5bf3811d62339b23d574947f394077da2df`
passed [CI40746297279, all six jobs](https://github.com/Omeruno/switch-recompiler/actions/runs/40746297279).
Ordinary GDI stays CPU by default; explicit native experiment admits bounded solid top-level window calls. No Paint speedup claim.

[User-facing global status](PROJECT_STATUS_RU.md), [global A–I plan](ROADMAP.md),
[technical handoff/history](AI_HANDOFF.md). These documents supersede old next-F1,
next-image/F5 and next-G1 notes. No overall project percentage is inferred.

## Product direction

The user's long-term targets are **Spore, Mirror’s Edge, The Sims 3 and Lineage 2 Classic / Asterios** as PC
games on Switch. These are planned, unverified compatibility/performance goals;
Paint/Minesweeper are common-path fixtures, not product limits. See
[TARGET_GAMES_RU.md](TARGET_GAMES_RU.md): full x86→ARM64 dynarec, real GPU API/shader
translation (prioritize auditing D3D9), audio, memory/streaming, thread correctness
and exact-version application acceptance. Do not promise full hardware utilization
or FPS, or add per-EXE shortcuts. Preserve the remaining CPU/GPU coherence and production integration queue;
choose subsequent CPU/H/D work by measured bottlenecks and actual dependencies.

## Global state

| Block | Implemented / accepted | Open |
| --- | --- | --- |
| A/B | CPU oracle/regressions, interpreter, scheduled guest threads/TEB/TLS/native continuations | Ongoing correctness, bounded HLE response, compatibility and performance |
| C | Limited one-Process HANDLE/wait/APC/timer/lifecycle scope accepted on Switch | Full NT/cross-process/security/AttachThreadInput outside acceptance |
| D | PE/DLL/TLS/unload/rollback, bounded SEH, substantial Win32/CRT | Page-right enforcement, full unwind/COM/API compatibility; D1–D4c bounded streaming/all callback modes implemented; positions/pause/broader formats remain open; host Pinball still needs MCI |
| E | CPU GDI reference, production Switch GL composition/fallback, retained windows/ownership/revisions, E8/E9 metrics/public oracles | Current physical Paint/GPU/fallback/Lite/input/relaunch and real workload evidence |
| F | Bounded software F0–F5 passes; Luna updates and active-panel integration retained | Physical acceptance; complete XP window behavior and planned extensions |
| G | G0/G1 measurement, G2 CPU SRCCOPY enabled; G3/G4a/G4b.1 actual-GL semantic prototypes/count and timing controls | Bounded G4b.2 native software acceptance; default/physical/broader GDI and dynarec/code-cache remain open |
| H | No completed DirectDraw/D3D translation | Real supported graphics APIs, application/game fixtures and GPU translation |
| I | CI NROs, diagnostics and public artifacts | Product compatibility matrix/configuration/delivery/stability/acceptance |

## F baseline to preserve

Status **PASS_SOFTWARE_PENDING_HARDWARE**; [ledger/report](F_ACCEPTANCE.md).
This is a bounded software milestone, not full Windows or completed visual shell.

| Scope | Contract / checks |
| --- | --- |
| F0 sessions | Fresh core/USER/legacy/MFC scopes; launch/exit/error/abort/return; [lifecycle](F0_LAUNCH_LOOP.md) |
| F1 filesystem | Persistent C/S/R, readonly/Unicode/CWD/profile/volume behavior; [filesystem](F1_FILESYSTEM.md) |
| F2/F3 shell | Desktop/Start/taskbar, actual explorer/history/address/sort, shared validated launch; [explorer](F3_EXPLORER.md) / [documents/args](F3_DOCUMENTS.md) |
| F3 state | Persistent bounded sort/associations and management; [preferences](F3C_PREFERENCES.md) |
| F3 tree/links | Real bounded tree, Unicode local Desktop/Start .lnk with args/CWD; [contract](F3C_SHORTCUTS_TREE.md) |
| File actions | Real create/rename/delete/properties; [provider](F_FILE_OPERATIONS.md), [UI](F_FILE_ACTIONS.md), [copy engine](F_FILE_COPY.md), [paste](F_FILE_PASTE.md), [contexts](F_CONTEXT_ACTIONS.md) |
| Native documents | Bounded real [TXT](F_TEXT_DOCUMENTS.md) and [BMP](F_IMAGE_DOCUMENTS.md); not arbitrary Notepad/Paint/image format compatibility |
| F4 workspace | One active Process, several guest windows; [workspace](F4_WORKSPACE.md), [independent input](F4B_INPUT_SERVICE.md), [persistent frontend](F4B_OUTPUT_LIFETIME.md), [retained native/guest planes](F4C_WORKSPACE_LAYERS.md) |
| F5 saving | Checked sync read/write/flush/size/truncate; [save contract](F5_FILE_SAVE.md); public save/find/view/fresh-session reopen and22 report groups |
| Luna | All five Claude updates preserved; [review and corrected PanelArea integration](LUNA_INTEGRATION_REVIEW.md) |

[F6b native actions](F6B_NATIVE_WINDOW_ACTIONS.md) add actual move/resize/min/max/
close and layout within one native tool group. Multiple native document instances,
multiple simultaneous guest EXEs and a complete XP shell remain unimplemented. File APIs/native tools retain their documented limits. PNG/JPEG,
recursive file-manager features and arbitrary Windows shell/COM are not implied.

## User-prioritized window actions

[F6a guest nonclient gestures](F6A_GUEST_WINDOW_GESTURES.md): real modal move/
eight-edge resize, min/max/restore/close release, application constraints/veto,
focus/capture/lifetime handling. [F6b native Explorer/Programs/About/TXT/BMP
actions](F6B_NATIVE_WINDOW_ACTIONS.md) add true geometry/layout/input/composition,
min/max/task restore, close policy and resize. One native tool group remains.
The existing bounded F0–F5 acceptance is preserved, not expanded into full XP.

## G implementation state

| Package | Actual result | Boundary |
| --- | --- | --- |
| G0/G1 | Bounded opcode/API census and [public real-PE phases/overhead](G1_MEASUREMENTS.md) | Measurement, not speedup |
| G2 | [32-bit CPU SRCCOPY fast path / optional executor wall phases](G2_BLIT_AND_PHASES.md) | Fast path active; physical FPS still unmeasured |
| G3 | [GL fill/copy/readback/recovery target](G3_GPU_GDI_PROTOTYPE.md) | Dedicated context, isolated bounded subset |
| G4a | [Generation-checked targets and direct texture composition](G4A_GPU_TARGETS.md) | No per-frame CPU readback merely for composition; not live HWND bindings |
| G4b.1 | [Ordered instanced fill batches](G4B_GPU_BATCHES.md), cached uniforms,42-sample same-build pixel/count/timing report | Copies/intersections preserve order; bounded journal/checkpoints retained |
| G4b.2a | [Bounded shared GL-state handoff](G4B2_SHARED_CONTEXT.md), actual EGL interleaving with production renderer | Implemented prerequisite; no live GDI activation |
| G4b.2b | [Actual HWND/DC identities, validated leases and bounded native target resources](G4B2B_WINDOW_BINDINGS.md); stale CPU DC→reused HWND rejection | Explicit native bridge; ordinary GDI remains CPU-authoritative |
| G4b.2c | [Opt-in CPU/GPU authority and real x86 read/ROP/DIB/layout barriers](G4B2C_CPU_COHERENCE.md), production shader cleanup | Native opt-in subset; ordinary GDI remains CPU reference |
| G4b.2d.1 | [Owning resident-resource frames](G4B2D_RESIDENT_FRAMES.md), retirement budgets/reset/fallback | Native manager subset; not production shell integration |
| G4b.2d.2 | [Resident-layer production render entry](G4B2D2_RESIDENT_PRESENTATION.md), masks/CPU overlay/cursor, explicit recovery | Native entry; no default GDI/ShellSession activation |
| G4b.2d.3 | [Owning mixed CPU/resident presentation](G4B2D3_MIXED_FRAME.md), independent redraw and actual software recovery | Explicit native entry; live HWND/frontend wiring remains open |
| G4b.2d.4 | [Live-window ownership into mixed frames](G4B2D4_LIVE_WINDOW_FRAMES.md), real x86/EGL output before CPU readback | Explicit coherent native entry; shell/frontend and ordinary dispatch remain open |
| G4b.2e | [Native shell mixed overlay + real USER producer selection](G4B2E_MIXED_WORKSPACE.md), GPU epochs and output-fault acknowledgement | Native selected adapter; persistent Switch device/swap and default GDI activation still open |
| G4b.2f | [Opt-in Switch mixed output device/cursor adapter](G4B2F_SWITCH_MIXED_OUTPUT.md), staged genuine recovery, teardown and v9 counters | Default off; ordinary GDI CPU, libnx physical acceptance deferred |
| G4b.2g | [Host pixel access/fault audit](G4B2G_HOST_PIXEL_AUDIT.md), guarded CPU views, sticky owner faults, checked compose/lifecycle and suspended session isolation | Registered paths audited; arbitrary future raw pointers remain caller responsibility |
| G4b.2h | [Ordinary solid GDI dispatch and paired first-frame measurements](G4B2H_ORDINARY_GDI_DISPATCH.md), sixteen actual-x86 CPU/EGL/exits/count/recovery cases | Explicit native experiment only; default CPU, one bounded workload, local GPU first-frame path slower |
| G4b.2i | [Cold/warm persistent-resource measurement + covered journal retirement](G4B2I_WARM_GDI_JOURNAL.md),12 real-x86 normal exits/108 EGL frames; capacity readbacks8→0, final real read preserved | Native experiment only; partial/pending/recovery/budget gates preserved |
| G4b.2j | [Ownership-epoch guarded workspace snapshot reuse](G4B2J_RETAINED_WORKSPACE.md),24 actual-x86 exits/216 native-shell EGL frames; tile builds9→1, partial9→6, CPU DIB9 | Native experiment only; new-owner/CPU/layout/shell/reset/failure boundaries preserved |
| G4b.2k | [Adaptive exact damage / atomic masked CPU commit](G4B2K_EXACT_DAMAGE.md),36 ordinary-x86 exits/324 EGL frames plus odd-window/fallback/recovery/capture gates | Partial mid-series commits5→0, snapshots6→1; eight journal-capacity reads remain; default CPU |
| G4b.2l | [Bounded capacity checkpoint replay](G4B2L_CAPACITY_REPLAY.md),48 ordinary-x86 exits/432 EGL frames; partial capacity reads8→0 with32768 private CPU pixel applications | Real final GetPixel/DIB reads remain; bounded CPU work/refusal, alias/GL fault/loss gates; native experiment only |
| G4b.2m | [Measured differential CPU import](G4B2M_DIFFERENTIAL_IMPORT.md),60 x86 exits/540 EGL frames; DIB upload bytes655360→66112 | Full CPU seeds and actual GetPixel/DIB reads retained; bounded scan/owned input/error/loss gates; native experiment only |
| G4b.2n | [Consolidated native software acceptance / actual default CPU-native cycles](G4B2N_SOFTWARE_ACCEPTANCE.md):17 groups/five reports;32 normal exits/288 EGL frames | DIB and size-refusal costs retained; CPU default, physical/game/full-GDI scope open |
| G4b.2o | [Explicit no-resident CPU routing](G4B2O_CPU_ROUTING.md),48 exits/432 EGL frames; actual CPU/GPU transitions and output faults | Flag2 alternative only; flag1/reference and default CPU preserved; physical/default acceptance open |
| D1 | [Real bounded registry creation/open/values/grants/close](D1_REGISTRY_KEYS.md),11 A/W APIs and four fresh x86 sessions | Old registry entry blocker removed; disk persistence/security/full encoding still open |
| D2 | [Fixed-US keyboard translation](D2_KEYBOARD_LAYOUT.md), MapVirtualKey/GetKeyNameText A/W, actual USER scan/E0/sided states and four x86 sessions | Other layouts/IME/localization remain open; Pinball passes key names |
| D3 | [Real bounded INI/profile files](D3_INI_PROFILES.md),16 A/W exports and four fresh x86 writer/reader sessions, checked file publication | ASCII/UTF16LE subset; full ACP/IniFileMapping/profile structs outside scope |
| D4a | [Owning PCM/device queries](D4A_AUDIO_OWNERSHIP.md), checked audout reset/release, bounded resource/memory PlaySound and real caps/format query | Host capture/transport models; no hardware sound or streaming waveOut acceptance |
| D4b | [Bounded waveOut stream](D4B_WAVE_STREAMING.md): real handles/prepare/write/reset/close/window-thread callbacks;7 normal exits +1 abort | Canonical PCM only,1 stream; function/event callbacks/positions/pause/mixing broader formats open |
| D4c | [Guest function/event audio callbacks](D4C_WAVE_CALLBACKS.md): real service CPU/TEB/TLS/stdcall20 and128-record queue, event type/rights/aliases;9 process terminations +1 callback abort | Canonical PCM/one client; positions/pause/broader formats/mixing/MCI remain open |
| Next independent block | Actual frame position/pause/restart | Count confirmed played frames separately from cancellation; preserve callback/lifetime/error gates; MCI separate |

Host software GL timings and exact work reductions are not Switch GPU/FPS
measurements. Capacity checkpoints and explicit CPU reads still synchronize.
Minimum ARM64 executable-memory probe previously returned42 on Switch; do not
repeat it as a prerequisite or treat it as an accepted full dynarec.

## Next package and activation gates

1. **Implemented native entry:** [live HWND ownership → mixed frame](G4B2D4_LIVE_WINDOW_FRAMES.md).
   Real x86/EGL pixels, CPU-window occlusion, original read/ROP/DIB counts and actual-loss fallback stay gates.
2. **Implemented native entry:** [ShellSession overlay + USER selection](G4B2E_MIXED_WORKSPACE.md).
   Dynamic native chrome/Start/dialogs, GPU-only epochs, no selection checkpoint and failure acknowledgement are checked.
3. **Implemented opt-in adapter:** [persistent Switch device/swap/cursor](G4B2F_SWITCH_MIXED_OUTPUT.md).
   Real host x86/EGL/recovery/order checks and Switch CI compilation; default off, not physical acceptance or ordinary GPU GDI.
4. **Implemented registered-path audit:** [host pixel/fault boundaries](G4B2G_HOST_PIXEL_AUDIT.md).
   Guarded CPU views, expired/reentrant/sticky faults, checked compose, lifecycle and suspended-session isolation are tested.
5. **Implemented bounded experiment:** [ordinary solid FillRect/PATCOPY dispatch](G4B2H_ORDINARY_GDI_DISPATCH.md), public real-x86 CPU/EGL/read/ROP/DIB/layout/lifecycle/budget and actual-loss recovery gates. Default off.
6. **Implemented paired first-frame and warm measurements:** [persistent-resource CPU/reference-GPU/retired-GPU experiment](G4B2I_WARM_GDI_JOURNAL.md), capacity readbacks8→0, real final read retained. [G4b.2j](G4B2J_RETAINED_WORKSPACE.md) measures native snapshot reuse and partial/DIB boundaries. [G4b.2k](G4B2K_EXACT_DAMAGE.md) implements adaptive exact damage and atomic masked CPU/capture exchange. [G4b.2l](G4B2L_CAPACITY_REPLAY.md) bounds private capacity replay and preserves actual application reads. [G4b.2m](G4B2M_DIFFERENTIAL_IMPORT.md) measures seeds and bounds changed-rectangle uploads. [G4b.2n](G4B2N_SOFTWARE_ACCEPTANCE.md) consolidates software gates and compares actual CPU/native routes. [G4b.2o](G4B2O_CPU_ROUTING.md) implements the explicit route alternative. **Next:** D actual frame position/pause/restart after [D1 registry](D1_REGISTRY_KEYS.md), [D2 keyboard](D2_KEYBOARD_LAYOUT.md) and [D3 INI/profile](D3_INI_PROFILES.md); retain USER/GL/CPU/alias/read/ROP/DIB/fault contracts. Default/physical acceptance remain open.
7. Continue D compatibility with public reproducers of actual API/memory/unwind
   blockers; Pinball x86 remains a separate unsupported application target.
8. Develop the measured x86→ARM64 dynarec path: real block execution, code-cache
   invalidation/lifetime, instruction/cache correctness and interpreter fallback.
9. Audit H/D3D9 for target games, then actual supported resources/shaders/commands
   and public graphics fixtures. No Direct3D/game acceptance exists today.

Steps2–6 are the immediate GPU integration sequence. Steps7–9 are larger follow-on
queues; measurements and dependencies may change their order. Every implementation
package carries its own tests, handoff/map update and exact-SHA six-job CI; testing
is not postponed until the end. Physical workload evidence remains deferred.

D compatibility is a separate queue: page protection/unwind/COM/API contracts
and concrete public reproducers. Pinball x86 is not certified by graphics work.
G4b.2 is not full x86→ARM64 dynarec or DirectDraw/D3D.

## Installer/archive extension and later product work

[Installation and archive import](F_INSTALL_AND_ARCHIVES.md) remains planned:
real supported installer payload/filesystem/API/shortcut handling, then ZIP,
RAR/other formats later. Existing ProgramFiles/profile paths and native links
are infrastructure, not universal installer/child-process/MSI/COM acceptance.
Native window ergonomics/Luna polish, multi-Process, broader images and product
compatibility/configuration remain visible future work.

## Validation and working agreement

Preserve interpreter/Win32 correctness, CPU GDI/ROP/readback, software fallback,
native1280x720, L legacy and ZL software overrides, one persistent frontend/output
and fresh guest sessions. No borrowed guest/window pointer may outlive synchronous
submission or continuation draining. Keep user binaries/private logs outside git.

Before edits fetch/check HEAD/tree and handoff; never reset newer work. Each code
package runs the full COMPOSITION_GL_TEST=1 host/MinGW/Mesa suite plus relevant
gates, updates handoff/map, normal-pushes only claude/wonderful-hawking-10mpr0
and verifies all six CI jobs for the exact published SHA. Documentation-only
packages use source/link/diff checks and still report exact-SHA CI honestly.
Local devkitA64/libnx is absent; CI builds NROs. Current hardware/Paint/GPU/
fallback/input/relaunch remains the deferred [consolidated list](STAGE_E_PERFORMANCE.md).
SD recovery is closed; no repeated SD diagnosis or invented physical acceptance.

CI setup note (2026-10-26): ARM cross-tool, host MinGW/Mesa and CPU-oracle multilib dependencies use the official Ubuntu
HTTPS archive with bounded setup/network timeouts after measured Azure HTTP
payload throttling. Test commands and the six core gates are unchanged.

F6b final5f72b67 passed CI40390490786 six jobs, including the GCC13 caption-lifetime
fix. The earlier eb67ac3 host failure is not acceptance for that earlier SHA.
