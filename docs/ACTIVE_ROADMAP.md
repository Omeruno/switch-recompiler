# Active implementation map — 2026-10-25

Whole-F baseline: **PASS_SOFTWARE_PENDING_HARDWARE**; [acceptance ledger/report](F_ACCEPTANCE.md). Programmatic F0–F5 is implemented and passes the full suite; physical acceptance and the planned extension remain separate.

User-facing global status: [Где находится проект](PROJECT_STATUS_RU.md).

Current user priority: develop a real usable system before further visual polish.
F1/F2/F3a/F3b and F4a are implemented. **F4b.1 now services native input between
executor quanta independently of a guest USER pump**, with bounded deferred input
and asynchronous owner-window commands. **F4b.2 now keeps one frontend worker
and output device across guest sessions.** Shared file mutations/properties now exist in the provider/browser model;
[Native file actions](F_FILE_ACTIONS.md), name entry, confirmation and properties
are now connected. [Checked byte copy](F_FILE_COPY.md) is shared by VFS, Win32 and
the browser model. [Native Copy/Paste](F_FILE_PASTE.md) now runs in bounded work
steps with real byte progress and cancellation. [Explorer context menus](F_CONTEXT_ACTIONS.md)
now route object/current-folder actions through pointer, Menu key and controller
focus. [Persisted sorting/associations and management](F3C_PREFERENCES.md) are now
implemented as a bounded F3c slice. [Bounded native TXT read/edit/save](F_TEXT_DOCUMENTS.md) is now implemented; [bounded native BMP viewing](F_IMAGE_DOCUMENTS.md) and a public x86 GDI/save/browse/reopen slice are now implemented; the remaining baseline tree/shortcuts and retained planes are now implemented. Exact-SHA F CI passed six jobs at `50ec505` (run 40139298651).
Current stage is G: [G1 public PE phase/overhead measurement](G1_MEASUREMENTS.md)
and [G2 measured SRCCOPY/optional executor phases](G2_BLIT_AND_PHASES.md),
following the user's Paint/large-Minesweeper slowdown report. GPU output already
exists; canonical GDI is still CPU rasterized. Measured GDI/GPU and interpreter
work precedes speedup claims; Luna visual fidelity remains open. See the
[F4b.1 contract](F4B_INPUT_SERVICE.md) and [functional shell plan](F_SHELL_PLAN.md).
AI_HANDOFF.md and this map supersede historical next-E1/next-G notes.

