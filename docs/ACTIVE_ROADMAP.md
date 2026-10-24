# Active implementation map — 2026-10-24

Current user priority: develop a real usable system before further visual polish.
F1/F2/F3a/F3b and F4a are implemented. **F4b.1 now services native input between
executor quanta independently of a guest USER pump**, with bounded deferred input
and asynchronous owner-window commands. **F4b.2 now keeps one frontend worker
and output device across guest sessions.** Shared file mutations/properties now exist in the provider/browser model;
[Native file actions](F_FILE_ACTIONS.md), name entry, confirmation and properties
are now connected. [Checked byte copy](F_FILE_COPY.md) is shared by VFS, Win32 and
the browser model. [Native Copy/Paste](F_FILE_PASTE.md) now runs in bounded work
steps with real byte progress and cancellation. Next: context actions,
coherent Switch input, TXT/images and the F5 workflow.
Luna visual refinement and G optimization are deferred. See the
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
| F3b | [Document applications and arguments](F3_DOCUMENTS.md), Open/Choose application, remembered session types, full A/W/CRT argument transport and public x86 reader | On-disk preferences/association management remain F3c; no default or verified Paint association |
| F4a | [Shared active workspace](F4_WORKSPACE.md), new UI default including Paint, native backdrop/Start/taskbar, one-task minimize/restore and work area/input routing | Physical acceptance; flat native overlay composition still needs F4b retained-layer integration and visual/interaction refinement |
| F4b.1 | [Independent native input](F4B_INPUT_SERVICE.md), bounded deferred guest events, physical async state, real owner-queue task commands and pending feedback | Physical acceptance; a host API that never yields remains a responsiveness limit |
| F4b.2 | [Persistent frontend/output](F4B_OUTPUT_LIFETIME.md), one large-stack owner, shared native/guest presenter, real desktop return and scoped metrics | Physical acceptance; exact 975f393 CI passed six jobs; explicit mode/failure transitions can recreate the device; native/guest retained planes remain open |
| **Functional F: in progress** | [Shared real file operations/properties](F_FILE_OPERATIONS.md) in VFS, Win32/CRT and browser model; [native create/rename/delete confirmation/properties UI](F_FILE_ACTIONS.md); [checked byte-copy provider](F_FILE_COPY.md); [native Copy/Paste](F_FILE_PASTE.md) with progress/cancel; persistent shell/session/input/output | Input/context menus → preferences/TXT/images → save/find/reopen |
| Planned F extension | [Installer and archive plan](F_INSTALL_AND_ARCHIVES.md) recorded from user request | Supported EXE installation to persistent folders, actual desktop/Start shortcuts, ZIP then RAR/other formats; compatibility and extraction gates remain open |
| G0 | Bounded opt-in opcode/host-entry census and public fixture | Representative PE timing/overhead and optimization deferred behind the F milestone; census is not CPU speedup |
| H/I | Diagnostics/build artifacts exist; no completed DirectDraw/D3D/product release | Separate graphics API translation, installation/config/compatibility matrix/release work |

## Next concrete package: native context and pointer/controller actions

F1 decouples persistent C from EXE directory/cwd, exposes real SD as S and
read-only ROMFS as R, and keeps ordinary host launches compatible by default.
See [F1_FILESYSTEM.md](F1_FILESYSTEM.md). Existing files are not moved; old saved
absolute C paths from the former per-EXE mapping may need reselection through S.

F4b.1 removes dependence on the guest message pump for native Start/folder input
at executor boundaries. It preserves real guest command delivery and explicitly
shows pending requests instead of fabricating a minimized window. F4b.2 now separates frontend/device lifetime from guest lifetime; see
[F4B_OUTPUT_LIFETIME.md](F4B_OUTPUT_LIFETIME.md). Native file actions and checked-copy model are connected; native Copy/Paste is now connected; context interaction and TXT/image tools remain open.

The common real create/rename/delete/properties provider and browser model are
implemented; see [the exact scope](F_FILE_OPERATIONS.md). DesktopShell now exposes [native name/actions/confirmation/properties](F_FILE_ACTIONS.md)
with controller focus and modal input ownership. The [checked-copy provider/model](F_FILE_COPY.md) now checks data I/O/fsync/close
and stages publication without truncating an old destination. Its device rename/
race/cleanup limits are explicit. The [native single-file Copy/Paste flow](F_FILE_PASTE.md) now remembers a source,
accepts a destination name, services a resumable job and supports cancellation.
Next implement coherent right-click/context/controller actions. Retained native/guest GPU planes remain
an explicit graphics integration boundary. Implement
persisted associations/settings and actual document tools before further Luna
polish. F3/F4/F5 and general Win32 compatibility remain incomplete.

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
2. Preserve F2/F3a/F3b shell/browser/document checks. Preserve F4b.1/F4b.2 input/output ownership. Implement real file operations and input,
   F3c preferences/settings/tree and TXT/image workflows.
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
