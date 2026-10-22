# AI development handoff — switch-recompiler

Last verified implementation GitHub state: **1a5f585ea718eadf02eb3696cb86f6accbe44bb7**, branch `claude/wonderful-hawking-10mpr0` (2026-10-22). All five jobs passed in run [39775696837](https://github.com/Omeruno/switch-recompiler/actions/runs/39775696837), including 36 initializer fault modes. The GUI input/Stage C acceptance packet below still needs its own exact-SHA CI. This file is coordination documentation, not a substitute for branch HEAD/diffs/results.

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

## 2026-10-21 first guest Win32 thread HLE APIs
- Previous full CI `39635724430` SUCCESS for `ab7035bc` (real x86 workers and thread-aware SEH).
- Implemented in `source/win32/kernel32_module.cpp`: KERNEL32 CreateThread (24 stdcall bytes), ExitThread (4), GetExitCodeThread (8), SuspendThread (4), ResumeThread (4); synthetic handle encoding `0xE1000000 | guestThreadId` **temporary until shared NT HANDLE object table**. Resume changes Suspended to Runnable at zero count. Main-thread ExitThread currently emits fatal diagnostic pending complete process lifetime handling; not production correct.
- Added internal HLE `__SwitchRecompilerWorkerReturn` to terminate a guest worker when its stdcall start routine RETs, using x86 EAX as DWORD exit code. `Process::CreateGuestThread` installs that trampoline instead of process-wide EntryReturnAddress (commit `2dd3d1a`). This internal name must not be treated as a public Windows API.
- Changed `hle_util.h` GetLastError/SetLastError to use `Process::CurrentTebAddress()` where available (`65f5959`), and `kernel32_module.cpp` TlsAlloc/Get/Set to use current TEB (`e821718`).
- `tests/winrun.cpp` now asserts worker initial return address is internal exit host function (`ba325cc`), and simulates real x86 `mov eax,42; ret 4` into worker trampoline, checking only worker exits and exitCode=42 (`f35ec18`).
- **Major incomplete wiring**: guest main loops (Switch main and general host winrun) are not yet driven by GuestScheduler; CreateThread API creates guest workers but guest code cannot yet expect them to run. ExitThread main thread and waits/CloseHandle still need coherent lifecycle; no DLL_THREAD_ATTACH/DETACH. New synthetic HANDLEs are not yet integrated with CloseHandle or WaitForSingleObject. Do not represent it as fully working Windows multithreading.
- New CI `39636712029` for `f35ec18` was in progress; check completed status and logs before trusting guest thread RET test. Paint known hardware green baseline `2c9a1fa`, keep safe.

## 2026-10-21 scheduled regular execution loop
- Previous `f35ec18` worker RET trampoline full CI SUCCESS run `39636712029`. An earlier intermediate `2dd3d1a` failed a now-obsolete test expecting worker return to terminate the *whole process*; fixed in later tests.
- `tests/winrun.cpp` ordinary production loop now selects `GuestScheduler::Next()` per 256 instruction slots and invokes HLE, Interpreter::Step and SEH on `running->cpu`, using worker TEB for FS. First integration `12b10a0`, per-quantum interpreter reuse `8b31db0`.
- `tests/guest/gui_thread_create.c` added (`fab9030`): guest uses real `CreateThread`, worker writes shared volatile, returns 42, main spins and verifies `GetExitCodeThread`. Normal `tools/run_host_tests.sh` discovers gui_*.c, builds and executes test, not only special probe.
- `source/main.cpp` Switch regular Win32 execution loop now chooses a scheduler thread every 256 operations, creates FS-scoped interpreter per quantum, advances selected thread's CPU through HLE/Step/SEH; non-main HLT terminates only that worker. Commit `28e417a`; testing in CI run `39637524525` when written.
- Found original host_functions.cpp GetCurrentThreadId legacy entry still returned hardcoded 5678 even though separate KERNEL32 module returned thread ID. Fixed to `Process::CurrentThreadId` (`09f89a0`), ensuring original import path and TEB identity match.
- IMPORTANT: Currently no scheduler-integrated guest WaitForSingleObject or CloseHandle, main ExitThread still fatal; nested guest callbacks can block cooperative scheduling; some other HLE global per-thread state may need audits. New code not yet hardware tested. Exact SHA latest CI must be checked; Paint hardware-confirmed rollback SHA remains `2c9a1fa`.

## 2026-10-21 cooperative Win32 thread waits checkpoint
- Confirmed `d695ba0` run `39638620353` SUCCESS all 3 jobs. Four previous red workflows came from a stale variable `threadInterpreter` in winrun; fixed.
- Discovered `kernel32_sync.cpp` previously treated *unknown* handles as already signaled via `TryAcquire`, not suitable for Win32 thread synchronization.
- `guest_scheduler.h` `66ca31b`: Exit(thread) wakes all threads waiting for encoded synthetic thread handle `0xE1000000 | id`.
- `kernel32_sync.cpp` `3696490`: `WaitForSingleObject` and `WaitForSingleObjectEx` special-case known synthetic thread handles: already exited => WAIT_OBJECT_0; zero timeout => WAIT_TIMEOUT; invalid thread id => WAIT_FAILED; infinite wait on live thread => scheduler.Wait(), HLE customReturn=true so blocked thread retains PC/stack, reexecutes HLE on wake. This does NOT implement finite positive timeouts, events/mutex/semaphores waiting, waitAll or true NT HANDLE refcounts.
- Guest test `tests/guest/gui_thread_create.c` `e6ffdf2` uses `WaitForSingleObject(worker,INFINITE)` instead of busy polling, then verifies changed shared value and GetExitCodeThread 42.
- CI `39639875326` for guest test initially in_progress. Must inspect exact HEAD SHA and jobs; no claims of verified real-hardware multiplayer yet.
- Next: fix failures if any; introduce typed HANDLE table and CloseHandle, then event waits and finite timeouts, process lifecycle, full thread synchronization.

## 2026-10-21 event wait / HANDLE validity batch
- Previous cooperative thread wait full CI run `39639964020` success; also `39639875326` success.
- `kernel32_sync.cpp` `e67e80b`: SetEvent wakes scheduler threads waiting on its guest HANDLE (all for manual reset, one for auto-reset). WaitForSingleObject/Ex now blocks guest thread cooperatively for nonsignaled event/mutex/semaphore with INFINITE; does not block renderer/host. Unknown HANDLE now ERROR_INVALID_HANDLE/WAIT_FAILED instead of false WAIT_OBJECT_0 for single waits. **Finite positive timeouts remain incomplete**; mutex and semaphore release wakeup semantics remain incomplete, no real typed NT HANDLE table yet.
- `tests/guest/gui_thread_event.c` `752ac6d`: independent worker signals auto-reset event, main waits INFINITE, validates wake and consumption, worker exit code and CloseHandle.
- `kernel32_module.cpp` `ed7ff23` and `6ea84c9`: tracks open synthetic guest thread handles in std::set, exposes HleIsOpenThreadHandle/HleCloseThreadHandle to other HLE modules; closing HANDLE does not terminate guest thread.
- `kernel32_file.cpp` `ec1d1c5`: CloseHandle checks file/sync/thread handles and reports ERROR_INVALID_HANDLE for unknown/closed descriptors.
- `kernel32_sync.cpp` `5f4b220`: WAIT on a closed guest thread HANDLE returns WAIT_FAILED rather than treating its thread object as open.
- `tests/guest/gui_thread_create.c` `d0614ae`: checks thread CloseHandle, double CloseHandle failure and WAIT_FAILED for previously closed HANDLE.
- Exact latest CI for these new commits in progress as of handoff, e.g. run `39640847744`. Verify before deploying Switch NRO. Existing real-hardware Paint baseline `2c9a1fa` remains rollback.

## 2026-10-21 synchronization batch mutex/semaphore and CI fix
- New events and HANDLE cleanup commits from last checkpoint had Switch compile failures: `kernel32_sync.cpp` calls `HleIsOpenThreadHandle` but no namespace declaration. Fixed in `46a1ed9` (prior run `39640970281` failed Switch with undeclared symbol; don't confuse this with post-fix result).
- `kernel32_sync.cpp` `f09558b`: Mutex now tracks owner guest thread ID and recursion; CreateMutex(initialOwner) records owner, TryAcquire returns WAIT_TIMEOUT if owned by someone else, ReleaseMutex fails for non-owner (ERROR_NOT_OWNER) and wakes one waiting guest on final release. Semaphore validates initial/max count; ReleaseSemaphore validates increment and max and wakes waiting threads up to released count.
- New `tests/guest/gui_thread_sync.c` `a105b3f` verifies owner mutex → worker handoff, release semaphore wake, count consumption, worker exit and CloseHandle.
- Finite positive timeouts, WaitForMultipleObjects, typed HANDLE object lifecycle, critical sections, abandoned mutex and cleanup on thread termination remain future work.
- Latest `a105b3f` CI run `39641366326` queued/in progress when written; check before declaring green. Paint hardware verified baseline `2c9a1fa`.

## 2026-10-21 early architecture hardening
- Confirmed `2e9119c` full CI green (run `39641498932`), and `a50c708` full CI green (run `39642076520`) for roadmap.
- Added guest regression `tests/guest/gui_thread_spin.c` (`b7f75b2`): Windows CreateThread, one guest main thread busy-spins on volatile flag without Sleep / yielding HLE; other worker publishes flag; verifies scheduler instruction-quantum preemption and worker completion. Need confirm host CI green for exact SHA or newer.
- Added isolated libnx `tools/jit_probe/main.cpp` (`bb33f1e`, input API fix `904347f`) that calls `jitCreate`, transition writable, writes ARM64 `movz w0,#42; ret`, transition executable, executes, prints result, frees buffer. Uses modern Pad API (older hidScanInput removed). Standalone Makefile (`854ad64`), README (`1be6eec`).
- CI change `3d7081e` builds and publishes experimental `jit_probe.nro` in a separate `jit-probe-build` job, leaving primary Switch artifact build independent. **Compilation success is not physical permission validation**, user must run experimental NRO on real Switch; collect Result codes and launch context. No JIT integrated into x86 engine.
- Prior CI attempts `ee18a07` / `1be6eec` included probe build inside main Switch job; new independent job avoids losing primary NRO on JIT build failure. Check latest CI run after `3d7081e` for compilation errors.
- Next: investigate HLE nested guest callbacks blocking scheduler / message pump, add latency and watchdog instrumentation. Avoid claiming a busy-wait guest test establishes fairness for all HLE paths. Preserve hardware Paint fallback.

## 2026-10-21 non-blocking guest Sleep HLE and full-sleep scheduler
- Verified prior `b7f75b2` pure busy-spin guest test CI success, and isolated JIT probe separate job compiled green on run `39642997564`; no physical Switch JIT validation yet.
- `source/runtime/guest_thread.h` `ae2c8a8`: per-guest sleepDeadlineUs, sleeping flag and yieldRequested.
- `guest_scheduler.h` `df55c27`: Sleep(thread,ms) defers virtual CPU, WakeExpiredSleepers checks rt::Clock::NowUs every Next, HasSleepingThreads detects all-asleep; Sleep(0) requests preemption; Wait(object) clears sleep state; Exit clears state.
- `kernel32_sync.cpp` `66bd6d6`: HLE Sleep/SleepEx no longer blocks host when scheduler is executing guest thread; SwitchToThread requests quantum yield. Legacy HLE without running guest scheduler still uses rt::Clock::Sleep.
- `tests/winrun.cpp` `3cec271` and `source/main.cpp` `6d8f0df` rotate upon yieldRequested or quantum exhaustion, when all guest threads sleeping tick rt::Clock::Sleep(1) instead of treating as fatal no-runnable.
- `tests/guest/gui_thread_sleep.c` `020da68`: end-to-end worker Sleep(100) and main observes successful completion; `tests/test_guest_scheduler.cpp` `9164b3f`: deterministic fast-forward wake order when all guests sleeping, Sleep(0) yield.
- **Caveats**: SleepEx alertable/APC semantics not implemented; timed object waits not completed, nested guest callbacks may still hold host thread; renderer/graphics event loop and fairness on real Switch need actual test. Full CI for exact HEAD not yet confirmed at handoff; follow runs e.g. `39643603791`.

## 2026-10-21 finite WaitForSingleObject deadlines
- Verified full CI green for previous Sleep checkpoint `865897bd4`, run `39643720166`.
- `source/runtime/guest_thread.h` `aa102888`: guest stores waitDeadlineUs and timedOutHandle.
- `source/runtime/guest_scheduler.h` `ba609a5`: WaitFor(thread,handle,milliseconds) parks guest with monotonic deadline, WakeExpiredWaits wakes expired guest, Signal cancels waiting deadline, Next checks both sleep and wait timers, HasPendingTimers supports all-waiting case.
- `source/win32/kernel32_sync.cpp` `bed4da2`: WaitForSingleObject and Ex on a valid thread/event/mutex/semaphore HANDLE now support positive finite deadlines and INFINITE without blocking Horizon host thread; on wake the HLE call retries with intact EIP/ESP; if deadline expired returns WAIT_TIMEOUT once.
- `tests/winrun.cpp` `bcb8fc7` and `source/main.cpp` `7c6d610`: all-nonrunnable handling checks HasPendingTimers (instead of only sleeping).
- Guest test `tests/guest/gui_thread_timed_wait.c` `ae1a224` exercises WAIT_TIMEOUT 0 and 2ms, early SetEvent signal before 100ms, and finite worker completion. Scheduler host unit `tests/test_guest_scheduler.cpp` `4e95b7e` tests expiration and cancellation.
- CI for exact latest code `39645136359` in progress at time written; verify all jobs. Not yet tested on physical Switch. `WaitForMultipleObjects`, APC, MsgWait*, abandoned mutexes, references of HANDLE and per-thread message pumping remain unfinished. Do not claim complete NT synchronization.

## 2026-10-21 critical-section contention implementation (ChatGPT batch)
- User requested larger interconnected development packages rather than isolated API commits. Plan: complete thread synchronization as a coherent milestone before integrating x86->ARM64 JIT.
- `86441462` fixes `source/win32/kernel32_sync.cpp` CRITICAL_SECTION semantics: thread-specific owner, recursive enters, TryEnter refusing contention, Enter parking a guest thread via scheduler and retrying the original HLE call on wake, Leave waking one waiter upon final release, and basic wrong-owner detection. Existing Win32 ABI structure preserved.
- `37e3a2b` adds `tests/guest/gui_thread_critical_section.c`: real guest CreateThread verifies TryEnter failure under contention, main recursion/unlock, worker Enter wait and protected state transfer. Automatically built and executed by `tools/run_host_tests.sh` via gui_*.c discovery.
- IMPORTANT: not yet a complete Windows CRITICAL_SECTION implementation: lack of deleted-lock invalidation and abandonment policies; ownerless standalone host HLE path cannot cooperatively block. Guest test and Switch CI must pass before describing this as working. **Hardware not tested**.
- CI for new guest test: [39647172174](https://github.com/Omeruno/switch-recompiler/actions/runs/39647172174), was in progress when noted; earlier baseline `4cb6a32` run [39645218633](https://github.com/Omeruno/switch-recompiler/actions/runs/39645218633) successful.

## 2026-10-21 cooperative multiple wait implementation batch
- Previous critical-section implementation and guest test passed all CI jobs, exact run [39647172174](https://github.com/Omeruno/switch-recompiler/actions/runs/39647172174), current docs baseline [39647244237](https://github.com/Omeruno/switch-recompiler/actions/runs/39647244237) successful.
- Commits `258407d9`, `d1e0a4c0`: GuestThread tracks multiple wait handles, GuestScheduler supports WaitForMultiple; Signal wakes waiters whose requested list contains signaled handle; pending finite multiple waits expire via first-handle token.
- Commit `b0b8161b`: replaces nonblocking placeholder WaitForMultipleObjects in kernel32_sync.cpp. Validates 1..64 handles, their mapping, duplicates and known thread/sync handles. Implements wait-any lowest signaled index, wait-all preflight before consumption, immediate timeout, cooperative pending HLE calls and finite wait deadline. Not native threads.
- Commit `6b2c1af`: guest Win32 integration test `gui_thread_wait_multiple.c` exercises both manual-reset events signaled by separate worker, wait-any index, wait-all after second signal, finite timeout, invalid handle.
- Commit `4f354d6`: deterministic host scheduler test for secondary-handle wake and finite timeout cleanup.
- **Current build status NOT YET VERIFIED**: CI run for latest code [39648242384](https://github.com/Omeruno/switch-recompiler/actions/runs/39648242384) was queued when written. Verify all jobs before user deploys.
- **Known incomplete semantics**: wait-all on auto-reset events may wake/re-evaluate rather than reserving a signal atomically; unrelated intermediate signals can reset a finite deadline when the call re-parks. HANDLE object close/reuse/abandonment and MAXIMUM_WAIT_OBJECTS edge cases need stronger compatibility work. The current guest test intentionally tests manual-reset events only. Hardware not tested.
- For next substantial batch, fix intermediate-signal timeout preservation and auto-reset wait-all behavior, then implement thread lifetime / object ownership; do not infer native Switch correctness from CI alone.

## 2026-10-21 large synchronization correctness batch
- Confirmed previous multi-object-wait release `994c378` all four jobs SUCCESS at [39648308984](https://github.com/Omeruno/switch-recompiler/actions/runs/39648308984).
- New linked changes:
  - `6953720`: guest thread stores original absolute retry deadline/token.
  - `31433a1`, later refined by `c8ed306`: scheduler preserves finite timeout on repeated wait attempts after partial object signaling; applies to both single and multiple waits. Sleeping/exited flows clear retry state.
  - `0b39e11`: multiple-object wait returns WAIT_TIMEOUT when original deadline elapsed and clears retry state when fulfilled; closing a sync HANDLE wakes parked guest threads.
  - `13974a2`: closing an open synthetic guest thread HANDLE similarly wakes waiters without terminating underlying thread.
  - `eb1efcf`: single-object waits also honour preserved deadline across intermediate wakes.
  - `98ed113`: release/signal of mutex, semaphore or auto-reset event wakes all candidate cooperative waiters to re-evaluate available resources; only successful HLE acquisition consumes a token.
- New tests: `tests/guest/gui_thread_wait_lifecycle.c` (auto-reset wait-all preflight/consume, partial signals vs timeout, close while waiting); additions to `tests/test_guest_scheduler.cpp` (deterministic original-deadline persistence across partial signals).
- **DO NOT CLAIM NEW WORK GREEN YET**. Latest relevant CI [39649844525](https://github.com/Omeruno/switch-recompiler/actions/runs/39649844525) was in progress at this writing; earlier code runs mostly in progress due lengthy host tests. Last confirmed all-green baseline is `994c378`.
- **Limitations**: Multiple-object wait-all is preflight + consume within a cooperative CPU instruction, but strict Windows kernel wait-all fairness and token reservation with external waiters is not yet fully modelled. No guest hardware validation. Thread HANDLEs still use synthetic one-per-ID mapping rather than reference-counted NT objects; DuplicateHandle semantics beyond file handles need work. ExitThread on main still deliberately fatal.

## 2026-10-21 HANDLE validation after wait-correctness batch
- Previous wait-correctness HEAD `bf8c220` full CI status was still in progress at the beginning of this shift. Do not claim verified until the exact run finishes.
- Commit `e10c2b7` fixes `KERNEL32!DuplicateHandle` in `source/win32/kernel32_file.cpp`: validates destination pointer and flags, duplicates only supported file handles via `rt::Files().Duplicate`, returns error for unknown and unsupported thread/sync handles instead of falsely returning success with original numeric HANDLE, honors DUPLICATE_CLOSE_SOURCE for supported file handles.
- Commit `aabd56d`: new guest lifecycle regression verifies that an attempted unsupported sync-object duplicate fails without destroying the original event.
- This is deliberately **fail-closed**, not a complete NT HANDLE table. True independent duplicate handles for threads, mutexes, events and semaphores remain unfinished. Confirm current CI before using on hardware.

- Commit `af7a63d`: added positive file-HANDLE duplication guest regression in `gui_thread_wait_lifecycle.c`: closing the source must leave the duplicate writable; independently closes duplicate and removes temporary file. Relies on FileTable's shared_ptr semantics, verified in source review. CI [39650536464](https://github.com/Omeruno/switch-recompiler/actions/runs/39650536464) queued when recorded.
- Wait-correctness release `98ed113` all four CI jobs completed green at [39649844525](https://github.com/Omeruno/switch-recompiler/actions/runs/39649844525). The `gui_thread_wait_lifecycle.c` integration test first introduced at `87d0312` also had full green CI [39649623623](https://github.com/Omeruno/switch-recompiler/actions/runs/39649623623).


## 2026-10-21 resumed shift: shared HANDLE objects and thread-exit lifecycle

Read this entry before the older chronological checkpoints above. The previous
ARCHITECTURE.md was v1; it is now preserved as archive/ARCHITECTURE_v1.md and
replaced by a v2 map. ROADMAP.md's obsolete immediate queue has been refreshed.
Historical WIN32_FOUNDATION observations are explicitly marked historical.

Implemented together in this batch:
- `runtime/handle_table.*`: Process-owned typed table for File/Thread/Event/
  Mutex/Semaphore. Each HANDLE owns a shared reference; duplicates get unique
  monotonically increasing values in reserved 0xD0000100..0xDFFFFFF8 range.
  FileTable is now a facade over it; standalone/v1 HLE has an isolated fallback.
- Removed global sync-object map and synthetic thread-id HANDLE set. Thread
  handles resolve objects, not encoded IDs. DuplicateHandle supports current-
  process file/sync/thread aliases and current-thread pseudo-handle conversion,
  source-close transfer, close-only form, and close-source on a failed duplicate.
  Source/target process validation rejects foreign handles. NULL destination
  retains documented legacy duplicate-without-return behavior.
- Signaling uses every alias; closing one alias wakes only waiters on that
  closed number without destroying other references. Auto-reset and semaphore
  consumption still happens only after complete wait-all readiness preflight.
  Wait-all rejects aliases to the same underlying object before consumption;
  wait-any retains lowest-index semantics.
- Scheduler exit observer signals every HANDLE for the exiting guest and
  abandons its owned mutexes. WAIT_ABANDONED is propagated by single/any/all
  waits; partial wait-all does not take ownership or clear abandonment.
- ExitThread on the initial guest no longer aborts the host. Workers continue;
  host/Switch loops distinguish all-exited from deadlock and finish on last
  thread exit. Entry-return/ExitProcess remain process-wide exits.
- Console output recognizes duplicate console HANDLEs by object type.
- Host suite compiles common runtime objects once per invocation, linking them
  directly into each harness (retaining static HLE registration); it rebuilds
  them each time so edited headers cannot leave stale cached code.

Local validation before upload:
- `CXX='g++ -fno-rtti -fno-exceptions' sh tools/run_host_tests.sh`: scheduler,
  five original x86 tests and new `test_handle_objects` all PASS. MinGW was
  initially absent from PATH, so the guest section was then run separately
  from the same script against the same compiled objects with the extracted
  i686 MinGW GCC 13 Win32 toolchain.
- Complete guest section PASS: 26 Windows EXEs (interactive dialog / SHELL32
  handled in their dedicated scenarios), both thread probes, CRT/C++/DLL/SEH,
  all thread/wait tests, and scripted Open/List/Save plus SHELL32.
- New `gui_thread_handles.c` PASS: event alias wake, thread alias survives source
  closure, shared semaphore tokens, wrong-type rejection, duplicate-object
  wait-all rejection with no token consumption, recursive mutex abandonment
  in wait-any/all, close-source including failure, file-position sharing,
  last-reference delete-on-close and duplicate-console writes.
- New `gui_thread_main_exit.c` PASS: worker observes initial thread exit code
  73 and survives even after its own HANDLE is closed; last thread returns 0.
- New deterministic host `test_handle_objects.cpp` PASS: HLE stdcall stack/PC,
  pending retry, alias wake, object destruction, Process table isolation and
  thread-exit callback/abandonment. Uses no RTTI or host C++ exceptions.
- Local CPU oracle cannot compile because the environment lacks 32-bit libc
  headers (`bits/wordsize.h`). No local devkitA64/libnx. Full CPU oracle and
  Switch/JIT builds MUST be read from CI for this batch's exact resulting SHA.
  No new physical Switch validation; preserve Paint baseline `2c9a1fa` NRO.

Test-environment notes: local GCC MinGW POSIX variant fails pre-existing CRT
TLS/C++ cases, while the Win32 variant used by CI passes. Rerunning the file
picker scenario with a pre-existing `build-host/guest/My Documents` directory
created by gui_shell32 makes its semantic first-row click select a directory.
Use a fresh generated guest working directory when repeating that scenario;
this is not evidence of a runtime regression. Never delete user program data.

Remaining contracts: nested callbacks still execute synchronously and cannot
safely park the guest with an outer native callback stack alive. Prioritize a
reproducible WndProc+worker wait test and resumable callback design next.
Thread stack/TEB/TLS storage is retained until Process destruction; full
DLL_THREAD_ATTACH/DETACH, named objects, access-mask enforcement, inheritance,
protect-from-close, APC, MsgWait* and isolated per-thread USER32 queues remain.
GetCurrentProcess pseudo-handle as a source object is not implemented (only
current-thread conversion); cross-process duplicate is unsupported. Standard
console HANDLE closure retains the old runtime policy. Active-wait handle
closure returns WAIT_FAILED defensively; Windows defines that case as undefined.
Do NOT call this complete NT object/security or thread-lifetime support.
JIT stays an independent probe, with no x86 dynarec integration.

Contract references: Microsoft Learn DuplicateHandle / WaitForMultipleObjects
and Microsoft's Old New Thing 2011-02-25 duplicate-object wait-all explanation.


## 2026-10-21 saved native continuations for nested guest callbacks

Incoming HEAD `3a2902d` was fully green (run 39655096557). This batch replaces
recursive peer-pumping with one saved native stack per guest thread.

- New `runtime/host_fiber.*`: stackful context, same host OS thread. x86 Linux
  uses ucontext; AArch64 uses an AAPCS64 context switch saving x19-x30, SP,
  d8-d15, FPCR and FPSR. Main native stack 2 MiB; workers 512 KiB. Canary and
  safepoint reserve check; no Horizon guard-page claim. Finished native stacks
  are released only after returning to scheduler, never while executing on them.
- New `runtime/guest_execution.*`: runs existing GuestScheduler-selected guests
  on these stacks. Nested callback checkpoints honor waits, Sleep, suspend,
  explicit yields and ~256-operation quanta. Host errno, USER32 GuestCtx and
  g_tebAddr are restored after resumption. Switching cannot re-enter a parked
  guest's HLE import while its native caller frames are still active.
- Main Switch/winrun loops both use GuestExecution. Existing old probes remain
  independent. Unscheduled loader initialization / standalone v1 callbacks keep
  their existing synchronous behavior. No new DLL notification implementation.
- Callback nesting depth is per GuestThread. Active callbacks force FS/GS to
  their actual guest owner even for local SEH CpuState copies. TryHandleHostCall
  scopes USER32/TEB bindings and suppresses normal RET after nested ExitThread.
- GetMessage waiting with live peers yields instead of indefinitely blocking
  the native host. Legacy MFC/GetMessage/modal loops also have checkpoints.
  Session shutdown drains pending continuations through ordinary native returns
  under a temporary process-exit flag, releasing RAII objects. Guest code after
  a nonreturning ExitThread is not executed.
- Host tests use a fresh generated guest-run directory per invocation. This
  fixes repeat-run contamination from SHELL32-created My Documents; no user
  program data is removed.

Evidence before upload (read exact resulting SHA CI separately):
- Old baseline winrun did not complete gui_thread_callback_wait within 15 sec
  (timeout status 124). With continuations the same mutual-wait case passes.
- test_host_fiber: 20,000 alternating stack switches, nested local/RAII state,
  FP rounding environment and clean completion. CI additionally builds it for
  Linux AArch64 and executes under QEMU, including pinned x19/d8 checks; the
  AArch64 switch is identical to the one used in the NRO.
- test_guest_execution: actual x86 callbacks on local CpuState copies, mutual
  event waits, current TEB/FS/errno, root GuestCtx restoration, and draining a
  suspended callback on stop without executing guest instructions after wait.
- gui_thread_callback_wait: two own-window WndProcs wait on each other; TLS and
  IDs remain independent; nested GetMessage receives a worker PostMessage;
  nested guest-only spin yields to a worker; finite callback wait times out.
- gui_thread_callback_exit: initial thread ExitThread from WndProc signals its
  real HANDLE, unwinds native frames, and leaves the survivor worker alive.
- gui_thread_seh_wait: both guest SEH handlers park while their dispatcher CPU
  copies and active-frame scopes remain alive; both resume and restore context.
- Local full host suite with -fno-rtti/-fno-exceptions passes, including original
  x86 tests, CRT/C++/DLL/SEH/thread cases and scripted COMDLG Open/List/Save.
  Recheck final full-suite result and ARM64/Switch/CPU-oracle CI before claiming
  the batch verified. Physical Switch/actual Paint binaries unavailable here.

Remaining: physical Horizon regression (preserve known-good NRO), guest stack/
TEB/TLS reclamation and DLL thread attach/detach, per-thread USER32 queues and
cross-thread SendMessage ownership/routing, APC/MsgWait and loader lock. GUI
objects are not yet protected against arbitrary peer destruction while a native
callback retains references. Blocking filesystem/native library calls are not
made asynchronously by this change. ARM64/QEMU proves register/stack mechanics,
not libnx TLS/newlib behavior or actual Switch rendering. JIT remains separate.

## Thread resource reclamation (incoming baseline aed2a39)

- GuestThread records host-owned guest allocation ranges: stack, TEB, TLS
  vector and static TLS blocks. Main-thread TEB is reclaimed separately from
  the neighboring process-owned PEB page. Guest-writable TLS/TEB pointers are
  never used to discover ownership during cleanup.
- GuestExecution reaps exited continuations at scheduler/root-stack boundaries.
  A parked native stack is first resumed with Exited status so checkpoints
  unwind wrappers normally. Only after fiber completion are guest mappings
  unmapped and the continuation record erased. Never-started suspended workers
  are reclaimed without allocating a native stack. Destructor also reaps exited
  threads after draining pending native frames.
- GuestThread metadata remains in Process/Scheduler for open/duplicated thread
  HANDLEs and exit codes; metadata garbage collection is a separate task.
- Native HostFiber already released its stack on ordinary completion before
  this batch. This batch removes retained continuation metadata, handles parked
  exits promptly, and adds guest-memory reclamation.
- test_thread_resources uses an actual generated MinGW PE containing static
  TLS: 256 create/exit cycles, reused stack/TEB addresses, all owned pages
  unmapped, native RAII sees mappings before cleanup, retained HANDLE aliases,
  externally exited parked worker, unstarted suspended worker, and main-thread
  exit preserving PEB. Runs in run_host_tests.sh after guest fixture build.
- DLL thread notifications/TLS callbacks, loader serialization and dynamic
  static TLS for existing workers remain unfinished. SetupStaticTls avoids
  writing into a reclaimed main-thread TLS vector after initial-thread exit.
- Local full host suite passed with -fno-rtti/-fno-exceptions, including guest
  CRT/C++/DLL/SEH/thread and COMDLG scenarios. After the destructor reaping
  addition, rebuilt guest_execution.o and reran test_guest_execution and the
  final test_thread_resources: both passed. Final resource test also corrupts
  guest TLS slots to reference the main TEB, checking that cleanup follows
  host ownership, and tests an exited parked continuation on session teardown.
- Check the exact resulting SHA's CI separately. Physical Switch/Paint
  regression still requires hardware.

## DLL/TLS thread lifecycle and cooperative loader serialization

Incoming baseline: 9e434f0 (thread mapping reclamation), CI 39660483591 green.

- GuestThread.needsThreadAttach is consumed on the worker's first native fiber
  entry, before its LPTHREAD_START_ROUTINE. CREATE_SUSPENDED delays callbacks.
  TLS callbacks precede DllMain within a module; modules follow completed
  initialization order on attach, reverse order on detach. EXE receives TLS
  callbacks too. Thread callback DllMain return values are ignored.
- ExitThread/worker return request exit, making checkpoints unwind the original
  native guest-call wrappers. GuestExecution then enters a detach phase on the
  same guest fiber, allowing DLL/TLS guest code while preserving the original
  requested exit code. Scheduler.Exit, HANDLE signal, mutex abandonment and
  memory reaping happen afterward. ExitThread during detach aborts further
  callbacks rather than recursively detaching. Direct Scheduler.Exit remains
  forceful with no guest notifications; standalone unscheduled HLE retains its
  immediate-exit fallback for old host harnesses.
- Process::LoaderLock is recursive per guest ID, uses internal wait token
  0xfffffff1 (not a public HANDLE), and parks native continuations cooperatively.
  The lock spans HLE LoadLibrary (mapping + initialization), Process::LoadLibrary,
  module initialization, GetProcAddress, GetModuleHandle, DisableThreadLibraryCalls
  and thread notifications. A checkpoint precedes the lock ownership test;
  no yield is permitted between finding it free and assigning the owner.
- Modules distinguish initializing, initialized, failed and disabled-thread-calls.
  initialize-order snapshots survive nested LoadLibrary/vector growth. Callbacks
  execute on local CPU copies/current TEB; the caller's entry EIP/ESP/registers
  remain intact. Static process loads pass nonzero DllMain reserved; dynamic
  loads and thread notifications pass zero. Failed initializations cannot be
  reported as successful by a subsequent LoadLibrary/GetModuleHandle.
- Runtime static TLS setup now populates all live guest TLS vectors, including
  existing suspended workers. Each owns a fresh template + zero-fill mapping;
  existing threads receive no retrospective THREAD_ATTACH. Reclaimed main TEB/
  TLS is never touched. DisableThreadLibraryCalls rejects invalid/EXE handles
  and modules with a static TLS directory, and suppresses thread callbacks for
  eligible DLLs.
- tests/guest/gui_thread_dll_lifecycle.c + six CRT-free lifecycle_*.dll fixtures:
  dependency TLS/DllMain order; real PE TLS templates and zero-fill; EXE callbacks;
  suspended start; two concurrent runtime loads (one PROCESS_ATTACH); recursive
  LoadLibrary; Sleep inside DLL callbacks with peers attempting loader entry;
  independent dynamic TLS for existing threads and fresh TLS for later workers;
  DisableThreadLibraryCalls; failed DllMain/dependent module/repeated load/GetModuleHandle;
  normal worker return, ExitThread inside WndProc, initial-thread ExitThread;
  checks inside every detach that current exit code is STILL_ACTIVE and self
  HANDLE is nonsignaled. Survivor checks initial detach before handle signal.
  Failing guest prints its stage and full notification log for diagnosis.
- The fixture compiles only project-owned code; no Microsoft binaries added.

Validation: full host suite passed with -fno-rtti/-fno-exceptions, including
resource reclamation, all CRT/C++/SEH/thread guests, the lifecycle test, and
COMDLG Open/List/Save. The first pass used older kernel32 objects during an
in-progress edit; GetModuleHandle(failed) test caught the missing guard.
After the full pass, propagated dependency initialization failure to the parent
module and added lifecycle_parent.dll coverage; rebuilt process.o/winrun and
the expanded guest passed against those final objects. Exact SHA CI and
physical Horizon/Paint verification are separate gates.

Remaining: physical Horizon/Paint checks; per-thread USER32/message routing,
APC/MsgWait; full DLL unload/refcounts and failed-load mapping rollback;
PROCESS_DETACH/process shutdown; TerminateThread. Startup module initialization
before GuestExecution is synchronous (no blocking inter-thread waits in startup
DllMain). Failed module mappings/TLS allocations remain owned until thread/
Process teardown; this is not a full Windows loader/unloader implementation.

## USER32 v2 thread queues and sent-message ownership

Incoming baseline: 7fd25a3, CI 39664716820 green (DLL/TLS thread lifecycle).

- New user32_thread.cpp; Window.threadId identifies its creator. State.queues
  maps IDs to stable ThreadQueue records: posted/input/sent, receiving stack,
  timers, keyboard snapshot, quit flag/code, last MSG and timer allocator.
  USER32 API ENTER and window creation create queues lazily. PostThreadMessage
  fails ERROR_INVALID_THREAD_ID for absent/closed queues; HWND posts route to
  their owner, NULL HWND to caller. GetWindowThreadProcessId returns real owner.
- FetchMessage pumps incoming sent requests before matching posted/input;
  WM_PAINT and timers are restricted to caller's windows/queue. WM_QUIT bypasses
  filters; PM_NOREMOVE preserves it. Menu/list/dialog/COMDLG modal loops and
  TranslateMessage use caller-local quit/key/post state. Platform input routes
  to HWND owner's input queue; async keys/focus/active/capture remain global.
- Cross-thread SendMessage enqueues a shared SentMessage and parks sender's
  native continuation. Internal scheduler token 0xfffffff2 wakes send waiters
  on new sent messages or completion (wake-all, each rechecks its own request).
  Receiver runs WndProc only at message retrieval or its own send-wait pump,
  never because arbitrary kernel waits/Sleep execute guest window code.
  Blocking sends process incoming sent requests by default, allowing reciprocal
  calls. Posted messages remain untouched. CallProc uses a local CpuState for
  guest WndProc so reentrant delivery cannot corrupt the caller HLE's args/PC/SP.
- Receiving context is per queue and LIFO. InSendMessage/Ex and ReplyMessage
  support early synchronous reply; final handler return cannot overwrite it.
  Cross-thread SendNotifyMessage queues an asynchronous request. Finite
  SendMessageTimeout parks with guest-clock timers and supports SMTO_BLOCK;
  unstarted timed-out requests are skipped, running ones continue. Same-thread
  calls remain direct; HWND_BROADCAST uses a stable HWND snapshot.
- ExitThread's clean detach phase now destroys own windows before DLL/TLS
  detach. Scheduler exit closes its GUI queue and fails pending/active sends.
  RetireWindow cancels queued sends and removes posted/input for stale HWNDs.
  Foreign DestroyWindow fails. Forceful Scheduler.Exit cannot execute guest
  callbacks, but removes remaining owned HWNDs and wakes blocked senders.
- WindowCallScope spans every native HLE call and CallProc/cleanup. Logically
  removed Window and WndClass objects are retained until all native HLE/window
  frames return, protecting raw Window*/Ctl* across reciprocal callbacks and
  peer destruction. UnregisterClass removes class lookup immediately but defers
  physical destruction. This is conservative retention, not full GDI/hook
  lifetime protection; a long-lived modal HLE can retain retired objects.
- gui_thread_messages: absent/closed queue errors, own posted messages survive
  peer retrieval, thread WM_QUIT and filters/PM_NOREMOVE, independent timers and
  keyboard state, real HWND owner, foreign destroy rejected, owner exit removes
  HWND. gui_thread_send: reciprocal Send with correct owner TID/TLS, sender's
  posted queue preserved, InSendMessageEx/ReplyMessage, async notify, timeout
  cancellation while target kernel-waits, SMTO_BLOCK and delayed reentrant pump,
  nested BUTTON -> foreign parent -> destroy BUTTON while native frames live,
  ExitThread from target WndProc and stale HWND failure. gui_thread_send_exit:
  owner kernel wait does not dispatch posted/sent requests; owner return destroys
  its window on owner CPU and releases another sender's infinite wait.
- Full local suite passed with -fno-rtti/-fno-exceptions, all three new guests,
  DLL lifecycle, original x86/native harnesses, CRT/C++, SEH/thread/resource
  reclamation and scripted COMDLG Open/List/Save. Exact commit CI and physical
  Horizon/Paint validation must be checked separately.

Remaining: real MsgWait HANDLE+queue and APC/alertable waits; per-thread input
focus/capture/hooks/menu ownership and AttachThreadInput; accurate queue change
bits, extended SMTO hung rules, full system-message marshalling; full process/
DLL shutdown and unload. Timeout accepts NORMAL/BLOCK/ABORTIFHUNG only, with
ABORTIFHUNG using the same deadline (no separate hung detector). Existing
MsgWait* exports still have partial legacy behavior and native sleeps; do not
claim their kernel-object semantics implemented. v1 loop remains separate.

## Current package: MsgWait and user APC / alertable waits

This section supersedes the preceding MsgWait/APC limitations, not the
remaining input ownership, loader/shutdown or HANDLE policy limitations.

- Shared kernel_wait helpers validate HANDLE lists and atomically preflight /
  consume thread/event/mutex/semaphore waits. Existing nonalertable multiple
  waits retain their retry/deadline behavior. WaitForMultipleObjectsEx is now
  registered; alertable single/multiple waits and SleepEx preserve a native
  continuation with an absolute guest-clock deadline. Timeout/spurious wake
  cannot restart the original duration. SleepEx(INFINITE,TRUE) has no deadline.
- UserApc FIFO belongs to GuestThread. QueueUserAPC resolves thread aliases /
  pseudo handles, rejects invalid types (6) and exiting/exited targets (31).
  Only an alertable target is interrupted; InterruptWait wakes that thread
  without signaling or consuming its waited object and respects suspendCount.
  Before-start APCs run after worker DLL/TLS attach and before user entry.
  Callback uses local CpuState and receiver TEB/TLS; normal waits inside an
  APC are nonalertable, explicit nested alertable waits support recursion.
  Delivery drains FIFO and returns WAIT_IO_COMPLETION (192). ExitThread /
  scheduler exit discard pending APCs before detach/reclamation.
- MsgWaitForMultipleObjects/Ex support 0..63 HANDLEs, masks, WAITALL (1),
  ALERTABLE (2), INPUTAVAILABLE (4), lowest ready handle and abandoned indexes.
  WAITALL requires selected queue input AND all handles before consuming any
  object. Queue-only success is nCount. Messages remain queued; this API does
  not dispatch sent requests. Peek/Get pump them later on the owner's CPU.
  WaitMessage uses the same queue-only wait and ignores previously checked
  unread input until something new arrives.
- ThreadQueue tracks selected-category change bits separately from available
  input. Post/quit/sent and key/mouse arrival mark changes; timer/paint readiness
  transitions announce once until retrieved/validated. GetQueueStatus returns
  high-word availability / low-word changes, clears requested changes; Peek/Get
  mark observed input old. INPUTAVAILABLE can wake for old unread input.
  Modal quit reposts and TranslateMessage also maintain change bits.
- MsgWait polls frontend nonblocking and yields through scheduler.Sleep(1),
  allowing platform input even when all guest CPUs wait. This is cooperative
  periodic polling, not a fully event-driven host integration. Standalone HLE
  fallback may sleep the host; real GuestExecution does not.
- New gui_thread_msgwait guest covers HANDLE/input indices, validation,
  GetQueueStatus/old input, atomic WAITALL, abandoned mutex, delayed signals /
  posts, timer categories, sent wake without dispatch, and WaitMessage.
  gui_thread_apc covers suspended-start FIFO, owner TLS, blocking APC body,
  pending APC during ordinary wait, infinite SleepEx interruption, object /
  wait-all / MsgWait interruption without consuming tokens, nested APC, exit
  before entry, dropped callbacks and wrong/closed/exited HANDLE errors.
  Additional finite alertable timeout / nonalertable Ex checks are included.
- Full local suite passed with -fno-rtti/-fno-exceptions, native/x86 harnesses,
  all MinGW guests, lifecycle/SEH/resource tests and COMDLG Open/List/Save.
  Check exact new commit CI separately; no physical Switch validation yet.

Remaining: named objects and access/inherit/protect HANDLE policies; I/O
completion APCs, waitable timers and SignalObjectAndWait; full Windows
queue-change history/raw input/system activation; input/focus/hooks ownership;
full process/DLL shutdown. v1 message loop remains separate. ROADMAP stage C
is approximately 70% by functional blocks, not a time estimate or completion
claim. Keep reporting an approximate current-stage percentage in final work
reports as requested by the user.

## Current package: named synchronization objects and HANDLE policies

Supersedes the preceding named-object / per-HANDLE access limitations within
the SINGLE emulated Process. Prior MsgWait/APC commit `4adf937` has green exact
CI `39694866338` (5 jobs); NRO artifact `12390755066`. New package needs its own
exact-SHA CI and physical Switch test. Paint hardware baseline stays `2c9a1fa`.

- HandleTable entries now contain object shared_ptr, granted access and flags.
  Add defaults to supported type access (XP THREAD_ALL_ACCESS, not Vista),
  standard streams have directional access. Alias iteration / object enumeration
  preserve identity and wake behavior. FileTable remains the CRT facade.
- New kernel32_handles.cpp owns Create/Open Event/Mutex/Semaphore A/W exports
  (old create registrations removed), Get/SetHandleInformation. Named lookup
  is a weak map owned by Process HandleTable; last reference expires the object,
  names do not keep objects alive and stale entries are pruned on bind/lookup.
  Case-sensitive UTF-16 keys, ACP1252 A conversion (including 0x80 Euro), exact
  W Unicode including surrogate pairs. Local/default aliases and distinct
  Global prefix; reject invalid backslashes/nested prefixes, overlong or unmapped
  names. This is NOT IPC or a true global/session Horizon object manager.
- Repeated Create returns a fresh handle and ERROR_ALREADY_EXISTS without
  resetting event mode/state, acquiring mutex or changing semaphore count/max.
  Conflicting type returns 6, missing Open returns 2. Open uses requested
  mapped access and inherit argument. Unsupported access bits fail 5.
  MAXIMUM_ALLOWED maps to supported type rights under current no-DACL model.
- SECURITY_ATTRIBUTES is read as mapped 12-byte x86 layout; bInheritHandle
  feeds named/unnamed sync objects, thread and file creation. Security descriptor
  remains ignored (no tokens/ACLs). INHERIT metadata is queryable/changeable;
  no CreateProcess handle-transfer mechanism is claimed.
- Get/SetHandleInformation operate on individual entries. Masked updates
  preserve other flags. CloseHandle of protected reference returns 5, leaves
  object and waiters intact. Ordinary duplicate takes explicit inherit and
  clears protection. Runtime rejects DUPLICATE_CLOSE_SOURCE of protected source
  with 5 and publishes no alias: defensive policy, not full NT/debugger exception
  fidelity. Existing close-source-on-other-error and close-only behavior remain.
- Duplicate SAME_ACCESS copies actual source access; explicit desired access
  uses generic mapping. File aliases cannot exceed source rights. Sync/thread
  explicit rights can differ within supported masks; DACL enforcement absent.
  Zero-access handles remain valid for flags/close but cannot wait/mutate.
- All object wait paths preflight SYNCHRONIZE before consumption, including
  alertable and MsgWait. Event mutation / ReleaseSemaphore require bit 2.
  ReleaseMutex checks owner only; MUTEX_MODIFY_STATE is reserved, not required.
  Thread exit query uses QUERY_INFORMATION, suspend/resume SUSPEND_RESUME,
  QueueUserAPC SET_CONTEXT; current-thread pseudo handle retains full access.
- File Read/Write data, SetEndOfFile, WriteConsole and CRT descriptor read/write
  enforce entry access plus shared OpenFile capabilities. Append-only HANDLE
  writes at EOF even after shared position is changed. CONIN$/CONOUT$ opens get
  independent HANDLE metadata. Legacy standard-stream Close convention remains.
  File metadata/legacy APIs have not all been audited for NT security fidelity.
- gui_thread_named tests A/W/ACP/Unicode identity, namespaces and type collision,
  Create initialization preservation, worker Open/wake, lifecycle across aliases,
  invalid names/masks and permission-denied wait-all preserving object tokens.
  gui_handle_policy tests independent flags, protected close, duplicate rights,
  read/write/zero-access aliases, CRT bypass rejection, append-only EOF, console
  creation attributes, thread query/suspend/APC rights and pre-entry APC delivery.
  Native test_handle_objects also checks isolation of named maps across Process
  instances and object expiration after last handle.
- Final full local suite passed with -fno-rtti/-fno-exceptions after all source
  changes: native/x86 harnesses, all MinGW guests including both new tests,
  original APC/MsgWait/lifecycle/SEH/resource regressions and COMDLG Open/List/Save.
  Local log: /tmp/switch-named-policy-final.log (transient, not an artifact).

Remaining stage C: waitable timers, SignalObjectAndWait, I/O completion APCs,
more contention/edge tests and physical Switch acceptance. Full IPC, child
inheritance and token/DACL enforcement require the future process model;
per-thread USER32 input/focus/hooks and process/DLL shutdown remain separate.
Stage C estimate is now ~80% by functional blocks, not time. The requested
overnight automation was created earlier for eight hourly continuation runs;
do not duplicate or extend it. Read current ROADMAP before choosing next work,
so a scheduled run does not repeat this named-object/HANDLE package.


## 2026-10-22 next package: SignalObjectAndWait

Incoming exact HEAD `788174988754d706cd5ff0ad8693516f0c21e546` was clean and
unchanged remotely; exact CI [39696366060](https://github.com/Omeruno/switch-recompiler/actions/runs/39696366060)
passed all five jobs. No unfinished local edits, active build/test process or
in-progress workflow was found before starting. No additional AGENTS.md or
CLAUDE.md instructions were present in this checkout/parent directories.

- Implemented KERNEL32 SignalObjectAndWait (16-byte stdcall) using shared
  event/mutex/semaphore signal helpers, followed by the existing saved native
  object-wait loop. A new enableApcs argument disables delivery for ordinary
  combined waits; all previous callers retain alertable behavior by default.
- Signal is emitted once before APC dispatch/acquisition; the native frame
  survives every park, so retries/timeouts cannot re-post a semaphore or
  re-release a mutex. Pending APC returns WAIT_IO_COMPLETION only when requested.
  ReleaseSemaphore overflow now uses ERROR_TOO_MANY_POSTS (298); nonpositive
  counts still use ERROR_INVALID_PARAMETER. Updated the older guest expectation.
- Preflight validates wait HANDLE/type/SYNCHRONIZE before signal mutation.
  Event/semaphore signal requires MODIFY_STATE; mutex uses owner/recursion.
  Invalid wait preflight with no mutation is a defensive runtime policy,
  not a claim of complete NT error-precedence/ordering fidelity. Closed wait
  HANDLE returns WAIT_FAILED under the existing defensive policy. No promise
  of cross-processor atomic signal/acquisition is made.
- Drop the signal object shared_ptr before parking: a waiting caller must not
  keep a closed signal HANDLE's named object alive. Wait-side revalidation and
  absolute deadline use the shared wait loop. Standalone HLE without a native
  continuation only polls, matching the existing unscheduled wait limitations.
- Added gui_thread_signalwait guest regression: aliases/same-object waits,
  recursion, wrong owner/type/rights, wait preflight preservation, semaphore
  overflow, 32 blocking handshakes, exactly-once count after timeout, pending
  APC signal-before-callback order and ordinary APC deferral, wait HANDLE
  closure and abandoned mutex. Auto-discovered by tests/guest/build.sh.
- Added test_signal_wait native continuation test to mandatory host suite:
  actual parked wait, exactly-once count, finite timeout, signal HANDLE closure
  and weak named-object expiration while caller is still waiting. Expanded
  test_handle_objects with direct stdcall and immediate signal/wait checks.
- Local native/x86 host suite passed with -fno-rtti/-fno-exceptions, including
  the new direct HANDLE ABI assertions. New test_signal_wait also built and
  passed independently against the same runtime objects. Final mandatory host
  script was rerun after all source/test-list changes and passed, including
  test_signal_wait (MinGW guest stage still skipped as described below).
- Local environment has no MinGW compiler or 32-bit libc headers: MinGW guests
  were skipped locally and CPU oracle could not compile (bits/wordsize.h).
  Package installation was unavailable (dpkg lock held outside visible PID
  namespace). GitHub Actions installs these dependencies; verify the new
  commit's exact head_sha and all five jobs before calling this package green.
  No new physical Switch or proprietary Paint binary testing was performed.

Next: waitable timers (typed/named HANDLE, scheduler deadlines, manual/auto reset,
Set/Cancel and timer APC cancellation/lifetime contract), then contention/edge
regressions and hardware acceptance. I/O completion APCs, IPC/token/DACL,
input/focus/hooks ownership and full process/DLL shutdown remain separate.
Stage C remains approximately 80% until the timer block and acceptance gates
advance; this smaller completed block does not automatically raise the estimate.
Do not create/extend automation, switch branches, or advance JIT/GPU/shell gates.


## 2026-10-22 current package: waitable timers and timer APC lifecycle

Incoming branch HEAD `3bd6ff315a7c78c4e826a67285735c87e318a0b5` was clean and
matched remote. Exact [CI 39701381134](https://github.com/Omeruno/switch-recompiler/actions/runs/39701381134)
passed all five jobs, including native/MinGW SignalObjectAndWait, COMDLG and CPU
oracle. No in-progress workflow, unfinished changes, competing build process
or additional repository AGENTS.md/CLAUDE.md was found before starting.

- Added typed TimerObject and weak per-HandleTable timer registry. Shared named
  Create/Open A/W code covers timers, preserving reset mode on repeated Create,
  Local/default/Global and namespace type collisions, access/inherit/protect and
  DuplicateHandle policies. TIMER_MODIFY_STATE for Set/Cancel, SYNCHRONIZE for
  waits. Timers cannot be used on the signal side of SignalObjectAndWait.
- SetWaitableTimer/CancelWaitableTimer implement relative/absolute/periodic
  deadlines, manual/auto reset, parameter validation and optional timer APC.
  Set resets signaled state/cancels old APC; Cancel preserves signaled state.
  Relative signed 100-ns input rounds upward to us without negating INT64_MIN.
  Absolute FILETIME uses live UTC wall clock, so external clock adjustments are
  observed. Periodic deadlines retain phase and coalesce missed expirations.
- Clock::FileTime includes fast-forward skew; BOTH legacy and v2
  GetSystemTimeAsFileTime now share it (legacy previously used a fake date).
  Relative Clock stays monotonic. Real power-state behavior is unverified.
- Process installs scheduler poll/pending hooks, including idle infinite waits.
  Wait preflight/APC dispatch also poll expiry. All common single/multiple,
  alertable, MsgWait and SignalObjectAndWait consumers use the existing shared
  transaction layer; incomplete wait-all must not consume an auto timer.
- Timer APC passes three stdcall arguments (data, UTC signal FILETIME low/high)
  on setter CPU/TEB/TLS, only during alertable delivery. One pending APC per
  timer; periodic signals do not accumulate count/backlog. Weak source and
  generation reject stale notifications. Cancel/rearm removes the old source's
  queued APC without touching ordinary QueueUserAPC entries. Last Close removes
  queued callbacks and timer registry/name do not retain the object.
- Setter exit cancels its callback timers, retaining signal state; a timer
  armed without callback survives setter exit. Source object strong references
  are released before guest callback execution, allowing reentrant close/rearm.
- Fixed a real new cancellation race in shared DispatchUserApcs: Checkpoint can
  yield after the nonempty queue check, then a peer cancels/removes the timer
  APC. Recheck before front/pop; do not dereference an emptied deque. Native
  regression forces precisely this interleaving, with no timing-based guess.
- New test_waitable_timer native HLE/x86 callback regression covers names/mode,
  weak expiry, access/errors, relative/absolute/periodic/reset/cancel, coalescing,
  12-byte APC ABI/timestamp/TEB, setter exit, last close, idle infinite wait and
  cancel-at-dispatch race. Registered in mandatory run_host_tests.sh.
- New gui_thread_timer MinGW guest covers A/W namespaces/rights/inherit,
  alias consumption, manual/auto reset, infinite/ALL/MsgWait/combined waits,
  absolute future/past, periodic reactivation, setter TLS/context/timestamp,
  APC coalescing and selective cancel/rearm/close, setter-exit/no-callback case.
- Final mandatory native/x86 suite passed with -fno-rtti/-fno-exceptions after
  all source changes, including timer APC cancellation race, INT64_MIN relative
  due-time and fResume unsupported-power behavior. Log (transient only):
  /tmp/switch-timer-final.log. Local MinGW/32-bit libc dependencies remain absent:
  guest/oracle/Switch/ARM64 validation must be verified in exact new-SHA CI.

Limits: cooperative quantum/1-ms idle polling, not a 100-ns accuracy promise;
no Horizon hardware or suspend/resume test, no actual wake from suspend
(fResume returns success with ERROR_NOT_SUPPORTED), no XP low-power timing
parity claim, no Vista+/high-resolution/Ex or threadpool timer APIs. No I/O
completion APCs, IPC/child inheritance/token/DACL expansion. Standalone HLE
without Process cannot schedule timer APC; unscheduled waits remain poll-only.

Next justified stage-C package: contention/edge regressions (competing timer
waiters, wait-all token/deadline preservation, callback cancel/rearm/close,
retargeting callback owner, shutdown/cancellation boundaries) and collect
physical Switch acceptance separately. Input/focus/hooks and full process/DLL
shutdown remain separate contracts. Stage C approximately 90% by functional
blocks after the timer block, with hardware acceptance still open. Do not
advance JIT/GPU/shell gates or create/extend scheduled runs.


## 2026-10-22 current package: wait retry isolation and timer contention/APC edges

Incoming exact HEAD `b96061e6f69cb059145f9e1bc909a0474dcb1d37` was clean and
matched remote; [CI 39708466163](https://github.com/Omeruno/switch-recompiler/actions/runs/39708466163)
passed all five jobs. No active workflow, competing compiler/test process or
unfinished edits were found; no additional AGENTS.md/CLAUDE.md exists in checkout.

- Reproduced a correctness bug in terminal object-wait cleanup BEFORE patching:
  main parks wait-all([auto event A, event B], finite); peer closes B, waking
  caller; retry fails validation with ERROR_INVALID_HANDLE, but old retry token
  A/deadline survives. After that deadline expires, a NEW infinite single wait
  on an already signaled A incorrectly returns WAIT_TIMEOUT instead of success.
  Native baseline log: /tmp/switch-retry-before.log (transient). This is a
  runtime defensive-close policy regression; closing an active Windows wait
  HANDLE/altering a pending wait array is not claimed as defined NT behavior.
- Introduced common ClearWaitRetry/FinishObjectWait in kernel32_sync.cpp.
  Single/multiple normal completions and errors clear retry token, original
  deadline AND timeout marker. Multiple preflight failures, scheduler park
  failures and Ex preflight failures use the same terminal path. A genuine
  pending customReturn preserves scheduler state until its original call retries.
  Alertable continuation cleanup shares the helper. Successful/timeout resource
  consumption and timeout ordering are otherwise unchanged.
- New test_wait_retry_state native HLE ABI regression first fails on b96061e
  with fresh infinite wait result 0x102, then passes with this fix. It covers
  ordinary/Ex multiple waits, duplicate-array validation failure on resume,
  independent new single wait deadline/token consumption and failed timed-out
  single wait cleanup. Registered in mandatory host suite.
- New gui_thread_timer_contention Win32 guest: three threads waiting through
  shared auto-timer aliases consume exactly one expiration each; manual timer
  wakes all three. Periodic wakeups do not renew wait-all's original deadline
  or consume timer/semaphore tokens before an unsignaled gate is ready. Tests
  both ordinary/Ex calls and a subsequent successful all-object acquisition.
  Also checks actual guest closed-wait failure followed by a fresh infinite
  single wait on the same now-ready first object.
- New gui_thread_timer_reentrant guest: timer APC cancels its own periodic
  source, rearms itself, closes its last named handle and creates a different
  object under that name BEFORE callback return (proves no retained source
  shared_ptr). Timer callback ownership can move from a nonalertable worker
  with a pending old APC to main; old notification is removed, old setter exit
  cannot cancel replacement, and callback checks main thread/TLS. Callback
  rearm test allows dispatch to drain both expirations in one alertable call,
  as well as separate calls. Finite callback waits bound regression failures.
- Mandatory local native/x86 suite passed with -fno-rtti/-fno-exceptions,
  including the original timer/SignalObjectAndWait/SEH/lifecycle regressions and
  new retry-isolation test. Log: /tmp/switch-contention-host.log (transient).
  MinGW and 32-bit libc headers remain absent locally, so guest tests and CPU
  oracle require exact new-SHA CI, together with Switch and ARM64 ABI jobs.
  Check that run before calling this package green; no physical Switch test.

Stage C remains approximately 90%: this closes a bug and extends acceptance
coverage, not a new large functional block. Remaining gates include physical
Switch acceptance and further targeted stress of mixed message/object/APC
waits, timer callback blocking/ExitThread and teardown boundaries. I/O completion
APCs, IPC/token/DACL/child inheritance, input/focus/hooks ownership and full
process/DLL shutdown remain separate; do not drift into JIT/GPU/shell or imply
hardware validation. Do not create/extend automation or change branches.


## 2026-10-22 current package: post-APC wait revalidation and mixed/exit boundaries

Incoming HEAD `0a09a4358c25053dd77b48d5550032e92b240967` was clean and matched
remote. Exact [CI 39713383159](https://github.com/Omeruno/switch-recompiler/actions/runs/39713383159)
passed all five jobs. No active workflow, competing compiler/test process or
unfinished local edits; no extra AGENTS.md/CLAUDE.md in this checkout.

- Reproduced BEFORE patch: alertable wait validates [HANDLE A, ready auto-event
  B]; its pending timer-APC dispatcher yields at a quantum checkpoint. A peer
  cancels that queued APC and closes A. Dispatch returns false (no callback),
  then the old code directly polls the previously validated set. Wait-any
  returned index 1 and consumed B despite invalid A; all/single could instead
  time out or park on a HANDLE already closed, losing its close wakeup.
  Native baseline log: /tmp/switch-apc-validation-before.log (transient),
  mode 0 returned 0x1/error 0. Closed-active-HANDLE behavior remains runtime's
  defensive policy, not a claim that Windows defines this action.
- AlertableObjectWait now checks thread/process continuation state AND validates
  all object HANDLEs again after DispatchUserApcs returns without delivery,
  before token acquisition or parking. Applies to Ex single/multiple and
  SignalObjectAndWait through the common loop. Sleep-only has no HANDLE set.
  USER32 MsgWait already validated again after its platform-processing checkpoint;
  it does not need a duplicate change. APC-delivered WAIT_IO_COMPLETION behavior
  and one-shot signal semantics are unchanged.
- New mandatory test_alertable_revalidation uses real saved native continuations
  and HLE stdcall entry points, forcing precisely the 256-operation checkpoint
  between initial validation and APC pop. Covers wait-any/all/single/combined,
  canceled callback with closed HANDLE, WAIT_FAILED/6 and preservation of ready
  auto-event tokens; combined signal still occurs once. Proven failing before
  patch, all four scenarios pass after it.
- New gui_thread_mixed_apc runs 12 Win32 mixed MsgWait/timer/APC rounds. Timer
  callback blocks in a normal kernel wait while main queues sent and posted
  messages, an ordinary user APC and signals an auto-event. Ordinary callback
  wait does not dispatch APC/messages. Main cancels periodic pending timer APCs
  and closes its last named handle while its native callback stack is parked;
  a different object can immediately reuse that name. After gate release the
  ordinary APC drains, original MsgWait returns IO_COMPLETION, event token and
  queued messages remain for explicit waits/PeekMessage, and sent WndProc runs
  on owner CPU/TEB/TLS. Finite waits bound regression failures.
- New gui_thread_timer_exit executes ExitThread(77) inside a periodic timer APC
  while owning a HWND/mutex and queuing an additional ordinary APC. Checks no
  guest instructions after ExitThread or after outer SleepEx, queued APC drop,
  owner-context window destruction/TLS, thread exit code, mutex abandonment,
  timer cancellation with retained signaled state and no later periodic fire.
- Full mandatory native/x86 local suite passed after source changes with
  -fno-rtti/-fno-exceptions, including all original SEH/continuation/timer/wait
  regressions and the new four-case revalidation test. Log (transient):
  /tmp/switch-mixed-waits-host.log. Local MinGW/32-bit libc still unavailable;
  new guests/oracle/ARM64/Switch require exact new-SHA Actions verification.
  No physical Switch or proprietary Paint testing performed in this package.

Stage C remains ~90%: acceptance coverage and correctness improved; no new
large feature block or hardware acceptance is claimed. Remaining actual gates
include physical Switch continuation/GUI/thread acceptance and targeted audit
of untested shutdown/cancellation interactions. Avoid endlessly inflating this
estimate through more tests alone. Full process/DLL shutdown and unload/refcount
contracts are still concrete next roadmap foundation work; input/focus/hooks,
I/O completion APCs and real IPC/token/DACL/child inheritance remain separate.
Do not advance JIT/GPU/shell before their acceptance gates, change branches,
create automation or extend the existing run series.


## Execution stop / process-exit cancellation boundary (2026-10-22)

Incoming HEAD 4e0335dad38f3f95e79049cc9b910f5e65b55fc0 verified with
all five jobs green in [39718964033](https://github.com/Omeruno/switch-recompiler/actions/runs/39718964033).
Remote ref matched local HEAD immediately before changes; no competing diff or
active incoming CI was present. Work stays on the agreed branch.

- Confirmed defect: destroying GuestExecution unwound parked native frames but
  left waiting/suspended threads live, with APC queues and timers still armed.
  New mandatory test_execution_stop failed on old code at HasLiveThreads.
- Last executor for a Process now drains native stacks first, cancels every
  waitable timer (including callback-free periodic timers), then exits all
  registered live threads through the common scheduler/USER32 exit contract.
  This clears waits/retry/deadline/APC state, retires native windows/queues,
  abandons mutexes and releases owned stack/TEB/TLS after native RAII returns.
  Thread HANDLE metadata, PEB and already-final exit codes remain available.
  Explicit ExitProcess/entry RET uses the requested code; host stop marks
  remaining threads STATUS_UNSUCCESSFUL (0xC0000001). Existing exit codes stay.
- Nested executors of the same Process only drain their own continuations;
  the outer owner still controls session lifetime. Real-allocation regression
  preserves this isolation and adds live parked plus never-started worker
  reclamation, checking every page and existing PEB/exit-code lifetime.
- New native test covers host stop and actual ExitProcess HLE request, RAII
  before unmap, parked/suspended/already-exited thread states, timer cancellation
  with retained signals, queued APC drop, mutex abandonment and HANDLE retention.
- New gui_thread_process_stop Win32 guest requests ExitProcess from a user APC
  while a timer callback is parked in a normal wait, another APC is pending,
  a plain periodic timer is active and a CREATE_SUSPENDED worker never started.
  No guest instruction after the exit/wait may run; native tests inspect final
  cleanup directly (a guest cannot inspect itself after process exit).
- Full local mandatory host/native/x86 suite passed with -fno-rtti and
  -fno-exceptions; targeted test rerun and resource-harness compile passed.
  Log: /tmp/switch-execution-stop-host.log (transient). Local MinGW and 32-bit
  libc remain unavailable. Win32 guest/resource execution, CPU oracle, ARM64
  ABI and Switch builds require exact new-SHA Actions verification.

This is bounded execution-session cancellation, not orderly DLL_PROCESS_DETACH,
FreeLibrary unload/refcounts or TerminateThread semantics. Those remain explicit
loader work. Stage C remains ~90%; additional boundary correctness is not a new
large feature block. Physical Switch acceptance remains unperformed. Continue
with concrete shutdown/input ownership gaps and loader foundations, without
advancing JIT/GPU/shell gates or committing proprietary Microsoft binaries.


### Verified execution-stop CI results
Implementation commit **0280f43a44b138a5decca7ff6eb375a6d7823674**: all five
jobs green in [39723997023](https://github.com/Omeruno/switch-recompiler/actions/runs/39723997023).
Fetched host-job logs confirm the new test_execution_stop, 256 real thread
reclamations plus live-stop/suspended-worker and nested-executor isolation,
and gui_thread_process_stop all PASSED; entire native/x86/MinGW guest suite
passed. CPU oracle: three 50,000-case runs, zero differences. ARM64 native
continuation ABI/QEMU, main Switch NRO and existing isolated probe NRO builds
passed. No physical Switch acceptance is inferred. This documentation follow-up
also preserves the test runner executable bit. Verify any later HEAD separately.

Next concrete foundation contract: orderly process DLL_PROCESS_DETACH and its
interaction with cancellation, loader lock and already-exited workers; do not
confuse the now-verified native abort cleanup with guest DLL process shutdown.
Unload/refcounts/rollback and input/focus/hooks ownership remain open. Stage C
is still ~90% and hardware acceptance remains a separate gate.


## Orderly process-detach boundary (2026-10-22)

Incoming HEAD **5012aedd98cb7703170c624a90a92c086cee87be**, all five jobs
green in [39724317735](https://github.com/Omeruno/switch-recompiler/actions/runs/39724317735).
Ref and clean branch/worktree checked; no active prior build or competing CI.
No proprietary binaries used. Work remains only on the agreed branch.

- Added distinct cooperative process-exit notification phase for ExitProcess,
  CRT exit and runtime entry RET observed by RunOne. First unwind all original
  saved native stacks; cancel timers/APCs and terminate peers without thread
  detach. Keep requesting thread and its stack/TEB/TLS live until process
  callbacks finish on a fresh native fiber. Then signal/reclaim requester.
- Completed initialization order is traversed in reverse once; EXE TLS and
  DLL TLS/DllMain receive PROCESS_DETACH and nonzero reserved. Thread-notification
  disabling does not suppress process callbacks. Failed/partial attach modules
  are excluded. Recursive loader queries of existing modules remain valid;
  no new modules or workers are admitted during shutdown. No unload/refcount
  or rollback contract is claimed. Sleep/finite waits can yield normally.
- Recursive exit/ExitThread during detach ends remaining notifications without
  repeating completed callbacks; first process code wins. Host abort/destructor
  remains native-only. Nested executors defer shared process teardown and
  preserve the original requester identity for the outer owner.
- Expanded mandatory native test_execution_stop: actual ExitProcess and entry
  RET from worker, parked peer, nested executors, RAII/mapping lifetime and
  finalized tombstones before reporting Stopped. Reproduced failing before
  patch (HasLiveThreads remained true); patched cases pass. Full mandatory
  host/native/x86 suite passed with -fno-rtti/-fno-exceptions; targeted expanded
  test rerun and new host-inspection harness compile passed. Local log:
  /tmp/switch-process-detach-host.log (transient).
- New source-only gui_process_detach MinGW fixture and mandatory host inspector
  exercise main APC exit, worker APC exit, CRT/main return and host-abort
  control. Inspector reads retained DLL data after termination: exact callback
  order/count, requester thread/TLS, nonzero reserved, live requester vs already
  terminated peer HANDLEs/codes, prior completed worker code, forced peers with
  no THREAD_DETACH, disabled/failed modules, recursive loader lookup, finite
  Sleep, pending APC/timer cleanup and final mapping release. Abort must have
  zero PROCESS_DETACH. Existing lifecycle fixtures extended with guarded checks.
- Local MinGW/32-bit libc remain unavailable: guest execution/resource inspection,
  CPU oracle, ARM64/QEMU and Switch builds still require exact new-SHA CI.

References used for the process/thread ordering and reserved-pointer contract:
[ExitProcess](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-exitprocess),
[DllMain](https://learn.microsoft.com/en-us/windows/win32/dlls/dllmain).
This is not full NT process shutdown. Next bounded contracts: connect the last
ExitThread/worker return and startup failure to process shutdown; then DLL
unload/refcounts/rollback. Startup blocking waits, TerminateThread, security/IPC
and physical Switch acceptance remain open. Stage C stays ~90%, without an
automatic percentage increase for another shutdown regression package.


### Verified orderly process-detach CI results
Implementation **6dcc8c42c3e5106339c848cd394853cc6e4582b5**, all five jobs
PASSED in [39730636783](https://github.com/Omeruno/switch-recompiler/actions/runs/39730636783).
Fetched host logs confirm the four-scenario main/worker APC, CRT return and
host-abort inspector PASSED, including order/count/reserved/TLS/loader/peer
state and disabled/failed DLL checks. Expanded native execution-stop/entry
RET/nested-owner regression, real thread-resource reclamation and the entire
previous native/x86/MinGW suite passed. CPU oracle: 3 x 50,000, no differences.
ARM64 continuation ABI/QEMU, Switch NRO and existing separate probe NRO builds
passed. Local worktree synchronized to that exact code tree; no physical
Switch validation claimed. The following documentation-only commit must also
receive its own exact-SHA CI verification.

Stage C remains ~90%. Next: last-thread ExitThread/worker-return and startup
failure process-shutdown bridge, followed by DLL unload/refcounts/rollback.
Keep native abort separate from orderly guest teardown and retain hardware
acceptance gate; do not advance JIT/GPU/shell or change the agreed branch.


## Last-thread process-shutdown bridge (2026-10-22)

Incoming HEAD **a39aa84cbd0d44db9e19364ce55447243db3f988**, all five jobs
green in [39730901919](https://github.com/Omeruno/switch-recompiler/actions/runs/39730901919).
Remote ref/branch matched local, worktree clean, no running build/test process
or active prior CI. No competing change or uncommitted package was present.

- Confirmed gap: last ExitThread directly finalized its HANDLE and stack/TEB/TLS
  without process detach. Extended mandatory native regression failed on old
  code expecting Stopped/global process code rather than an empty scheduler.
- GuestExecution now selects process shutdown for a last ExitThread/worker
  return before ordinary thread detach/finalization. Original native callbacks
  still unwind normally; window cleanup remains on owner. Common process phase
  retains TEB/TLS and unsignaled requester HANDLE until DLL/TLS process callbacks
  finish, preserves requested code and drops pending APC/timers/owned mutexes.
- Re-check live peers after acquiring the cooperative loader lock: a requester
  parked behind another thread's DLL_THREAD_DETACH may now be last. Skip the
  extra thread notifications and use process detach once. Created/suspended
  peers remain live; no premature whole-process termination. Native/manual
  scheduler Exit and host abort retain their existing explicit control semantics.
- New source-only gui_last_thread_exit and expanded post-exit inspector cover
  six real IA-32/Win32 scenarios: main ExitThread(55), main ExitThread(73) leaving
  a final worker, worker return(55), ExitThread inside user APC, peer wakes during
  DLL_THREAD_DETACH/Sleep while loader lock held, and CREATE_SUSPENDED last peer.
  Host inspector resumes the latter only after proving the main thread has
  exited without process detach and the suspended worker's TEB/TLS remain live.
  Exact callback order/count, nonzero reserved, owner TLS/CPU, no final-owner
  THREAD_DETACH, retained peer codes(73), final process code(55), mapping release,
  APC drop, callback-free timer cancellation and mutex abandonment are checked.
- The nonzero-exit fixture is intentionally handled by the post-exit inspector,
  not the generic zero-exit guest loop. Existing four ExitProcess/CRT/abort
  cases and old lifecycle guests remain mandatory, using the same DLL sources.
- Full local native/x86 mandatory suite passed with -fno-rtti/-fno-exceptions.
  New/extended inspector compiled locally. Log: /tmp/switch-last-thread-host.log
  (transient). Local MinGW and 32-bit libc remain unavailable; exact new-SHA CI
  must run all ten inspector modes/Win32 fixtures, CPU oracle, ARM64 and Switch.

Reference for last-thread termination: [ExitThread](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-exitthread).
Remaining concrete foundation work: startup failure/partial attach shutdown,
startup blocking waits, DLL unload/refcounts/rollback. No claim of full NT
shutdown, TerminateThread or physical Switch verification. Stage C stays ~90%;
functional boundary closure does not remove its hardware acceptance gate.
Keep all changes on the agreed branch and do not extend/create automation.


### Last-thread first CI attempt and harness correction
Commit 19cf5bb0fdcef6f848dcc9eca78675ee8bff744f / run
[39736596521](https://github.com/Omeruno/switch-recompiler/actions/runs/39736596521):
host job failed on incomplete post-exit inspection, while native regression,
existing Win32 suite, CPU oracle and Switch/probe builds passed. Do not cite
this attempt as green. Source audit found the new suspended-peer harness
called Scheduler::Resume directly: that primitive only decrements suspendCount;
K32 ResumeThread additionally changes Suspended status to Runnable. Corrected
the inspector to invoke the actual Win32 ResumeThread HLE on a scratch frame
in live worker memory and assert previous count=1 plus runnable state. Added
scenario/state diagnostics for any remaining incomplete inspection. No runtime
assertion was removed and the production last-thread fix was unchanged.
Inspector compilation passes locally; complete exact-SHA CI rerun is required.


### Verified last-thread bridge and corrected inspector
Implementation/fix state **2a7346c1339f2852913da039af84c0e02f756f20**: all five
jobs PASSED in [39737128019](https://github.com/Omeruno/switch-recompiler/actions/runs/39737128019).
Fetched host-job logs confirm: `process detach (4 exit/abort + 6 last-thread
cases), loader-lock recheck, suspended-peer lifetime, TLS/order/codes/APC/timer/
mutex cleanup` PASSED. This verifies the corrected ResumeThread HLE path and
all assertions, including observed loader-token parking, no final-owner thread
detach, exactly one process sequence, old main/finished code 73 and final code 55.
Expanded native execution-stop regression, real thread reclamation and all
prior native/x86/MinGW guests passed. CPU oracle: 3 x 50,000, no differences.
ARM64 native continuation ABI/QEMU, Switch NRO and existing separate probe NRO
builds passed. The earlier red attempt is superseded, not described as green.
No physical Switch testing is claimed. A following documentation-only SHA
still needs its own Actions verification before reporting final completion.

Stage C remains ~90%. Next concrete packet: startup failure/partial initialization
shutdown, then DLL unload/refcounts/rollback. Startup blocking waits and full
NT failure semantics remain explicit limitations. Preserve interpreter and
hardware gates; do not advance JIT/GPU/shell, change branch, or extend automation.


## Cooperative startup and attach-failure termination (2026-10-22)

Incoming HEAD **fc557eff9152d1b595c9007a936112b7bf6870d7** verified green
in all five jobs [39737619758](https://github.com/Omeruno/switch-recompiler/actions/runs/39737619758).
Remote ref/branch/worktree checked, no pending diff or active build/test process
or competing CI. Work continues only in the agreed branch. No Microsoft
proprietary binaries or physical Switch were used.

- Audited startup: Switch/winrun ran InitializeModules before GuestExecution,
  so no saved startup stack/normal cooperative waits or fatal-failure cleanup.
  New native startup regression reproduced missing process-failure status.
- New explicit GuestExecution Startup::InitializeModules runs main DLL/TLS
  initialization on its fiber before main APC/user entry. AlreadyInitialized
  remains default for existing probes/harnesses; standalone initialization
  keeps its old limitations. Both production frontends now opt in.
- Ordinary false initialization selects STATUS_DLL_INIT_FAILED (0xC0000142),
  retains a diagnostic string, skips EXE entry and uses common process detach
  for completed modules before freeing TEB/TLS and finalizing HANDLEs. Winrun
  now returns raw Windows status for this failure (POSIX low byte 0x42), instead
  of old generic code 3; Switch prints the initialization diagnostic.
- DllMain returning FALSE balances TLS/DllMain with one immediate failed-load
  PROCESS_DETACH, reserved=0, under the same loader lock. Marked detached before
  guest cleanup so recursive exit cannot repeat it. This also corrects dynamic
  LoadLibrary failure: old inspector now distinguishes immediate failed-module
  cleanup from final process shutdown. Failed image/TLS unmapping, dependency
  refcounts and retry/rollback are deliberately still separate work.
- Explicit ExitProcess during attach or failed-load cleanup keeps its own code.
  ExitThread from the initial startup callback conservatively cancels the
  partial process with the requested code, preventing created workers from
  running into an uninitialized EXE; this is a bounded runtime policy, not
  complete NT behavior for unsupported DllMain termination. Host abort drains
  native RAII without new guest callbacks. Interrupted partial modules are
  excluded from process notifications; full partial-initialization rollback
  and startup exception/fault semantics remain open.
- New CRT-free source-only lifecycle_startup DLL + gui_startup_lifecycle EXE
  and mandatory post-exit inspector cover six modes: normal cooperative
  startup, FALSE attach, ExitProcess(37) from attach, startup ExitThread(38),
  host abort during pending wait, and ExitProcess(39) from failed-load detach.
  They verify child parks on loader token, finite wait times out in DllMain,
  child body begins only after EXE TLS initialization, main entry runs exactly
  once on success and never on failure/cancel, exact TLS/DllMain detach order
  and reserved values, original exit codes, no partial-module callbacks,
  mutex abandonment, timer cancellation, suspended/started worker reclamation
  and PEB retention. Existing ten exit/last-thread modes remain mandatory.
- Full local mandatory native/x86 suite PASSED with -fno-rtti/-fno-exceptions;
  winrun and both host inspector harnesses compile/link against rebuilt objects.
  Log: /tmp/switch-startup-host.log (transient). Local MinGW/32-bit libc still
  unavailable: new guests/old MinGW suite, CPU oracle, ARM64/QEMU and Switch
  build must be verified for the exact published SHA by Actions.

References: [DllMain](https://learn.microsoft.com/en-us/windows/win32/dlls/dllmain),
[process startup/thread serialization](https://learn.microsoft.com/en-us/windows/win32/procthread/terminating-a-process).
Next foundation work is DLL unload/refcounts/failed-image rollback and startup
SEH/fault handling; do not claim full NT loader semantics. Stage C stays ~90%
with physical Switch acceptance still outstanding. No JIT/GPU/shell gate is
removed and no automation may be created/extended.


### Startup first CI attempt: six new modes pass, old precondition corrected
Implementation 68db8675685d069615fb51cfd7aaed8d6eead406 / run
[39743657894](https://github.com/Omeruno/switch-recompiler/actions/runs/39743657894):
new six-mode startup inspector PASSED, as did the native suite, ordinary guest
programs, CPU oracle, ARM64/QEMU, Switch and probe builds. Host job remained
red because the older suspended-peer inspector's pre-resume scan rejected
*any* prior PROCESS_DETACH event. It must now distinguish the immediate
failed-load DLL6 detach (reserved=0) from final process-shutdown notifications,
just as its post-exit scan already does. Corrected that precondition to allow
only DLL6/kind2/reserved0, retaining the prohibition on all final process
callbacks before resume; final scan still requires exactly one failed detach
and the exact final process callback sequence. No production change required.
This first attempt is not green; verify the corrected exact SHA separately.


### Verified cooperative startup and attach-failure CI
Implementation/fix state **b0db92313bc5e91d5ebc50c08ea1bdf1299e1b88**: all five
jobs PASSED in [39744115873](https://github.com/Omeruno/switch-recompiler/actions/runs/39744115873).
Fetched host logs explicitly confirm six cooperative-startup modes PASSED,
all ten prior exit/last-thread modes PASSED, the new raw-entry startup EXE
PASSED through ordinary winrun, and the entire previous native/x86/MinGW
suite PASSED. This verifies both production-style startup and corrected
failed-load vs process-detach assertions. CPU oracle: three 50,000-case runs,
zero differences. ARM64 native ABI/QEMU, Switch NRO and existing separate
probe NRO builds all passed. The earlier red attempt is superseded and is
not reported as green. No physical Switch testing is inferred.
A following documentation-only commit still needs its own exact-SHA CI.

Stage C remains ~90%. Startup false/exit/abort paths are now covered, while
DLL unload/refcounts and full failed-image/dependency rollback remain the
next substantial foundation packet. Startup CPU fault/SEH failure policy and
physical Switch acceptance remain explicit gaps. Keep the same branch and
JIT/GPU/shell gates; do not create or extend any automation.

## DLL reference ownership and normal unload (2026-10-22)

Incoming HEAD ef3aec8528a8de0b6155bfe74b2541af9f12c146 verified against
remote branch and green five-job Actions run 39744624441. Work remains only
on claude/wonderful-hawking-10mpr0; no automation or proprietary binaries used.

- Replaced the unconditional FreeLibrary success stub with explicit dynamic
  LoadLibrary references, GetModuleHandleEx reference/PIN/UNCHANGED_REFCOUNT/
  FROM_ADDRESS handling and invalid-flag/output validation. Ordinary module
  lookup adds no reference. EXE and HLE modules remain permanent roots.
- Live-root graph traversal retains import/forwarder dependencies, handles
  shared children and collects unreachable import cycles. Forwarder lookup
  records a dependency instead of accumulating a dynamic reference per query.
  Dynamic lazy-forwarder initialization still deserves its own focused audit;
  this package does not assert complete NT loader semantics.
- Under cooperative loader lock, unload freezes the retiring batch and calls
  TLS/DllMain PROCESS_DETACH once, reserved=0, reverse initialization order.
  All images/TLS stay mapped until the entire batch returns. Reentrant loads
  of retiring modules and new imports from them fail rather than resurrecting
  an image that is about to disappear. Unsupported DllMain loader recursion
  is not advertised as a complete Windows contract.
- Unmap images and host-owned per-thread static TLS regions, including live
  suspended peers; clear guest slots and reuse vacant TLS indices. Never infer
  mapping ownership from guest-writable TLS values. Retain Module tombstones
  for callback snapshots; lookup and future notifications skip unloaded state.
  Host metadata can grow with repeated loads; compaction is not implemented.
- New CRT-free source-only unload_dep/top/side DLLs and gui_dll_unload exercise
  80 load/free cycles, two explicit loads plus GetModuleHandleEx ownership,
  detach order/TLS/reserved, common dependency lifetime, invalid/stale handle,
  A/W/address/unchanged/PIN flags, and a worker loading while another thread
  sleeps in detach. The worker receives a fresh load after loader serialization.
- New mandatory native inspector test_dll_unload runs 80 unload cycles with a
  deliberate cyclic dependency and suspended peer. Verifies image and every
  static TLS mapping is reclaimed, memory-region counts stay constant, TLS
  slots clear, corrupt guest TLS pointers cannot unmap the PEB, and main/worker
  TEB/PEB survive. Guest and native tests run through tools/run_host_tests.sh.
- Final full local suite PASSED, including all old/new Win32/CRT/C++ fixtures,
  startup/shutdown inspectors, native tests and Open/Save. Used relocated
  MinGW win32 toolchain and host g++ -fno-exceptions -fno-rtti. Initial local
  attempts had a POSIX-toolchain mismatch and a non-executable winrun output;
  neither is counted as green. Final log /tmp/loader-verified.log is transient.
  Exact published SHA still requires CPU oracle, ARM64/QEMU and Switch CI.

References: https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-freelibrary
and https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getmodulehandleexa.
Next bounded packet: failed-load image/TLS/dependency rollback and clean retry.
Failed/uninitialized mappings and their reference roots are deliberately not
collected here, preserving existing failure-inspection behavior. Also pending:
FreeLibraryAndExitThread, LoadLibraryEx datafile/image flags, full startup
fault/SEH policy and physical Switch acceptance. Stage C remains ~90%; normal
DLL unload belongs to the overlapping D work. JIT/GPU/shell gates unchanged.


## Dynamic loader rollback, forwarded attach and self-unload (2026-10-22)

Incoming HEAD **8b0ad70ad104a6a0226c62d2b63f2d1a37a12453** checked against
remote ref, all five jobs green in run 39766611875. Work remains on the agreed
branch. No automation, proprietary binaries or physical Switch were used.
User authorized publication to this repository/branch and offered hardware
testing; a concrete Paint baseline scenario and previous green NRO were sent.
No new hardware result has yet been received.

- New LoadLibraryInitialized owns dynamic map/attach transactions under the
  recursive cooperative loader lock. Temporary pendingLoads roots survive
  callback yields; an explicit reference is committed only after successful
  initialization. Failure releases only new unreachable native modules.
- Rejected DllMain already receives its immediate TLS/DllMain detach once;
  rollback avoids repeating it. Successfully initialized abandoned dependencies
  receive reserved=0 process detach in reverse order. All retiring batch
  images/TLS stay mapped until callbacks return; uninitialized maps never get
  callbacks. Normal image/TLS ownership cleanup also covers parser/bind and
  TLS setup failure, clears slots and permits clean repeat LoadLibrary.
- Existing dependencies/explicit references and independent nested successful
  loads are preserved. A new nested module depending on the rejected outer
  DLL is marked failed transitively (including cycles), detached if initialized
  and discarded despite its provisional returned handle. A new real-PE cycle
  regression failed on the previous implementation and passes after this fix.
- Reachability protects pending/initializing roots even during failed-load
  detach callbacks, while inactive failed roots cannot retain dead images.
  Dependency initialization uses a snapshot across nested callbacks.
- Actual guest GetProcAddress passes CPU, journals forwarder edges, initializes
  newly resolved targets before exposing callable code, and removes new edges
  plus unused images on missing export or attach failure. An initialized source
  module stays valid after a failed forwarded target and can retry. Native
  resolve-only calls without CPU retain their old loader/inspector contract.
- FreeLibraryAndExitThread caches its arguments, releases the DLL and requests
  thread exit without guest RET into the just-unmapped caller. An earlier
  callback exit/abort keeps its own requested code. A real stdcall DLL worker
  self-unloads and exits with 77; its HWND/normal thread cleanup uses existing
  execution lifecycle code. Export test uses actual PE name UnloadSelf@4.
- Missing explicit DLL returns 126; bad PE 193; rejected attach 1114; memory/TLS
  setup failure 8. Optional native CRT still takes precedence if present; a
  missing registered HLE library (e.g. msvcrt) retains fallback. Arbitrary missing
  explicit libraries no longer create success stubs. Legacy static unknown
  import/HLE placeholder policy is unchanged: full strict import resolution
  and complete NT status mapping are not claimed.
- New source-only controller/dep/top/baddep/parent/keeper/cycle/forwarder and
  malformed-PE/TLS fixtures exercise 80 failed loads plus a successful retry,
  exact TLS/DllMain detach ordering/reserved/TID, pre-existing/shared and
  independent nested roots, failed dependent parent, two racing failed loaders
  with a suspended peer, failed bind/TLS setup, forwarder missing export,
  attach failure/retry and repeated export lookup without extra attachments.
- New mandatory native test_loader_rollback captures mapped images and TLS
  while callbacks are parked, then checks every page is unmapped after failure,
  both threads' region counts stay constant, slots clear, corrupt suspended
  peer slots cannot unmap PEB/TEBs, and subsequent good load/unload works.
  Existing lifecycle fixture now expects three real failed attach attempts
  instead of a permanently cached failure; prior process-exit/startup contracts
  and inspectors are preserved. Extended normal-unload guest checks self exit.
- Final complete local suite PASSED, using host g++ -fno-exceptions -fno-rtti
  and relocated MinGW win32 toolchain: all native, x86, CRT/C++, loader,
  startup/process-exit, USER32 and Open/Save fixtures. Final transient log:
  /tmp/rollback-publish-host.log. CPU oracle/ARM64/Switch still require exact
  published-SHA CI; do not infer results or hardware acceptance from local green.

References: https://learn.microsoft.com/en-us/windows/win32/dlls/dllmain
and https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-freelibraryandexitthread.
Boundaries: rollback of arbitrary DLL side effects is not possible or claimed;
DllMain owns application resources/created threads. Explicit ExitProcess,
ExitThread and host abort during callbacks keep the existing shutdown/unwind
policy instead of starting new rollback callbacks. Startup/SEH fault rollback,
full NT loader/LoadLibraryEx datafile/image semantics, strict unknown imports,
security, metadata compaction and physical Switch acceptance remain open.
Stage C stays ~90%; this substantial packet advances overlapping D. Next:
startup CPU-fault/SEH policy and remaining loader/termination cancellation edges,
then hardware acceptance/input ownership and the renderer boundary. Preserve
JIT/GPU/shell gates, same branch and no new/extended automation.


### Publication authorization and Switch feedback (2026-10-22)
User explicitly authorized this and future project packages to be published to
Omeruno/switch-recompiler on claude/wonderful-hawking-10mpr0 without repeated
permission questions. Continue normal local validation, no force-push and
exact-SHA CI verification. This authorization does not broaden repository or
branch scope. The prior automatic publication rejection is superseded by this
explicit continuing authorization; no credential/login handoff is needed for
the connected GitHub tools.

User hardware report: Paint works without changes/regression on the provided
8b0ad70 NRO (Actions 39766611875). This confirms that Paint hardware baseline,
not every concurrency/loader edge or the new rollback code. Preserve existing
known-good 2c9a1fa and this newer verified Paint baseline separately. The new
rollback package still requires its own CI and hardware validation as applicable.


## Bounded loader initializer exception containment (2026-10-22)
Incoming HEAD a0d816a72c7c9aab8708fd0643b64a2bf4c74619 is exact-SHA CI green
(all five jobs, run 39770705669). User requested another substantial packet;
standing same-repository/branch publication permission above still applies.
Paint hardware baseline remains 8b0ad70; no newer hardware result is claimed.

- GuestFaultBoundary is a thread-owned linked native-frame record inside
  InitializeModule after dependency initialization. It survives fiber yields,
  uses normal returns/destructors (no C++ exceptions/longjmp), restores saved
  FS:[0] after an unhandled callback and pops before thread memory reclamation.
- Real guest SEH gets first refusal for RaiseException and interpreter faults,
  including null transfer and illegal opcode. An unhandled initializer fault
  stops nested guest callbacks and becomes ERROR_DLL_INIT_FAILED (1114) for
  dynamic loads or STATUS_DLL_INIT_FAILED (C0000142) at startup. Existing dynamic
  transactions reclaim rejected maps/TLS; startup preserves process shutdown.
- Faulted attach receives no PROCESS_DETACH. Ordinary DllMain FALSE retains its
  established immediate cleanup, including containment if that cleanup faults.
  Initialized dependencies still receive their normal detach. Nearest nested
  boundary contains a failed child load so a parent can continue successfully.
- Nested handler faults retain the first actual inner fault. RaiseException
  parameters/code/flags are local across handler yields; another running thread
  raising its own handled exception cannot overwrite the initializer record.
  SEH dispatcher/unwind stop when fault is pending rather than using a stale
  handler disposition/context. ExitThread/ExitProcess or host abort inside the
  handler preserve original exit/cancellation semantics, with no false fault.
- Module tombstones retain initializer exception code/EIP for inspectors.
  InitializationError includes code/EIP/fault address. Switch startup failures
  now write the existing SD crash report, including initializer/code/EIP if a
  module fault record is present; normal FALSE failure still gets phase/reason.
- Source-only PE fixtures and mandatory test_initfault cover 36 startup
  DLL/TLS/EXE-TLS cases, two parked loader/handler aborts, seven standalone
  failure scopes and a dynamic recovery sequence. Includes handled Raise/int3,
  unhandled Raise/AV/null/UD2, corrupt FS registration, handler Raise/AV,
  noncontinuable continuation, FALSE cleanup exception, nested LoadLibrary,
  yield with another raising thread, handler ExitThread/Process, live/suspended
  worker cleanup, exact FS restoration, unmapped images, retry and fault codes.
  All code builds with -fno-exceptions -fno-rtti. A pre-fix cancellation probe
  reproduced false exception diagnostics for handler ExitThread; guards fix it.

Boundaries: this is not a complete NT loader SEH frame/finally-unwind policy.
Host/HLE fatal validation and unsupported operations remain fatal; arbitrary
DLL resource/created-thread side effects are application responsibilities.
Full startup image rollback, exact NT failure mapping, termination/security,
LoadLibraryEx flags and metadata compaction remain future work. Stage C stays
~90%; this packet advances overlapping D and adds shutdown acceptance coverage.
Do not claim physical Switch verification from CI or the prior Paint result.

Validation for this packet: complete local sh tools/run_host_tests.sh PASSED,
using relocated MinGW and host CXX='g++ -fno-exceptions -fno-rtti'. Log:
/tmp/initfault-full-clean.log. All old native/PE/CRT/GUI/dialog regressions and
new inspector/dynamic guest passed. Earlier stale malformed-fixture failure
was isolated: fresh build retained its deliberate 35-byte invalid PE and all
rollback tests passed without changing their expected behavior. Exact-SHA
CPU oracle/AArch64/Switch CI remains a publication gate; inspect its actual
result before claiming green. Next bounded work: remaining shutdown/input
ownership boundaries, then renderer boundary; preserve existing roadmap gates.


## GUI input ownership and concrete Stage C acceptance (2026-10-22)
Incoming HEAD 1a5f585ea718eadf02eb3696cb86f6accbe44bb7 is exact-SHA CI green
(all five jobs in 39775696837). User requested next substantial packet and a
concrete finish for C rather than an indefinite 90% estimate. Continuing
same-repository/branch publication authorization remains in force.

- Queue-local active/focus replace shared globals; foreground is separate.
  Foreign SetFocus/SetActiveWindow/SetCapture cannot mutate a peer's queue.
  GetCapture/ReleaseCapture/SetCapture(NULL) cannot expose/release peer capture.
  Controls use centralized capture change notifications. Drawing checks the
  owner window's queue; dialog/comdlg operations use their current queue.
- Focus and activation publish state before guest callbacks, then validate
  serials/liveness/cancellation before sending follow-up notifications. Nested
  focus redirects, destroyed target and callback ExitThread keep their newer
  state. Re-activation preserves a valid child focus. Foreground state, API
  GetActiveWindow state and non-client display activation are distinct.
- Keyboard/char/wheel use foreground queue state, regardless of which thread
  polls the host. Capture routing distinguishes hover over another thread from
  foreground-owner drag. Per-button origin thread ownership prevents a click
  started on a peer from becoming a capture drag after that peer window dies;
  the fifth platform batch reproduced this before the origin-owner fix.
  WM_MOUSEACTIVATE caches HWND, revalidates after guest
  callbacks and cannot activate a window destroyed by its handler.
- Hide/disable/destroy clear subtree focus/capture. RetireWindow is the single
  silent invalidation point for queue/global references, including host abort
  and ExitProcess teardown. Menu loop no longer resurrects a saved stale HWND.
- GetGUIThreadInfo validates mapped PE32 48-byte buffer, cbSize, thread/queue,
  supports id=0 foreground and returns active/focus/capture; remaining fields
  are zero. AttachThreadInput explicitly returns ERROR_CALL_NOT_IMPLEMENTED:
  shared/implicit input attachment is not falsely advertised as success.
- Source-only gui_thread_input covers two GUI threads and TEB/TLS owner checks,
  foreground vs background focus/activation, foreign mutation/release rejection,
  disabled controls/parents, nested focus redirect and target destruction,
  reentrant capture, WM_SETFOCUS/WM_CAPTURECHANGED ExitThread and clean retry.
  Native test_gui_input additionally exercises host abort/ExitProcess while
  both native fibers are parked and five real platform-event batches polled by
  the peer: A/B keyboard/char, foreign hover, held-button drag and destruction
  from WM_MOUSEACTIVATE. All references/queues/windows are checked after exit.
- stage_c_acceptance.exe links eight existing source test bodies in separate
  translation units with renamed main functions: sync, multiple waits, named
  access, SignalObjectAndWait, timer contention, mixed MsgWait/APC, input and
  timer-APC exit. It flushes BEGIN/PASS per suite, ends with PASS: ALL 8 SUITES,
  writes stage-c-acceptance.txt beside EXE and displays a brief PASS window.
  CI runs this same EXE. docs/STAGE_C_ACCEPTANCE.md defines the bounded gate.
- Stage C estimate is now ~95% for the existing single-process model; remaining
  gate is physical execution of the matching green NRO + acceptance EXE, not
  full NT/security/cross-process/TerminateThread expansion. Software and
  hardware evidence remain separate. Prior Paint hardware baseline is 8b0ad70.
  Do not call C complete before receiving the actual hardware report.

Boundaries: no complete Windows foreground policy, shared AttachThreadInput,
implicit cross-thread parent input groups, hooks ownership, concurrent menu
loops, full caret/GUIThreadInfo metadata or cross-process UI/security contract.
These remain explicit follow-up work; renderer-backend boundary can follow
this acceptance gate without pretending to implement all Windows semantics.

Validation: final complete sh tools/run_host_tests.sh PASSED on the exact final
source state with host g++ -fno-exceptions -fno-rtti and relocated MinGW win32.
Transient log /tmp/input-exact-final-host.log. Five platform batches, forced
GUI cleanup, every old native/PE fixture and the combined eight-suite runner
passed. Earlier fifth-batch failure reproduced foreign-origin drag capture;
per-button origin ownership fixes it without weakening that assertion.
Publication still requires exact-SHA CI; do not infer hardware acceptance.
References: https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getfocus
https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setfocus
https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getactivewindow
https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setcapture
https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getguithreadinfo.