| Block | Implemented / accepted | Still required |
| --- | --- | --- |
| A/B | CPU oracle/regressions, cooperative guest threads/TEB/TLS/native continuations | Continuous correctness; bounded HLE response and compatibility extensions |
| C | Limited one-Process HANDLE/wait/APC/timer/lifecycle scope accepted on Switch | Full NT/cross-process/security/AttachThreadInput are outside that acceptance |
| D | PE/DLL/TLS/unload/rollback, limited SEH and substantial Win32/CRT | Filesystem contracts implemented in F1; page protection enforcement and full unwind/COM/API compatibility remain separate |
| E0–E4 | Backend/software/dirty/fallible resources/lifetime; accepted Stage E milestone 60% | Preserve regressions |
| E5–E7 | Switch GL/fallback, retained layers, ownership, revision caches and 28-scene CPU/GL oracle | Current Paint/output/input/fallback hardware acceptance and console workload evidence |
| E8/E9 | CPU/GPU/bytes/memory/input-correlation metrics, six software groups and public EXE/report | Console measurements, total process/driver memory, physical latency, GPU/fallback and real Paint |
| F0 | Picker, core/USER/legacy/MFC session ownership and production launch/exit/error/return loop | Physical picker/relaunch acceptance; **not the desktop/Start/explorer milestone** |
| F1 | Persistent C/S/R, per-drive cwd, mapped module/command-line/profile paths, readonly mutations, real statvfs space and Unicode W dialogs; [contract/tests](F1_FILESYSTEM.md) | Physical SD/Paint compatibility; no automatic migration of old absolute C paths |
| F2 | [Native idle desktop/theme](F2_DESKTOP.md), Start Programs/About/Exit, bounded EXE panel, licensed Tango art, device clock, retained selection and Start-dismiss focus restoration | Physical shell/input/launch-return checks; F3a now enables My Computer/Documents and path-only Run |
| F3a | [Real directory explorer](F3_EXPLORER.md), C/S/R and documents, bounded listing/history/case lookup, back/up/address/refresh/sort/pages and shared EXE launch from catalog/Explorer/path-only Run | Physical keyboard/navigation checks; F3b now adds explicit document applications |
| F3b | [Document applications and arguments](F3_DOCUMENTS.md), Open/Choose application, document type mappings, full A/W/CRT argument transport and public x86 reader | On-disk sorting/association management now implemented in [F3c slice](F3C_PREFERENCES.md); no default or verified Paint association |
| F3c baseline | [Persistent sorting/associations](F3C_PREFERENCES.md), checked dual-slot records, native reassign/forget/retry and fresh-shell x86 document opening | [Tree and Desktop/Start links implemented](F3C_SHORTCUTS_TREE.md); arbitrary Windows settings/COM are outside baseline; physical checks pending |
| F4a | [Shared active workspace](F4_WORKSPACE.md), new UI default including Paint, native backdrop/Start/taskbar, one-task minimize/restore and work area/input routing | [Retained native/guest integration](F4C_WORKSPACE_LAYERS.md) implemented; physical acceptance pending; further theme/input ergonomics are future refinement |
| F4b.1 | [Independent native input](F4B_INPUT_SERVICE.md), bounded deferred guest events, physical async state, real owner-queue task commands and pending feedback | Physical acceptance; a host API that never yields remains a responsiveness limit |
| F4b.2 | [Persistent frontend/output](F4B_OUTPUT_LIFETIME.md), one large-stack owner, shared native/guest presenter, real desktop return and scoped metrics | Physical acceptance; exact 975f393 CI passed six jobs; explicit mode/failure transitions can recreate the device; [native/guest retained planes](F4C_WORKSPACE_LAYERS.md) now integrated |
| **Functional F: software baseline complete** | [Shared real file operations/properties](F_FILE_OPERATIONS.md) in VFS, Win32/CRT and browser model; [native create/rename/delete confirmation/properties UI](F_FILE_ACTIONS.md); [checked byte-copy provider](F_FILE_COPY.md); [native Copy/Paste](F_FILE_PASTE.md) with progress/cancel; [native object/folder context menus](F_CONTEXT_ACTIONS.md); [persistent sort/association management](F3C_PREFERENCES.md); [bounded native TXT](F_TEXT_DOCUMENTS.md); persistent shell/session/input/output | [22-group integrated acceptance](F_ACCEPTANCE.md); physical acceptance pending, extension below still planned |
| Planned F extension | [Installer and archive plan](F_INSTALL_AND_ARCHIVES.md) recorded from user request | Installer compatibility/integration with existing real desktop/Start shortcuts, ZIP then RAR/other formats; installation/extraction gates remain open |
| G0/G1/G2 | Bounded opt-in opcode/host-entry census; [public actual-PE phase/overhead experiment](G1_MEASUREMENTS.md), three workloads/four modes, checked full pixels and reports; [G2](G2_BLIT_AND_PHASES.md) enables a bounded 32-bit SRCCOPY row path and optional console quantum-wall report, same-build reference/fast comparison | Physical console attribution remains deferred; coherent GPU GDI blit/fill/readback contract and actual EGL prototype; census is not speedup |
| H/I | Diagnostics/build artifacts exist; no completed DirectDraw/D3D/product release | Separate graphics API translation, installation/config/compatibility matrix/release work |

## Current boundary: software F complete, physical F pending

