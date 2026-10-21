# AI development handoff — switch-recompiler

Last verified incoming GitHub state: **83b299be01406d86d0796107d9b1c309b9ab0257**, branch `claude/wonderful-hawking-10mpr0` (2026-10-21). All four jobs passed in run [39650590532](https://github.com/Omeruno/switch-recompiler/actions/runs/39650590532). This file is coordination documentation, **not** a substitute for the actual branch HEAD, diffs or test results.

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
