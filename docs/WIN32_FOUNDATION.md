# Win32 foundation: architecture and implementation order

Status: design and implementation backlog, **NOT a claim that the features work**.
Target: **32-bit Windows XP / NT 5.1 user-mode** programs running as
x86 interpreted guests on Switch (libnx). Do not simulate a Windows
kernel or recompile the guest.

## Historical starting point (before SEH/thread implementation)

The paragraphs below describe the original planning checkpoint, not current
HEAD. SEH dispatch/unwind, runnable workers, cooperative waits and the common
HANDLE table now have implementations. Read [ARCHITECTURE.md](ARCHITECTURE.md),
[ROADMAP.md](ROADMAP.md) and the latest dated AI_HANDOFF entry first.

### Original observations
- \`rt::Process\` maps PE EXE/DLL and imports, has a single prepared main
  guest CPU/stack and a synthetic TEB/PEB. At \`PrepareMainThread\`,
  \`TEB+0x00\` (NT_TIB.ExceptionList, \`FS:[0]\`) is \`0xffffffff\`.
- \`CallGuestFunctionStdcall\` runs nested guest callbacks via its own
  interpreter and a sentinel return address.
- \`HostCallOutcome::customReturn\` can transfer control to guest code
  without forcing a normal \`ret\`. Use that for SEH dispatch/unwind.
- Existing KERNEL32 \`RaiseException\` previously ignored the exception.
  At \`3dfa89a\` it **fails with a diagnostic** rather than returning.
  Full guest SEH dispatch is **not** implemented.
- \`rt::Vfs\` maps guest C: to the EXE directory.
- \`kernel32_sync.cpp\` contains partial synchronization HLE. This is
  not equivalent to an NT guest scheduler.
- Dynamic \`GetProcAddress\` reporting exists for investigating MSVC delay-load.
  Known hardware fault: \`mfc42u.dll+0x21fd8\`, \`FF E0 / jmp eax\` with EAX=0.

## Contract priorities

### Phase 1: real single-threaded x86 SEH
Implement \`GuestExceptionDispatcher\` owned by \`Process\` (not separate
implementations in every CPU loop).

Guest memory layouts must be exact **x86 32-bit Windows ABI**:
- \`EXCEPTION_REGISTRATION_RECORD\` = 8 bytes:
  \`Next\` at 0; \`Handler\` at 4.
- \`EXCEPTION_RECORD\`: code at 0, flags at 4,
  chained record pointer at 8, exception address at 12,
  parameter count at 16, aligned parameter array at 20
  (max 15 \`ULONG_PTR\`).
- \`CONTEXT\` = x86 Windows layout including \`ContextFlags\`, segment
  registers, integer registers, \`Eip\`, \`EFlags\`, \`Esp\` and floating
  state. **Do not construct an invented reduced CONTEXT layout**.
- Dispatch calls a guest handler with 4 arguments:
  \`(EXCEPTION_RECORD*, establisherFrame, CONTEXT*, dispatcherContext)\`.
  Respect \`ExceptionContinueExecution\`, \`ExceptionContinueSearch\`,
  \`ExceptionNestedException\`, \`ExceptionCollidedUnwind\`.
- Read \`FS:[0]\` from the **current guest thread TEB**, validate all
  record addresses and prevent cycles, excessive chain depth and invalid
  pointers. Do not dereference untrusted guest memory without page checks.
- For \`RaiseException\`, represent exception as guest state and deliver it
  through the same dispatch path used by interpreter faults. Fail visibly
  only if no handler accepts it.
- \`RtlUnwind\`/unwind flags and \`SetUnhandledExceptionFilter\` behavior
  are separate tasks. Don't claim complete SEH until both normal dispatch
  and unwinding are tested.

Acceptance tests (guest x86, compiled with mingw where supported):
1. Native-code \`__try/__except\` style chain via hand-written x86 SEH
   registration (MinGW may not support MSVC syntax).
2. \`RaiseException\` handled and execution resumes at valid CONTEXT EIP.
3. Two handlers, one ContinueSearch then ContinueExecution.
4. Unhandled exception preserves code and address in crash log.
5. Invalid/cyclic registration does not crash the Switch host.
6. Nested WndProc and outer interpreter use the **same dispatcher**.

### Phase 2: per-thread guest state and scheduler
- \`GuestThread\`: unique thread ID, CpuState, stack limits, TEB/TLS,
  last error, SEH head, suspend count, exit code and wait state.
- Guest scheduler advances instruction quanta and can switch among
  runnable guests; avoid blocking libnx main/render thread on guest wait.
- CreateThread/ExitThread/GetCurrentThreadId/Tls* and basic waits:
  realistic handles, priority baseline, thread lifecycle and cleanup.
- Single-thread programs must continue to behave identically.

### Phase 3: NT object handles & synchronization
- Central handle table with typed file/event/mutex/semaphore/thread
  objects, reference counts and duplication.
- Correct WaitForSingleObject/WaitForMultipleObjects semantics,
  timeouts, signaled state and WAIT_ABANDONED.
- Critical sections and atomics must honor cross-guest-thread state.
- Keep host-native handles separate from guest 32-bit HANDLE values.

### Phase 4: loader and ABI hardening
- Module refcounts, correct FreeLibrary, per-module exports by name and
  ordinal, forwarded exports, TLS callbacks per thread and DllMain
  notifications, correct DLL load/unload errors.
- Validate /DELAYLOAD with two exports: present and missing, plus
  exception recovery hook, before attributing faults to SHELL32.
- Preserve exact stdcall/cdecl/thiscall stack and registers across HLE,
  callbacks and exceptions.

### Phase 5: Win32 user-mode compatibility layer
- KERNEL32/NTDLL, registry, VFS and path/encoding, USER32 message queues
  per thread, GDI, COM/SHELL32 and file dialogs.
- API support goes in reusable modules; **no executable-specific
  opcode/address patches for Paint**.

## Engineering protocol
- One feature slice = host-only fast regression + one CI run after the
  related commits; no repeated full CI per small change.
- Document verified host behavior separately from **physical Switch**
  behavior. Do not mark Paint fixed without reproduction on Switch.
- Preserve old working .nro. Keep \`docs/AI_HANDOFF.md\` up to date.
- Work ONLY on \`claude/wonderful-hawking-10mpr0\` until explicitly changed.
- Never commit proprietary Paint / MFC binaries. Request a matching
  legally sourced DLL only when symbol/disassembly requires it.

## Original first implementation slice (already implemented as a subset)
Create SEH ABI structs/constants with compile-time size/offset assertions
and a focused host test. Then implement the dispatcher and wire both CPU
loops and \`RaiseException\` to it. Do not re-enable silent exception
swallowing as a shortcut.