F1 decouples persistent C from EXE directory/cwd, exposes real SD as S and
read-only ROMFS as R, and keeps ordinary host launches compatible by default.
See [F1_FILESYSTEM.md](F1_FILESYSTEM.md). Existing files are not moved; old saved
absolute C paths from the former per-EXE mapping may need reselection through S.

F4b.1 removes dependence on the guest message pump for native Start/folder input
at executor boundaries. It preserves real guest command delivery and explicitly
shows pending requests instead of fabricating a minimized window. F4b.2 now separates frontend/device lifetime from guest lifetime; see
[F4B_OUTPUT_LIFETIME.md](F4B_OUTPUT_LIFETIME.md). Native file actions and checked-copy model are connected; native Copy/Paste is now connected; object/folder context interaction is now connected; persistent sorting/associations and bounded native TXT are implemented; bounded native BMP is implemented; PNG/JPEG and broader image support remain open.

The common real create/rename/delete/properties provider and browser model are
implemented; see [the exact scope](F_FILE_OPERATIONS.md). DesktopShell now exposes [native name/actions/confirmation/properties](F_FILE_ACTIONS.md)
with controller focus and modal input ownership. The [checked-copy provider/model](F_FILE_COPY.md) now checks data I/O/fsync/close
and stages publication without truncating an old destination. Its device rename/
race/cleanup limits are explicit. The [native single-file Copy/Paste flow](F_FILE_PASTE.md) now remembers a source,
accepts a destination name, services a resumable job and supports cancellation.
[Native object/folder menus](F_CONTEXT_ACTIONS.md) now implement right-click,
Menu-key and controller access with captured-target validation and paired input
ownership. The [F3c preference slice](F3C_PREFERENCES.md) now stores real sort/association
records in two checked slots and provides native management with restart/error
checks. The [bounded native TXT tool](F_TEXT_DOCUMENTS.md) now supports actual create/read/line-edit/save/Save As, retained backups and conflict/error outcomes. [Native BMP viewing](F_IMAGE_DOCUMENTS.md) and a public x86 GDI/save/browse/reopen slice now exist. F5 file-save correctness and integrated evidence are now implemented; see F_ACCEPTANCE.md. Retained native/guest planes and canonical fallback are integrated and checked. Preserve these regressions while moving to D compatibility and representative G measurements; physical acceptance remains separate. F3/F4/F5 baseline is implemented programmatically; physical F and general Win32 compatibility remain incomplete.

