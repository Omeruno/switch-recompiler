# D13 — real legacy RegCreateKey A/W

Incoming `79331d8f71f5a04f4ef4c599d512684ca7c64dca` passed all six jobs in
[CI41178225409](https://github.com/Omeruno/switch-recompiler/actions/runs/41178225409).
This extends [D1's real registry](D1_REGISTRY_KEYS.md), not the supported Windows
security/persistence/encoding scope or DirectX implementation.

RegCreateKeyA and RegCreateKeyW now use the same checked key-creation operation as
RegCreateKeyEx. Their actual ABI is stdcall12, with the result at argument2. The
legacy path must not read Ex-only class/options/security/disposition arguments
from unrelated caller stack words. Intermediate keys, case-insensitive ASCII
names, relative paths, existing keys and real allocated HKEY grants use the same
store. Null/empty subkey retains the legacy input-handle semantics. Output/path
ranges are checked before creating a key; errors preserve the output and last
error as in D1. Closing a non-root handle actually invalidates it.

Supported registry names remain bounded printable ASCII A/W. Full Unicode names,
Windows token/ACL behavior, checked durable persistence, enumeration/deletion and
other legacy default-value APIs remain separate work. RegCreateKey success means
an actual in-memory key/grant, not a power-loss-safe SD write.

The authored launcher_contract.exe now creates nested keys through A, writes an
actual DWORD through SetValueEx, reopens case-insensitively through W, queries its
bytes/type/size, checks close/stale errors and null/empty roots. Four normal
new/legacy x86 sessions plus two parked aborts exercise the real imports. The
native inspector independently reads the underlying store's exact DWORD bytes
and closes its inspection handle. It additionally checks stdcall cleanup, invalid
parents, inaccessible/overflowing outputs and input pointers, unchanged output on
failure and unchanged last error. Existing D1 registry/session/rights/limits tests
remain part of the full suite. Focused ASan/UBSan covers the registry module and
inspector with ordinary remaining runtime objects; guest ucontext caveat/leak scan
exclusion still applies. Strict source/inspector compilation is required.

CI stage-d-launcher-contract includes the updated authored EXE/source/inspector,
registry implementation and this contract. No supplied Doom95 EXE/DLL/WAD, private
traces or captures are published. In a private host run the original New Game
mouse click passes RegCreateKeyA and proceeds to the next actually missing call,
WINMM!midiOutGetNumDevs. No DirectDraw call/frame or game has been accepted.
Local Switch SDK is absent; exact-SHA CI supplies NRO. Physical tests stay deferred.

Next: implement truthful MIDI device enumeration (no MIDI backend currently),
trace the genuine startup again, then bounded DirectDraw/DirectSound interfaces
through public resource/pixel/ownership tests. Preserve CPU GDI default, Luna,
F7/F8 input/view, L/ZL overrides, existing audio and the closed SD recovery issue.
