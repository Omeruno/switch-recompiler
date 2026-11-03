# Active implementation map — 2026-11-03

[Визуальная карта проекта](https://omeruno.github.io/switch-recompiler/) · [Автономный HTML](roadmap/index.html) · [Как открыть и публиковать](roadmap/README.md)

**Current position: [G12 mixed CPU coverage/fault boundaries](G12_MIXED_CPU_COVERAGE.md):
optional engine census,five PE workloads,on/off controls,68 precise fallback sessions
and paired reports. Native is slower on mixed QEMU cases;G5 default. User deferred
G10 actual Switch testing;all outstanding physical checks DEFERRED_BY_USER.**
Incoming verified runtime `b653d950f335e60a2ddd5acd00f21b113d2c03a2`
passed [CI41608994958,all six jobs](https://github.com/Omeruno/switch-recompiler/actions/runs/41608994958).
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
| D | PE/DLL/TLS/unload/rollback, bounded SEH, substantial Win32/CRT | Page-right enforcement, full unwind/COM/API compatibility; D1–D4d bounded playback controls/PCM/MP3/MCI implemented; mixing/DirectSound/MIDI and game audio remain open |
| E | CPU GDI reference, production Switch GL composition/fallback, retained windows/ownership/revisions, E8/E9 metrics/public oracles | Current physical Paint/GPU/fallback/Lite/input/relaunch and real workload evidence |
| F | Bounded software F0–F5 passes; Luna updates and active-panel integration retained | Physical acceptance; complete XP window behavior and planned extensions |
| G | G5 common CPU fast path;G6 IR/G7 storage/G8 emitter/G9 cache/G10 batches/[G11 native LEA](G11_NATIVE_ADDRESS.md),G12 mixed coverage/fault boundaries;paired PE measurements;G2 CPU copies and bounded native GL software acceptance | Native guest data/stack/calls,default activation,physical performance,broader GPU GDI and game APIs remain open |
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
| Native documents | Bounded real [TXT](F_TEXT_DOCUMENTS.md), [BMP](F_IMAGE_DOCUMENTS.md) and [MP3/WAV Media Player](D4D_AUDIO_MEDIA.md); not arbitrary Notepad/Paint/image format compatibility |
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

[F6c media UI](F6C_MEDIA_VISUALS.md) adds XP-era player chrome, real playback-cursor PCM
spectrum/wave/circles/VU, authored music icons and timed path-safe Explorer/program
double-click; single selection/controller Open remain. One native tool group.

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
| D4b | [Bounded waveOut stream](D4B_WAVE_STREAMING.md): real handles/prepare/write/reset/close/window-thread callbacks;7 normal exits +1 abort | D4b package scope extended by D4c/D4d; one physical source, mixing/DirectSound/MIDI remain open |
| D4c | [Guest function/event audio callbacks](D4C_WAVE_CALLBACKS.md): real service CPU/TEB/TLS/stdcall20 and128-record queue, event type/rights/aliases;9 process terminations +1 callback abort | D4d adds controls/formats/MP3/MCI; one physical source, mixing/DirectSound/MIDI remain open |
| D4d | [Playback controls/MP3/MCI/native Media Player](D4D_AUDIO_MEDIA.md): actual device position/pause/master volume, PCM8/16 mono/stereo8–96k, native MP3 decoder, shared engine and real shell controls | One source, compressed/input snapshot32MiB, no MIDI/DirectSound/mixing/video or physical sound acceptance |
| D5 | [Real legacy file I/O](D5_LEGACY_FILE.md):9 stdcall APIs, checked OFSTRUCT/search/reopen, shared transfers/truncation and public x86 sessions | Regular files/CP1252/bounded search; deny locks/VERIFY/PROMPT/extra attributes unsupported; no Pinball acceptance |
| D4e | [Native mixer core](D4E_AUDIO_MIXER.md):8 sources,owning bounded PCM,actual played-span credits,independent gain/pause/reset/close and real audout adapter path | Explicit native core only; existing WinMM/MCI/MediaPlayer stay exclusive; no physical/game audio acceptance |
| D4f | [Real mixed waveOut](D4F_WAVE_MIXER.md):8 source handles,mapper activation with direct fallback,per-stream PCM/header/position/gain/pause/reset/close and actual x86 callbacks/fault/session tests | MCI/MediaPlayer/PlaySound still exclusive; logical routes share one physical output; no hardware sound/game acceptance |
| D6 | [TrueColor palette/string/message/clock contracts](D6_PALETTE_STRINGS.md):shared A/W resources/registered messages,timeGetTime,checked PE spans and actual new/legacy x86 evidence | First private new-UI host table frame only; legacy FindWindowA was the D6 boundary; D7 reaches LoadIconA; interaction/sound/performance/physical acceptance open |
| D7 | [Shared real-window lookup](D7_WINDOW_LOOKUP.md):FindWindow/FindWindowEx A/W,actual new/legacy title/class/atom/direct-child/after and4 x86 sessions + parked abort | ASCII case-fold profile; legacy LoadIconA,game interaction/audio/performance/physical acceptance open |
| D8 | [Public game-loop oracle and host key scripts](D8_GAME_LOOP.md):4 normal x86 sessions +2 held-key aborts,actual keys/chars/timers/moving/held/released pixels and owning output | Bounded public loop,not Pinball/game/FPS/physical acceptance; host scripting is not Switch rebinding |
| D9 | [Owning class menus](D9_CLASS_MENUS.md):actual resource loading before callbacks,per-window trees,checked templates/lifetime,4 x86 sessions+2 callback aborts | Private host ball launch/movement/score observed; full game/flipper/audio/FPS/Switch remain open |
| F7 | [Controller keyboard binding settings](F7_CONTROLLER_BINDINGS.md):12-button global profile,real editor/apply/save/reset,v1→2 migration,duplicate/held-context ownership and actual mapped x86 loop | New USER/shell only;R/Plus/mouse reserved;physical/USB/layout/per-EXE profiles outside acceptance |
| D10 | [Audio recovery/MMIO](D10_AUDIO_RECOVERY.md):correct audout release ordering,indexed MP3 seeks,real sndPlaySound A/W/read-only file MMIO/RIFF and4 x86 exits+2 owning aborts | Physical seek/black-screen recovery remains unverified; no full MMIO/game acceptance |
| F8/G2 indexed | [Live window view/preset/indexed copies](F8_GAME_VIEW_AND_INDEXED_BLITS.md):real Start/R mode,absolute input/offset cursor,touch,preset Apply/Save,4 x86 exits+2 aborts;2800 raster cases/paired native measurements | Physical FPS/view/input open;view is presentation,not virtual display-mode/maximize support |
| D11 | [Old PE/Doom95 audit](D11_OLD_PE_AND_DOOM95.md):raw-backed VirtualSize0/header imports,bounded records/strings,IAT/BSS/map guards;8 x86 exits+2 aborts with relocated ordinal DLL | Doom95 splash/launcher only;actual launcher boundary and DirectDraw/DirectSound/DirectPlay remain open |
| D12 | [Launcher contract](D12_LAUNCHER_CONTRACT.md):checked empty A/W+Ex paths,virtual UserName,unsupported DirectPlay names/ordinals with correct ABI;4 normal x86 sessions+2 aborts | Composed launcher visible;New Game stops at RegCreateKeyA;configuration/Common Controls/game/DirectX remain open |
| D13 | [Legacy registry create](D13_LEGACY_REGISTRY_CREATE.md):real RegCreateKey A/W,stdcall12,checked outputs,no Ex-only stack reads;4 normal x86 sessions+2 aborts and independent store bytes | New Game reaches missing MIDI device enumeration;game/DirectX remain open |
| G5 | [Common CPU performance](G5_CPU_INTEGER_PERFORMANCE.md):register/mapped/stack path,214323 differential cases,ARM64/QEMU,native CPU dual-path oracle;paired PE reports and Switch reference/census flags | Host-only ratios;full dynarec/physical performance/GPU game APIs remain open |
| G6 | [Owned block IR](G6_CPU_BLOCK_IR.md):32 nodes/192 bytes,portable semantics/budgets/checkpoints/current-byte validation/owner ABA;68865 cases+4 PE exits/2 HLE aborts | Production G5;G7 storage/G8 emission implemented;G9 cache/G10 batching now implemented;hardware performance remains open |
| G7 | [Executable storage](G7_EXECUTABLE_STORAGE.md):session ownership,immutable publication/cache protocol,8192 native calls,pinned suspension/retirement,bounded failed-release vault;ARM64/QEMU and full-runtime session tests | Preserve exact CPU/fault/TEB/callback/quantum/stop and renderer/ownership contracts;Doom95 MIDI and DirectX queues retained |
| G8 | [Actual ARM64 emitter](G8_ARM64_EMITTER.md):52437 native/reference/IR cases,198371 translated nodes,ABI/budgets/raw flags and parked checkpoint lifetime | G9 cache/G10 batching implemented;default activation and physical performance remain open |
| G9 | [Common native step cache](G9_NATIVE_DISPATCH.md):session-scoped16-slot/8-visit cache,current source validation,one-step integration,real ARM64 PE/DLL/callback/thread parity and7 paired samples | Experimental cpu-native.flag only;G9 one-step QEMU slower;G10 batching follows,G5 default;activation/physical performance open |
| G10 | [Budgeted native batches](G10_NATIVE_BATCHES.md):exact instruction/quantum/stop accounting,8 normal+6 abort PE sessions,80 traces,19 shared-validator samples | G5 default;actual Switch CPU report pending;broader native/default activation open |
| G11 | [Native LEA](G11_NATIVE_ADDRESS.md):all legal32-bit ModRM/SIB,owning IR/direct ARM64,152304 differential cases,76536 actual native comparisons,four PE modes/paired report | Pure address arithmetic;guest memory/stack/calls and physical/default activation remain open |
| G12 | [Mixed measurements/fault boundaries](G12_MIXED_CPU_COVERAGE.md):optional6x513 engine census,five PE cases/80 paired rows,17 boundary cases/68 sessions,explicit low-first reads | No native data accesses yet;G5 default;all outstanding hardware deferred;next checked MOV load/dynamic prefix |

Host software GL timings and exact work reductions are not Switch GPU/FPS
measurements. Capacity checkpoints and explicit CPU reads still synchronize.
Minimum ARM64 executable-memory probe previously returned42 on Switch; do not
repeat it as a prerequisite or treat it as an accepted full dynarec.

G8 follow-up: first6a49055 CI41342882076 was5/6 (D6 host DLL/string assertion).
Reproduced non-relocatable fixture/HLE-base collision;public resource DLL bases
are now deterministic and failure diagnostics split. Production loader unchanged;
revised b110fcc passed all six jobs in CI41344237545. See [G8](G8_ARM64_EMITTER.md).



## Next package and activation gates

Performance is the user's current priority(2026-11-01). [G5's implementation queue](G5_CPU_INTEGER_PERFORMANCE.md)
supersedes the previous next-MIDI and immediate GPU-sequence wording.

1. **Implemented G5:** current-byte register/in-page mapped interpreter paths,
   reference mode,paired public PE measurement and opt-in console diagnostics.
2. **Implemented G6:** bounded block/IR frontend,portable semantic oracle,exact
   budgets/source/owner/checkpoint exits and test-only one-instruction PE adapter.
   G10 now supplies guarded outer-thread batching; nested callbacks stay Step1.
3. **Implemented G7:** session-owned libnx/Linux executable storage,immutable publication/cache protocol,
   bounded pinned retirement and failed-close retention. Libnx alias mode is not physical W^X.
4. **Implemented G8:** actual bounded ARM64 emitter from G6 IR,exact raw flags/
   register/branch parity,ABI/checkpoint/lifetime/accounting and reference boundaries.
5. **Implemented G9:** guarded session cache/common Interpreter::Step with opt-in,
   reference priority,current bytes/owner validation,one-shot failure fallback,
   real ARM64 PE/callback/worker+abort parity and paired measurements. One step
   remains one instruction. Native is slower in local QEMU;G5 stays default.
   **Implemented G10:** exact-budget native batches,remaining-quantum charge,
   nested Step1,80 traces and paired public PE measurements.
6. **Implemented G11:** measured LEA boundary and added direct native address arithmetic,
   complete encoding/state/budget checks and paired real PE evidence.
   **Implemented G12:** five mixed workloads,optional engine census/on-off controls,
   precise native-prefix/data/stack/control fault boundaries and low-first reads.
   **Next G13:** checked32-bit MOV loads,dynamic completed-prefix ABI and exact
   refusal/fault accounting;stores/stack/CALL and broader policies follow.
   **Hardware:** user explicitly deferred G10 on2026-11-03. All outstanding physical
   checks deferred;continue software work without requesting a report or activation.
7. Extend resident GPU GDI/batches by measured workload needs and existing
   CPU/ROP/readback/DIB/alias/context-loss/ownership/fallback oracles;default CPU.
8. Real DirectDraw/D3D resources/shaders/commands for the selected target versions.
9. Retained compatibility queues: Doom95 MIDI discovery,media-bus/DirectSound/MIDI,
   legacy icon/resources,virtual display,installers/archives and broader system APIs.

Each package updates handoff/map and passes full host/MinGW/Mesa,exact-SHA six-job
CI,public artifacts and source identity. Software timings are not Switch FPS;
full hardware/application input/audio/stability acceptance remains deferred.

## Installer/archive extension and later product work

[Installation and archive import](F_INSTALL_AND_ARCHIVES.md) remains planned:
real supported installer payload/filesystem/API/shortcut handling, then ZIP,
RAR/other formats later. Existing ProgramFiles/profile paths and native links
are infrastructure, not universal installer/child-process/MSI/COM acceptance.
F6c adds [media visuals/music icons/double-click](F6C_MEDIA_VISUALS.md). Further native window ergonomics/Luna polish, multi-Process, broader images and product
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

## Deferred hardware and current software queue

The user deferred the G10 Switch test on2026-11-03. Its published exact-SHA
package remains available for later;no physical acceptance is inferred. G12
continues independent software coverage/fault work. Next is G13 checked MOV
loads with a dynamic prefix/fault contract. Default native activation still
requires separate evidence;all outstanding physical checks remain deferred.

G10 packaging follow-up: CI41355944529 at4175be9 compiled the Switch NRO,
but the new ZIP step failed Git's container ownership check before packaging.
The packager now scopes `git -c safe.directory=<this checkout>` to its SHA query;
no global trust exception or runtime change. The completed local full host/
MinGW/Mesa runner passed. Physical CPU acceptance remains pending; publish only
the revised exact-SHA bundle after all six CI jobs and binary/fixture audit.

G11 local verification completed: full COMPOSITION_GL_TEST=1 host/MinGW/Mesa,
22 F groups and17 GPU groups/five reports;fresh full ARM64/QEMU PE/callback/
abort/batch/shared-validator and new LEA checks,strict/sanitizer and map/browser
checks pass. Publish normally,then verify exact-SHA six-job CI and artifact identity.
