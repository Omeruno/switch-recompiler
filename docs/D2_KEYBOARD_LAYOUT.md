# D2 — fixed-US keyboard translation and names

Implemented 2026-10-28 after D1. This is a common USER32 implementation, with
no application-name switches, private payload or successful missing-API stub.

## Contract

The existing `GetKeyboardLayout` reports `04090409`. One authored PC set-1
US 101/102 table now supplies `MapVirtualKeyA/W` modes0–4,
`GetKeyNameTextA/W`, host physical snapshots and USER keyboard messages.
Unknown modes/keys/scans return0. Mode2 returns unshifted characters (uppercase
letters); modes1/3 collapse/preserve modifier sides. Mode4 preserves E0/E1;
legacy mode0 has no E1 representation and returns0 for Pause.

Virtual-key mapping and physical position are distinct. Numpad4 and Left have
legacy virtual scan4b; inverse virtual mapping selects Left. Physical navigation
uses E0, numpad digits use base scans. Right Ctrl/Alt, Windows/Application,
main/numpad Enter, Pause and Num Lock have distinct physical message positions.
Pause's E1 sequence is represented in WM_KEY* as scan45 without the E0 bit.
The host-only optional `InputEvent::scanCode` carries a full physical scan;
existing VK-only producers receive canonical scans. Historical packed low scan
in `vk` remains supported when no explicit scan is supplied. Guest MSG ABI is
unchanged; all host objects must be rebuilt after the InputEvent header change.

Messages use generic Shift/Ctrl/Alt wParam, physical scan/extended bits, existing
repeat/up/Alt-context semantics. Observation updates asynchronous state once;
deferred ShellSession events retain scan/side/previous-state snapshots. Removing
input updates the receiving queue's sided and aggregate state; PM_NOREMOVE does
not advance that state. Releasing one side preserves an aggregate held by the
other side. Draining old events does not rewind asynchronous physical state.

Names use lParam scan16–23, E0 bit24 and no-side bit25. Repeat/context/up bits do
not change a name. English ASCII names have equivalent UTF16 W output. Capacity
is in characters; output truncates to capacity−1 and NUL-terminates. Capacity0
or negative returns0 without touching output; capacity1 writes only NUL. An
unknown scan clears the first character of a valid positive-capacity buffer.
Success/unknown preserve LastError; positive-capacity NULL returns87; unmapped
or wrapping actual output range returns998 before any write. Only actual output
bytes are validated, rather than an arbitrarily large advertised capacity.

## Reproducible public evidence

Run `COMPOSITION_GL_TEST=1 sh tools/run_host_tests.sh` with host C++17, MinGW
and the documented Mesa dependencies. Existing six-job CI host job publishes
`stage-d-keyboard`: public CRT-free PE32, native mapper/input logs, four-session
API log and this contract. No proprietary executable or trace is published.

- `tests/test_keyboard_layout.cpp`: independent known positions, collisions,
  modifiers/E0/E1, OEM characters/names, full invalid-VK range and unknown modes.
- `tests/guest/keyboard_layout.c` + `tests/test_keyboard_api.cpp`: four fresh
  normal-exit0 ordinary interpreted x86 sessions. Real stdcall A/W API results,
  truncation/NUL/poison/errors/LastError;19 delivered events per session,
  main/numpad and navigation distinctions, repeat, generic/sided modifiers,
  PM_NOREMOVE and releasing left Shift while right Shift remains held.
- `tests/test_host_input.cpp`: both-side asynchronous state, generic VK with
  explicit right physical scan, deferred E0 snapshots, paired fixed-ring wrap,
  source-close and existing shell input behavior.
- Negative controls outside the source tree restore identity MapVirtualKey or
  zero message scans. The public guest rejects each at its intended assertion.
- Focused ASan/UBSan covers the inspector and keyboard/layout/USER API/message
  modules, with other runtime objects ordinary. Driver leak scanning is disabled;
  the known makecontext/swapcontext warning is retained. This is not whole-runtime
  sanitizer or Switch acceptance. Strict O2/no-exceptions/no-RTTI compilation
  covers the changed modules and inspectors; pre-existing USER unused-parameter
  warnings are excluded from the strict API compilation.

Reference tables/tests from Wine and ReactOS were consulted for PC/Win32 facts;
no third-party implementation is imported. No Windows execution oracle was
available. This fixed profile does not implement layout loading/switching,
localization, IME/dead keys/AltGr, ToUnicode, arbitrary media keys, physical USB
keyboard integration or a complete Windows keyboard subsystem. Existing Char/
TranslateMessage behavior is separate; no Switch controller remapping was added.

## Actual next dependency

A private, uncommitted host Pinball x86 probe passes GetKeyNameTextA and reaches
`KERNEL32!GetPrivateProfileIntA`, currently missing and fatal. It creates USER
windows but has no accepted playable/main game frame or measured Switch FPS.
Next D block: genuine bounded INI/profile reading/writing through the VFS with
public A/W/default/integer/size/error/persistence tests. Existing profile-default
and write-success stubs need an audit; adding only a signature/default return
would not meet that contract. Registry persistence migration/full encoding/
enumeration/deletion remain separate open work. Hardware Paint/GPU/fallback/
input/relaunch remains deferred; SD recovery is closed.
