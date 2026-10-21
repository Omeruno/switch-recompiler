# AI development handoff — switch-recompiler

Last verified GitHub state: **342f724be035586d818ce0b62e5092a84d9daeed**, branch `claude/wonderful-hawking-10mpr0` (2026-10-21 11:41 UTC). This file is coordination documentation, **not** a substitute for the actual branch HEAD, diffs or test results.

## Agreement between development shifts
- Active development handoff: Claude finished work on 2026-10-21 and plans to return Friday. ChatGPT continues in the interim. Before EVERY change re-check branch HEAD and recent commits; never assume the SHA above remains current.
- Every coherent change gets a separate descriptive commit on `claude/wonderful-hawking-10mpr0`. Do not overwrite another developer's work or force-push.
- Before pushing code, run `sh tools/run_host_tests.sh`; ensure Switch CI build and CPU oracle remain green. Local Microsoft binaries are optional and not part of public CI.
- Never commit Microsoft binaries: `paint.exe`, `winmine.exe`, `mfc42u.dll`; they belong in ignored `local/`.
- Preserve working functionality; describe exceptional changes to existing behavior in the commit message.
- Update this file when handing development over, recording HEAD, outcomes, tests and unfinished investigations. No speculative claims of success.

## Verified state from Claude's last message and GitHub
- Latest Claude commit `342f724`: Switch allows launching Paint on the **new** UI layer by holding **L** at startup; `mfc42u.dll` must be beside `paint.exe`. This is a testing override, **not** yet the default path.
- CI run [39593798846](https://github.com/Omeruno/switch-recompiler/actions/runs/39593798846): host-tests, CPU oracle and Switch build completed successfully. Artifact name `switch-recompiler-nro`.
- Reported local inspection command: `cd local && ../build-host/winrun --new-ui -f 60 -s 50 out.ppm paint.exe`; `-t` enables Win32 call tracing. These local binaries are unavailable through GitHub.
- **Known unfinished feature:** common Open / Save As dialogs via `comdlg32.dll`. Do not switch Paint to new UI by default until this functionality is validated.
- Existing new UI: `source/win32/user32_dialog.cpp` handles Win32 dialog templates; `source/win32/user32_*.cpp` provides UI; `source/ui/` is software graphics; `source/runtime/files.cpp` supplies VFS; `source/win32/kernel32_file.cpp` supplies file API. The loader lists `comdlg32.dll` among system HLE modules, but no standalone comdlg32 module was present at this checkpoint.
- `docs/ARCHITECTURE.md` describes historical v1 and should **not** be trusted as the current v2 architecture without verification.

## Next work item: common file dialogs
1. Audit existing imports and file-system/path semantics. Investigate what Paint calls on Open and Save As; collect trace if needed.
2. Implement a minimal **general** comdlg32 Win32 API layer (GetOpenFileNameA/W and GetSaveFileNameA/W), including correct 32-bit OPENFILENAME structures, flags, filters, nMaxFile, dialog owner, cancel behavior, CommDlgExtendedError and output file path semantics.
3. Reuse the existing new USER32 dialog, list/edit/button controls, VFS directory enumeration and common drawing. Avoid hard-coded Paint paths or UI coordinates.
4. Provide guest Win32 regression tests for positive selection, cancel, buffer too small, filter and extension behavior, ANSI/Wide strings, valid/invalid directories. Verify non-Paint apps too.
5. Run host tests, CPU oracle and Switch build. Mark actual Switch behavior separately (requires user's hardware validation).
6. Only **after** Open/Save As work and Paint hardware verification, consider changing the default Paint UI path.

## Future roadmap (planning, not completed)
- Thread model: per-thread x86 registers, stack, TEB/TLS, wait objects, scheduling and synchronization.
- Persistent Windows-style C: drive / directories, registry and process isolation; installers and archive extraction after robust file semantics.
- CRT / COM / game graphics / DirectX / JVM / CLR / PhysX only after dependency-specific feasibility assessments; do not imply binary DLL copying is sufficient.

## Handoff log
- **2026-10-21, ChatGPT incoming:** reviewed Claude's final message, commit `342f724`, CI run results, UI dialogs, VFS and host-function interfaces. GitHub source was accessed read-only prior to creating this document. No code/behavior changes made in this handoff commit.
- **For Claude on Friday:** read this file plus commits after `342f724`; check CI and hardware notes, then coordinate the next task with the user.

## ChatGPT development shift — 2026-10-21 after Claude's handoff
- Added `source/win32/comdlg32_module.cpp`, first implementation of `GetOpenFileNameA/W`, `GetSaveFileNameA/W` and `CommDlgExtendedError` on new UI; commit `6cedf8f`.
- Added `tests/guest/gui_comdlg.c`, negative validation smoke tests for invalid `OPENFILENAME`, commit `87b7247`; updated GUI test linking in `tests/guest/build.sh`, commit `c747511`.
- Fixed compile errors in `d23cd36` after reading CI compiler logs (ambiguous Win32 constants, host procedure lookup, WS_TABSTOP).
- At last check of CI run [39596778877](https://github.com/Omeruno/switch-recompiler/actions/runs/39596778877): **CPU oracle and Switch build passed**, host tests still running. Recheck before assuming green.
- **NOT YET PROVEN:** actual file picker interaction, Paint opening/saving files on Switch, ANSI/Unicode path edge cases. Request human hardware verification. Do not switch new UI to default yet.
- **First implementation limitations:** no file-type filters yet, no real overwrite-confirmation prompt (currently refuses an existing file when OFN_OVERWRITEPROMPT); no OFN hooks/templates, multi-select, file dialog notifications or full Windows error behaviors. Directory list is basic; prioritize correctness before appearance.
- **Potential bug to investigate:** WndProc callback/context lifetime and listbox selection notifications; inspect with `winrun --new-ui -t` or a purpose-made scripted UI test, not guesses.

- **2026-10-21 follow-up:** Commit `2955d04` populates the dialog filename edit control from initial `lpstrFile` and implements basic semicolon-separated extension filters (`lpstrFilter` + `nFilterIndex`) for directory listing. This is not full Win32 wildcard syntax and not a substitute for testing. CI run [39598367690](https://github.com/Omeruno/switch-recompiler/actions/runs/39598367690) should be checked for completion.
- The earlier code+tests at `d23cd36` and handoff at `13d33c7` passed all 3 CI jobs. No hardware verification yet.

- **2026-10-21, further review:** `852589e` fixes a real UI bug: host-side ListBox enumeration did not recompute scrollbar data; after listing contents, it now sends WM_SIZE to ListBox for scroll range recalculation.
- `f8b146d` handles Enter/Esc and a basic Tab focus switch in file dialog modal loop. **Tab handling remains minimal**; test keyboard usage on Switch and host before claiming parity.
- Latest tested baseline known good: `13d33c7`. Later commits' CI results must be checked; don't claim Switch UI behavior is hardware-verified.
- Needed next: scripted positive UI tests on a separately compiled guest Win32 executable, real-life Paint Open/Save smoke test, directory navigation and cancellation, proper OFN overwrite confirmation and cross-codepage filename handling. Never turn new Paint UI on by default until tested.

## 2026-10-21 Switch hardware report: critical Paint Open/Save exit
- User tested Paint on Switch with new UI (L held). Actual dialog appears, toolbox/palette work; user reports file list initially blank and the application exits when opening an existing saved file or saving.
- **Important: NOT FIXED yet**. Never describe successful host tests as proof Paint's MFC file load/save works.
- Added `tests/guest/gui_comdlg_interactive.c` and `tools/run_host_tests.sh` scripted end-to-end Open, Save, and clicking a BMP from list. First scripted click incorrectly hit beyond ListBox row and failed (CI `fa583d7`); trace showed one BMP found but `listSel=-1`. Fixed the **test harness coordinates** robustly via semantic control HWND lookup in `tests/winrun.cpp` (`3c737af`), updated CI command (`01ac3ee`). **All three CI jobs for [39605326606](https://github.com/Omeruno/switch-recompiler/actions/runs/39605326606) passed**, including scripted file selection and subsequent Win32 file I/O.
- Added opt-in host-only COMDLG tracing with `SR_COMDLG_TRACE` (`149916d`); does not print to closed Switch console.
- On Switch, nested x86 failures can call `cpu::FatalError` directly, bypassing main's error output. Added fatal logging at `source/cpu/guest_memory.h` (`761ad5e`) and nested exception metadata in `source/win32/message_loop.cpp` (`ce9795a`). `source/main.cpp` also logs regular interpreter stops (`f6a6e`, `eca084`). File on Switch: **`sdmc:/switch-recompiler-crash.txt`**; log is overwritten on next fatal exit, so collect immediately.
- Next actual hardware test: download NRO from successful CI run, hold L for Paint, reproduce Open/Save crash, then share the text contents of `sdmc:/switch-recompiler-crash.txt`. If log is absent, need diagnose outside guest FatalError path (native libnx/native crash).
- Possible causes of Paint-only crash: later MFC guest code / Win32 API gaps, bitmap parsing, nested guest interpreter; **not verified**. Do not patch based on speculation. Collect EIP/error first.

## 2026-10-21 actual Switch crash report and next diagnosis
- User provided crash log after attempting Paint Open/Save under new UI: `FatalError`, nested WndProc call entered EIP `0x00000000`, no CPU exception code/address recorded (both 0). **Do not assume this is definitely a missing import**: could be a null function pointer, incorrect x86 state or unresolved export.
- Problem in existing diagnostic: `message_loop.cpp` tracked `lastEip` but did not use it when reporting EIP=0; global `g_lastKnownEip` was 0. Fixed via `09b6d4b` and `a8dd783`: report previous nonzero instruction EIP, ESP, module name, module-relative offset, and use neutral message about null jump. File remains `sdmc:/switch-recompiler-crash.txt`.
- **Need next hardware log from this updated NRO**, not the old one: new fields `Previous instruction EIP`, `Previous module`, `Previous module offset`. Use those to inspect exact guest instruction and resolve offending import, pointer, or call chain. No functional fix for Paint crash yet.
- Check GitHub Actions run [39608410458](https://github.com/Omeruno/switch-recompiler/actions/runs/39608410458) before recommending download. Retain stable old NRO separately.

## 2026-10-21 additional actual hardware fault data (very important)
- User reproduced failure on real Switch in current enhanced diagnostic build: `Previous instruction EIP: 0x71da298d`, `Previous module: mfc42u.dll`, `Previous module offset: 0x0002298d`, `ESP: 0x0018f10c`. Guest next EIP was `0`. Fault code and fault address both zero because this is a null transfer, not recorded CPU fault.
- It is **NOT proven** to be a missing import. MFC could have called null via an uninitialized vtable, IAT, bad return address, or incorrect instruction semantics.
- Changes `c79c877` and `37b7cf1` enhance diagnostic with mapped instruction bytes (12 bytes at last nonzero EIP), EAX/ECX/EDX/EBX/ESP/EBP/ESI/EDI, and first 8 guest stack DWORDs. Native Switch only logs on fatal exit, no printf after consoleExit. Collect refreshed `sdmc:/switch-recompiler-crash.txt` from this build to decode offending operation before changing API implementation.
- Latest CI for code commit: https://github.com/Omeruno/switch-recompiler/actions/runs/39610159477 (check completion). Keep prior stable .nro.
- Once bytes obtained: determine if instruction is `FF 15` (indirect call IAT), `FF /2` (call through register/memory), `C3/C2` (bad RET), or another instruction; then inspect IAT and guest pointer origins. If DLL bytes are required to symbolicate, obtain user's matching `mfc42u.dll` legally/privately (don't commit Microsoft binaries).

## 2026-10-21 Windows delay-load and SEH investigation
- Actual Switch fault now at `mfc42u.dll+0x21FD8`, opcode `FF E0` = `jmp eax`, EAX=0. Previously `+0x2298D`. This is consistent with unresolved delayed function pointer but the target DLL/function remains unknown.
- Investigated `Process::ResolveExport`, KERNEL32 `GetProcAddress`, SHELL32 HLE. New recent GetProcAddress ring buffer in `guest_memory.h` and `kernel32_module.cpp` from previous commits can reveal precise resolution result on next Switch crash.
- **Found a definite correctness bug**: legacy `Impl_RaiseException_Experimental` in `source/win32/host_functions.cpp` did absolutely nothing and returned as though the Windows SEH exception were handled. Standard MSVC delay helper calls RaiseException if unable to resolve an import; swallowing it can lead to `jmp eax=0`. This is a possible explanation, not conclusively verified.
- Fix in commits `6d73520`, `3dfa89a`: preserve RaiseException code/flags/first arguments into Switch fatal log, stop with explicit `Unhandled Win32 RaiseException (guest SEH dispatch not implemented)` instead of silently returning. No real SEH dispatch yet: requires guest FS:[0] registration chain, exception/CPU context records, handler invocation, unwind semantics. Do not claim implemented.
- Hardware feedback requested after *one* successful CI build: crash log should show either `Win32 RaiseException: code=...` and `Recent GetProcAddress calls...` or exact null jump. Use it to pinpoint missing export before implementing another API. Avoid repeated full CI between tiny edits.
- User has no local repository; all code work lives in GitHub branch `claude/wonderful-hawking-10mpr0`. User can supply legitimate Windows DLL/EXE or diagnostic utilities if needed, with exact version/source requested.

## 2026-10-21 confirmed Switch Paint success and SEH continuation
- **User hardware-confirmed:** Paint successfully opens and saves files with current builds containing `COMDLG32!GetFileTitleW/A` (commit `c59099d`). Opening and saving now work correctly on physical Switch; retain known-good build `2c9a1fa` (CI 39622736600 green). This is actual hardware feedback, not only host tests.
- User explicitly requested: stop repeated planning, continue implementing universal Win32 foundation. Main priority is SEH, followed by guest threads and synchronization. Do not regress Paint.
- Initial SEH dispatch in `source/win32/seh_dispatch.cpp` and `RaiseException` wiring exists and CI green on `2c9a1fa`; it is **not full SEH** (missing CPU fault dispatch, unwinding, nested handling and per-thread TEB).
- Commit `180a6e4` upgraded guest SEH test to a two-handler FS:[0] chain, verifying ContinueSearch -> ContinueExecution, proper exception code/arguments and CPU context. Check CI; if it fails, fix based on logs.
- User prefers one consolidated full CI pass over repeatedly compiling after each small change. All work GitHub branch `claude/wonderful-hawking-10mpr0`.

## 2026-10-21 next implementation: CPU faults -> shared guest SEH
- User reconfirmed physical Paint Open/Save works. Keep `2c9a1fa` as baseline until new tests pass.
- On working branch, commits `bdfddc1`, `3df42c0`, `1706dbe`, `9923022`, `0302cd4` connect interpreter `CpuException` to `win32::seh::DispatchCpuFault` in nested WndProc, host winrun, and Switch main loops. Access violations use Windows two-argument exception information ([0]=read/write, [1]=address).
- `de3fffb` extends x86 `gui_seh.c` with `INT3`, `ExceptionContinueSearch`, `ExceptionContinueExecution`, and `CONTEXT.Eip += 1` continuation.
- **CI WARNING:** GitHub REST Actions runs API still displayed latest run for `2c9a1fa` despite newer commits being visible in branch history. No successful CI evidence for `de3fffb` yet; do NOT recommend downloading its NRO as verified until CI executes.
- Remaining work: investigate why Actions is not triggering after newer GitHub Contents API commits, compile/test `de3fffb` when available; implement x86 unwind, nested/collided exception semantics, unified per-thread exception state, then thread scheduler.

## 2026-10-21 targeted x86 SEH unwind implementation
- Hardware-confirmed baseline Paint Open/Save: `2c9a1fa` (green CI `39622736600`). Preserve that NRO.
- New branch commits `c65d19e`, `5e546d3`, `4d81c42`, `10096b5` add **target-frame subset of RtlUnwind**, with KERNEL32 export, termination handler calls receiving EXCEPTION_UNWINDING, validated SEH chain walk, FS:[0] removal, TargetIp continuation and EAX return value. `tests/guest/gui_unwind.c` checks it through dynamically resolved API.
- **LIMITATIONS:** No exit unwind (TargetFrame=null), collided/nested unwind, stack transfer into unrelated frame, general unwind CPU context, full Windows exception semantics. On unsupported paths fail visibly rather than silently return. No claims of full SEH yet.
- Automated GitHub Actions continued to show runs on older commit `de3fffb` when this change was made; no new run for `10096b5` yet. Do not claim new code passed CI before seeing a run with matching SHA. GitHub Contents API commits may not automatically trigger Actions under token-authenticated pushes.
- Next: run host `gui_unwind` and `gui_seh` on exact branch commit, fix any real CI failures, then extend to exit/nested/collided unwind and thread-local SEH.

## 2026-10-21 x86 SEH exit-unwind implementation
- CI `39626685021` for targeted `RtlUnwind` baseline `10096b5`: **success** on host tests, CPU oracle, Switch build.
- Current commits `d692d27`, `e03114e`, `17df8d5`: `UnwindToFrame` now supports `targetFrame=0` exit-unwind, walks to `FS:[0]` sentinel `0xffffffff`, sets both `EXCEPTION_UNWINDING` and `EXCEPTION_EXIT_UNWIND`, calls each guest termination handler, clears chain, resumes at TargetIp; `gui_unwind.c` exercises flags and chain removal.
- **Not yet CI-confirmed for latest commit** (GitHub Actions jobs backlogged and sometimes triggered out of order). Confirm exact SHA and host test output before advising Switch deploy.
- Still unsupported: collided/nested dispositions, unwind into unrelated stack frame, NT unwind/context and guest threads.

## 2026-10-21 nested and collided SEH work
- User confirmed prior exit-unwind CI run completed green; `17df8d5` / `ffd2cae` show successful workflow runs.
- New `seh_dispatch.cpp` changes: `41ec7a9` tracks currently executing guest exception handler registrations on the single guest thread, skips the same handler during nested `RaiseException`, and sets `EXCEPTION_NESTED_CALL`; `tests/guest/gui_seh_nested.c` tests inner/outer handler and counts. This is partial nested exception support, **not full NT nested frame semantics**.
- New `e022e5e` passes guest dispatcher-context pointer to termination handlers, accepts `ExceptionCollidedUnwind` when handler supplies a validated forward frame inside pre-scanned unwind chain; rejects backwards or malformed frame redirects. `tests/guest/gui_seh_collided.c` checks skipped frame, collision flag, and chain head. This is a conservative subset, **not complete collision/unwind state management**.
- New SHA CI must be checked before claiming tests pass. Keep hardware-confirmed Paint baseline `2c9a1fa` available.
- Next: thread-owned active SEH state, proper x86 CONTEXT FPU / SSE fields, ensure nested-dispatch exception-record scratch buffers and callback stack isolation are robust.

## 2026-10-21 CONTEXT32 floating/SSE implementation
- Nested and collided SEH tests are on branch; `3791faf` CI green; some newer runs still in progress. Preserve hardware-confirmed Paint build `2c9a1fa`.
- Commit `422f3fb`: `seh_dispatch.cpp` now uses `CpuState::GetEflags()` to materialize lazy CPU arithmetic flags into guest CONTEXT and `SetEflags` on restore. Sets CONTEXT_CONTROL|INTEGER|SEGMENTS|FLOATING_POINT|EXTENDED_REGISTERS (0x1002f). Serializes x87 control/status/tag/last pointers and 80-bit register images in FLOATING_SAVE_AREA (CPU internally holds double, therefore x87 precision remains bounded by double), plus MXCSR and XMM0..7 in FXSAVE-layout ExtendedRegisters. Honors CONTEXT flags on floating restore.
- Commit `7610813`: guest `gui_seh_context.c` verifies XMM0 visible in CONTEXT, then modified and restored alongside MXCSR after `RaiseException`.
- Exact SHA CI for these commits **not yet confirmed successful** when written; check GitHub Actions run `39630744462`. Do not claim fully correct Windows x87 ABI until broader FPU and SSE tests pass.
- Next: verify CI and fix compilation/test failures, then address per-thread SEH active-handler state and guest scheduler.

## 2026-10-21 GuestThread foundation and CI recovery
- Found real host CI failure for `7610813`: i686 MinGW's default target rejects x86 `xmm0` clobber in `gui_seh_context.c` without SSE. Commit `4d47022` builds that specific guest test with `-msse2` (not a global compiler flag).
- Found **undefined behavior** from an older bug: `source/cpu/guest_memory.h` allocated `g_sehExceptionArguments[4]` while `host_functions.cpp` already looped over 15; commit `8987b27` expands array to 15. This is a genuine runtime memory corruption fix.
- Introduced `source/runtime/guest_thread.h` (`b72c65a`) with owner of CPU, TEB, stack bounds, TLS array, ID, lifecycle/status, suspend/wait fields, and thread-owned active SEH handler frames (`c07cb8b`). `Process::MainThread()` and `CurrentThreadId()` exposed by `e7ad66a`. Main thread initialized from prepared TEB/CPU (`046ec8d`).
- `KERNEL32!GetCurrentThreadId` now obtains the current primary guest thread ID from Process (`c7fd25b`). `gui_thread_identity.c` checks it against `FS:[0x24]` and TEB self field (`12d23be`).
- `seh_dispatch.cpp` uses active SEH frame state stored in `Process::MainThread()`, not global dispatcher variables (`c3e2fbe`). `source/main.cpp` and `tests/winrun.cpp` now execute the actual `GuestThread::cpu` via a reference, rather than a disconnected local CPU copy (`3322213`, `12b2e9d`).
- **NO new runnable guest scheduler yet.** Only one main guest thread; thread-related fields are groundwork, NOT evidence CreateThread/Wait* work.
- Preserve known good Paint NRO from `2c9a1fa`. Latest CI not yet confirmed green on `12b2e9d` when written.

## 2026-10-21 cooperative GuestScheduler first implementation
- User agreed to postpone Windows XP-like taskbar/Start shell until a functioning multithreaded Win32 runtime; continuing core development.
- Previous full CI for `12b2e9d` succeeded (run `39631972627`); later other CI jobs still running, don't confuse revisions.
- `source/runtime/guest_scheduler.h` introduced in `a17edb5`: header-only cooperative scheduler tracking guest-thread pointers, round-robin Next(), suspend/resume counts, waiting on object-handle IDs, wake one/all, exit code/lifecycle. This is **cooperative bookkeeping only**; NO true CreateThread, concurrent interpreter execution, working guest WaitForSingleObject or sync HANDLE integration yet.
- `tests/test_guest_scheduler.cpp` `0b0e747` checks identity validation, deterministic round-robin, suspension, wait, signal one/all and termination. Hooked into `tools/run_host_tests.sh` with `f381e2c`.
- `Process` now owns `GuestScheduler` (`13aba06`), registers the main guest thread in `PrepareMainThread` (`ddeec51`). MainThread CPU and SEH active stack ownership from previous commits remain.
- Actions run `39632823805` corresponds to `f381e2c`, was still in progress at handoff; check exact SHA before treating branch as tested.
- Next implement actual worker-thread creation: separate stack + TEB + TLS, safe storage/lifetime for GuestThread, thread-local HLE last error and SEH, context switch into scheduler. Ensure verified old Paint NRO `2c9a1fa` remains intact.

## 2026-10-21 worker GuestThread allocation milestone
- Verified cooperative scheduler run `39632823805` SUCCESS (host tests, Switch build, CPU oracle) for `f381e2c5`.
- New `Process::CreateGuestThread(startAddress, parameter, stackReserve, suspended, error)` implemented (`2062ecb`, `b8d0be0`, `28af8b6`): maps separate worker TEB, stack and TLS vector, initializes x86 CPU entry and stdcall argument stack, TEB FS:[0] sentinel, NT_TIB stack pointers, ClientId.UniqueThread, Self, TLS pointer and shared PEB; allocates private initialized static-TLS blocks per DLL and rolls back allocations on failure. Stores workers in stable unique_ptr allocation and registers with GuestScheduler. Exposes `Process::FindThread(id)`.
- `tests/winrun.cpp` `--probe-threads` creates two workers (one suspended) and validates stack/TEB/TLS/SEH chain/CPU/argument independence (`0533f39`, `cf44d77`); `tools/run_host_tests.sh` invokes this probe on `gui_seh.exe` (`5dd1b5`).
- **Not yet actual concurrent execution:** scheduler round-robin is only bookkeeping; no HLE CreateThread and ExitThread, no context switch in interpreter loop, no thread-private Win32 HLE globals, no THREAD_ATTACH notifications. Worker return stack currently uses process EntryReturnAddress TEMPORARILY, and must be replaced with ExitThread trampoline before executing workers. Do not imply multi-threading functional.
- Latest commit `cf44d77`, its CI run `39633616818` initially queued. Check exact SHA CI and logs before declaring tests pass.
- User wants XP-like desktop/Start menu after viable multithreading; prioritize functional runtime, not GUI mockups yet. Paint Open/Save on real Switch verified with previous baseline.

## 2026-10-21 complete architecture roadmap and execution quantum
- User wants native Horizon graphics pipeline (rather than copying Wine/Winlator), XP-like Start shell after functional multithreading. New detailed Russian `docs/ROADMAP.md` commit `f9d11f7` maps stages A-I, CPU oracle, thread lifecycle, typed handles, Win32 ABI, GDI software reference + native GPU backend, XP host shell, optional dynarec, DirectDraw/D3D, release/tests, x64 explicitly separate.
- Confirmed `39633725215` SUCCESS for worker thread creation/TLS probe checkpoint `a9e62cff`.
- `source/runtime/guest_scheduler.h` `06e746f`: `RunQuantum(step,budget)` selects one runnable guest, calls a one-instruction callback at most `budget` times, stops early on callback yield or guest lifecycle changes (Waiting/Suspended/Exited); returns chosen thread id, executed count, callback stop flag. No host blocking.
- `tests/test_guest_scheduler.cpp` `d0da2da`: deterministic 2-guest CPU register increments under quantized round robin, suspension/wait interrupt, wake, callback yield and finished state. Standalone test already wired in host CI.
- **Crucial**: callback tests increment a register; they DO NOT run actual x86 Interpreter::Step for two threads yet. Wire main and host winrun interpreters next with per-thread FS base, TryHandleHostCall routing and proper ExitThread trampoline; do not claim runnable CreateThread yet.
- CI for latest quantum tests pending when written: `39634978264` / `39634968660`, must verify exact SHA.

## 2026-10-21 first real interpreter multi-thread quantum test
- User urged further progress after comprehensive roadmap. Keep branch `claude/wonderful-hawking-10mpr0` only. Last completed CI for `1833a8a` success (run `39635043552`), encompassing roadmap/quantum as of that SHA; later commits need own checks.
- `tests/winrun.cpp` `590e628`: added `--probe-x86-threads`, maps x86 machine code `64 A1 24 00 00 00 40 40 F4` (MOV EAX, FS:[0x24]; INC EAX twice; HLT), launches two workers via CreateGuestThread on separate TEB stacks; marks main waiting and runs Interpreter::Step with each worker's FSBase for instruction quanta via GuestScheduler::RunQuantum(2). Confirms each thread's EAX exit code is its own ID+2, independent progress and completion. Host CI script runs the probe since `62e48cc`.
- `GuestScheduler` now has Current()/SetCurrent and Next() updates current pointer (`b77f511`); `Process::CurrentThreadId/CurrentTebAddress` use scheduled current (`7013030`); x86 SEH resolves active handler stack against worker TEB via Current/FindThread (`bc50d26`); probe tests changing current ID/TEB each quantum (`e783f89`).
- This verifies **actual IA-32 guest instruction interleaving in host test**, not fully wired gameplay/main loops. No guest KERNEL32 CreateThread/ExitThread API yet; worker return thunk is still temporary; do NOT claim complete thread support or Switch hardware validation.
- Exact latest commit `e783f89` CI queued `39635642515` when written. Switch build and CPU oracle of preceding `62e48cc` already succeeded but host tests pending.
