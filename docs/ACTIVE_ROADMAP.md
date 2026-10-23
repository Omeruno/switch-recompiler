# Active implementation map — 2026-10-23

Current user priority: complete the supported XP-like shell and bind its files
to actual Switch SD storage. The next code package is **F1: persistent roots and
filesystem contracts**, before further G optimization. See the source-backed
[audit](PROJECT_AUDIT_2026_10_23.md) and [F1–F5 shell plan](F_SHELL_PLAN.md).
AI_HANDOFF.md and this map supersede historical next-E1/next-G notes.

| Block | Implemented / accepted | Still required |
| --- | --- | --- |
| A/B | CPU oracle/regressions, cooperative guest threads/TEB/TLS/native continuations | Continuous correctness; bounded HLE response and compatibility extensions |
| C | Limited one-Process HANDLE/wait/APC/timer/lifecycle scope accepted on Switch | Full NT/cross-process/security/AttachThreadInput are outside that acceptance |
| D | PE/DLL/TLS/unload/rollback, limited SEH and substantial Win32/CRT | Shared filesystem/volume/path contracts for F1; page protection enforcement, full unwind/COM/API compatibility remain separate |
| E0–E4 | Backend/software/dirty/fallible resources/lifetime; accepted Stage E milestone 60% | Preserve regressions |
| E5–E7 | Switch GL/fallback, retained layers, ownership, revision caches and 28-scene CPU/GL oracle | Current Paint/output/input/fallback hardware acceptance and console workload evidence |
| E8/E9 | CPU/GPU/bytes/memory/input-correlation metrics, six software groups and public EXE/report | Console measurements, total process/driver memory, physical latency, GPU/fallback and real Paint |
| F0 | Picker, core/USER/legacy/MFC session ownership and production launch/exit/error/return loop | Physical picker/relaunch acceptance; **not the desktop/Start/explorer milestone** |
| **F1–F5: next** | Existing host filesystem/USER/GDI/font/input primitives can be reused | Persistent SD-backed roots → native XP-like desktop/taskbar → functional Start/explorer → shell/guest integration and save/reopen scenario |
| G0 | Bounded opt-in opcode/host-entry census and public fixture | Representative PE timing/overhead and optimization deferred behind the F milestone; census is not CPU speedup |
| H/I | Diagnostics/build artifacts exist; no completed DirectDraw/D3D/product release | Separate graphics API translation, installation/config/compatibility matrix/release work |

## Next concrete package: F1

Today `Process::LoadMainExe` assigns C to the EXE directory; VFS drive letters
alias that one root, and volume/free-space APIs return constants. File bytes do
persist on real host/SD storage, but a common desktop/documents/programs volume
is missing. Resolve these contracts before drawing a functional explorer.

The proposed shell layout is C on `sdmc:/switch-recompiler/drive_c/`, a separate
SD view S and read-only ROMFS R. This is a plan, not shipped mounts. Preserve
existing EXE directories, documents and registry; keep EXE directory, guest CWD,
module/command-line paths and DLL search distinct from the persistent root.
Detailed acceptance and licensed visual sources are in F_SHELL_PLAN.md.

## Implementation order and gates

1. F1: mount/path/readonly/CWD and volume provider; shared documents and real-x86
   cross-session persistence, loader/CRT/dialog consistency and legacy launch compatibility.
2. F2/F3: native blue/green XP-like desktop/taskbar/Start and bounded real-directory
   explorer. Selected assets need source/license/hash notices; CSS is not a Switch runtime.
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
