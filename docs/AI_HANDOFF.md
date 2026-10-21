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