Подробная [очередь действий после F1](F_SHELL_PLAN.md#рабочий-план-после-f1-2026-10-23)
фиксирует критерии F2–F5, возврат к измерениям G и контрольные точки активной работы.

## Planned installation and archives

After the functional F5 baseline, add [supported EXE installers and archive import](F_INSTALL_AND_ARCHIVES.md):
real persistent application folders and desktop/Start shortcuts, then ZIP/RAR/other
formats with explicit supported subsets and real extraction errors. Installer child
process/COM/MSI dependencies and shortcut APIs need actual implementations; this
is not universal installer/game compatibility. This request is recorded without
replacing the current file/document queue.

## Implementation order and gates

1. Preserve F1 mount/path/readonly/CWD/volume and cross-session x86 regressions;
   existing standalone host launches keep the legacy one-C-per-EXE default.
2. Preserve F2/F3a/F3b shell/browser/document checks, F4b.1/F4b.2 input/output
   ownership and the completed file operations, F3c tree/links/preferences,
   TXT/BMP and F5 checked-save workflows.
3. F4/F5: shell/guest output and input ownership, active task handling in the
   one-Process model, return after errors/abort, Paint save/find/reopen workflow.
   Idle shell screenshots alone do not certify taskbar integration while a guest runs.
4. Keep D compatibility gaps explicit. Mapped-page protection metadata currently
   does not enforce all access rights; fix separately with privileged host writes
   and precise guest faults. Do not require full NT before native shell work.
5. Preserve G0. Resume PE phase timing/overhead and choose a small fast path from
   evidence after the shell milestone, or for a measured blocker. Dynarec/cache/
   icache/fault fallback and H DirectDraw/D3D remain separate programs.
6. Keep physical E8/E9/Paint/GPU/fallback/Lite input/relaunch checks consolidated
   and deferred in STAGE_E_PERFORMANCE.md. SD troubleshooting is closed.

Every code package updates AI_HANDOFF.md and this map, runs the complete
COMPOSITION_GL_TEST=1 host/MinGW/Mesa suite and applicable gates, publishes only
to claude/wonderful-hawking-10mpr0 without force, and checks all six jobs for the
exact published SHA. Documentation-only packages use source/link/diff checks;
CI status must still be reported against their exact SHA without invented passes.


[F5 synchronous file-save APIs](F5_FILE_SAVE.md) now check read/write/flush/fsync/size/truncate errors and HANDLE/access/alias behavior. The tree/shortcuts, retained workspace planes and integrated 22-group acceptance are implemented; only the software baseline is closed. Physical gates are still pending.


## Active next stage: measured performance after software F

G1 adds public actual-x86 CPU/tile/stroke workloads with independent pixel/result
checks, startup/execution/teardown wall phases and repeated census/raster overhead
controls. No production rendering policy changed; this is not GPU GDI or console
FPS acceptance. CI publishes exact-revision source/fixture/runner identities and
raw samples. Next: console opt-in attribution, then a measured interpreter or
blit/fill candidate, with a coherent GPU/CPU readback contract before GPU GDI.
Keep CPU pixel/ROP, retained layers and software fallback as regression oracles.
User-observed visual distance from XP remains open Luna refinement. Pinball x86's
first host failure is missing RegCreateKeyExA, separate D work; no compatibility
claim or user binary is committed.


G2 continues without asking the user for hardware tests: default bounded 32-bit
SRCCOPY now skips general scaling/format/ROP work while preserving capture and
reference alpha normalization. A native 1,600-case differential oracle and
same-build public PE reference/fast/phase comparison cover the change. Optional
production report v8 wraps outer RunOne/native service/idle sleep, never parked
HLE frames or each opcode; it stays disabled without an explicit flag.
See G2_BLIT_AND_PHASES.md. Next independent package: coherent GPU-GDI command/
target/readback contract and real-EGL CPU comparison prototype. No Switch FPS
claim or deferred hardware acceptance is added; Luna/D/installer work stays open.


Claude's five Luna commits through466bcfd are retained and reviewed.
[Luna integration review](LUNA_INTEGRATION_REVIEW.md) records their scope,
remaining decorative caption controls and the corrected active-workspace panel
geometry/input/ownership mismatch. New shared PanelArea is checked against real
x86 guest overlap and independent full-panel pixels, including actual EGL. G2
exact CI6/6 and clean comparison artifact verified; G3 prototype remains next.
No physical tests requested from the user.


G3 [isolated GL GDI target/readback/recovery prototype](G3_GPU_GDI_PROTOTYPE.md)
continues after the reviewed Luna integration (b7cb82d exact CI6/6). Real shader
fill/copy and bounded owned command/recovery storage are checked against CPU
pixels with actual EGL, including context destruction. It is not connected to
production GDI/presentation and grants no Switch FPS/physical acceptance.
Next G4: multi-target/batching/CPU-read and raw-DIB barriers plus texture-aware
presentation before activation. User hardware tests remain deferred.


G4a [multiple GPU targets/direct texture composition](G4A_GPU_TARGETS.md) adds
four generation-checked native target IDs, resource budgets and actual-GL scene
composition without per-frame CPU readback/reupload, plus explicit read/copy
barriers and manager-wide fallback. It remains isolated from live USER/GDI and
the production presenter. Next G4b: bounded batching, HWND/raw-DIB/ROP coherence
and the real presenter bridge; no physical test request or Paint FPS claim.
