# Active implementation map — 2026-10-25

[Визуальная карта проекта](https://omeruno.github.io/switch-recompiler/) · [Автономный HTML](roadmap/index.html) · [Как открыть и публиковать](roadmap/README.md)

**Current position: G4b.2b bounded real HWND/DC resource binding implemented; next G4b.2 CPU/GPU coherence/
production-presenter integration.** Runtime baseline `1d3f15588f51745c92292d073c686c2591f81a85`
passed [CI40204271361, all six jobs](https://github.com/Omeruno/switch-recompiler/actions/runs/40204271361).
G3/G4 GPU-GDI remains isolated; ordinary Paint GDI still runs on CPU.

[User-facing global status](PROJECT_STATUS_RU.md), [global A–I plan](ROADMAP.md),
[technical handoff/history](AI_HANDOFF.md). These documents supersede old next-F1,
next-image/F5 and next-G1 notes. No overall project percentage is inferred.

## Product direction

The user's long-term targets are **Spore, Mirror’s Edge and The Sims 3** as PC
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
| D | PE/DLL/TLS/unload/rollback, bounded SEH, substantial Win32/CRT | Page-right enforcement, full unwind/COM/API compatibility; Pinball x86 first host blocker RegCreateKeyExA |
| E | CPU GDI reference, production Switch GL composition/fallback, retained windows/ownership/revisions, E8/E9 metrics/public oracles | Current physical Paint/GPU/fallback/Lite/input/relaunch and real workload evidence |
| F | Bounded software F0–F5 passes; Luna updates and active-panel integration retained | Physical acceptance; complete XP window behavior and planned extensions |
| G | G0/G1 measurement, G2 CPU SRCCOPY enabled; G3/G4a/G4b.1 actual-GL semantic prototypes/count and timing controls | **G4b.2** integration before activation; dynarec/code-cache work separate |
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

Native-shell minimize/maximize caption glyphs remain decorative; free native-shell drag/resize,
multiple simultaneous guest EXEs and a complete XP shell are not accepted baseline
features. File APIs/native tools retain their documented limits. PNG/JPEG,
recursive file-manager features and arbitrary Windows shell/COM are not implied.

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
| Remaining G4b.2 | CPU/DIB/ROP/read/save/resize coherence and owning production texture bridge | **Next, not implemented/activated** |

Host software GL timings and exact work reductions are not Switch GPU/FPS
measurements. Capacity checkpoints and explicit CPU reads still synchronize.
Minimum ARM64 executable-memory probe previously returned42 on Switch; do not
repeat it as a prerequisite or treat it as an accepted full dynarec.

## Next package and activation gates

1. Preserve the [bounded HWND/DC bridge](G4B2B_WINDOW_BINDINGS.md) and shared GL-state
   contract. Native target leases are not owning production frames.
2. **Next:** define genuine CPU barriers for GetPixel/GetDIBits, CPU ROP/source access and
   raw guest DIB writes; define resize/repaint/fallback authority. Keep unsupported cases CPU-authoritative and report
   unsupported operations honestly; no successful stubs or EXE-name exceptions.
3. Preserve the [implemented shared-context handoff](G4B2_SHARED_CONTEXT.md) and build a texture-aware bridge to the
   existing presenter. ShellSession currently reads canonical CPU pixels: adding
   GPU drawing plus unconditional readback/duplicate CPU raster is insufficient.
   Preserve ownership masks, CPU fallback, independent input and all Luna changes.
4. Run public actual-x86 GDI/window/pixel/readback/session fixtures and actual-EGL
   comparisons before selective production activation. Measure transfer/checkpoint/
   submission overhead. Do not require physical user testing to develop these.
5. Later gather real Switch workload evidence and the consolidated physical E/F
   checklist. Hardware checks stay deferred at the user's request.

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
