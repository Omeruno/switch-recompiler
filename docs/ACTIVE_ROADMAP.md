# Active implementation map — 2026-10-24

Current user priority: complete the supported XP-like shell and bind its files
to actual Switch SD storage. F1 persistent roots/filesystem contracts are implemented; F2 native idle desktop/theme and F3a real directory navigation/path launch are implemented; F3b document applications/arguments are implemented; the latest user priority moves active-guest integration ahead of F3c. **F4a implements the shared native workspace and new UI default for Paint/all EXEs**; next is **F4b retained shell/window composition and interaction refinement**, followed by F3c persisted preferences/settings, before further G optimization. See the source-backed
[audit](PROJECT_AUDIT_2026_10_23.md) and [F1–F5 shell plan](F_SHELL_PLAN.md).
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
| **F4b/F3c/F5: next** | Persistent filesystem, shell/browser/document launch and one active guest | Retained active shell layers/refinement → persisted settings/preferences/tree → save/find/reopen |
| G0 | Bounded opt-in opcode/host-entry census and public fixture | Representative PE timing/overhead and optimization deferred behind the F milestone; census is not CPU speedup |
| H/I | Diagnostics/build artifacts exist; no completed DirectDraw/D3D/product release | Separate graphics API translation, installation/config/compatibility matrix/release work |

## Next concrete package: F4b retained workspace layers and interaction refinement

F1 decouples persistent C from EXE directory/cwd, exposes real SD as S and
read-only ROMFS as R, and keeps ordinary host launches compatible by default.
See [F1_FILESYSTEM.md](F1_FILESYSTEM.md). Existing files are not moved; old saved
absolute C paths from the former per-EXE mapping may need reselection through S.

F2 now provides the host-owned idle desktop/theme/menu and executable selector;
see [F2_DESKTOP.md](F2_DESKTOP.md). F3a now adds the bounded F1-backed browser and path-only Run; see
[F3_EXPLORER.md](F3_EXPLORER.md). F3b now adds explicit document applications and bounded A/W/CRT argv transport
([contract/tests](F3_DOCUMENTS.md)). F4a now keeps its native desktop/taskbar/Start around the active guest; see [F4_WORKSPACE.md](F4_WORKSPACE.md). Next: retained native/guest composition and interaction/visual refinement. Persisted preference/association management, supported settings and tree remain F3c, then the F5 scenario. F3/F4/F5 are not complete.

Подробная [очередь действий после F1](F_SHELL_PLAN.md#рабочий-план-после-f1-2026-10-23)
фиксирует критерии F2–F5, возврат к измерениям G и контрольные точки активной работы.

## Implementation order and gates

1. Preserve F1 mount/path/readonly/CWD/volume and cross-session x86 regressions;
   existing standalone host launches keep the legacy one-C-per-EXE default.
2. Preserve F2/F3a/F3b shell/browser/document checks. Finish F4b active native/guest
   retained-layer composition and interaction checks; then F3c preferences/settings/tree.
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
