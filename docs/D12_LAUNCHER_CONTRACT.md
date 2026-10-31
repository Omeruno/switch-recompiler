# D12 — launcher path, guest identity and explicit DirectPlay refusal

Incoming `431787a96726b2686517018a14e98e9dc3e879aa` passed all six jobs in
[CI41174190664](https://github.com/Omeruno/switch-recompiler/actions/runs/41174190664).
This package addresses common Win32 launcher contracts. It does not implement
DirectDraw, DirectSound, DirectPlay networking or certify Doom95 gameplay.

## Observed boundary and shared changes

The supplied Doom95 launcher tested an empty first data-file candidate. Previously
GetFileAttributesA normalized that empty string into the VFS current directory,
returned FILE_ATTRIBUTE_DIRECTORY and prevented the launcher's real candidate
search. Returning INVALID_FILE_ATTRIBUTES/ERROR_PATH_NOT_FOUND makes the original
code search actual files and load its configuration DLL. No game code, data,
command line or runtime instruction is patched.

GetFileAttributes A/W and Ex A/W now use a bounded, terminated guest path reader.
Empty names fail with error3; invalid/unmapped/wrapping spans fail with998 and
32768 unterminated code units with206. ANSI uses the existing guest CP1252→UTF8
conversion; UTF16 uses the shared UTF8 conversion. A real dot path still resolves
the current directory. VFS/stat errors are mapped, including actual missing paths,
non-directory path components and denied mounts/links. Ex accepts only the
standard information level, validates the whole36-byte output and leaves it
unchanged on failure. Existing timestamp/attribute projection remains unchanged;
this is not full NT path/attribute/ACL/page-protection semantics.

The next actually reached missing import was ADVAPI32!GetUserNameA. A/W now expose
our existing single virtual account, Player. The environment default and API share
one identity constant. Changing USERNAME alters the guest environment, not the
account identity, as on Windows. These APIs do not read the cloud user's identity,
implement Windows tokens, impersonation or multiple users. Size is in characters,
includes NUL, and is returned on success/insufficient capacity. Short buffers get
ERROR_INSUFFICIENT_BUFFER122 without text writes; inaccessible/overflowing spans
get998 without partial text writes. No host account name is leaked.

The following reached call was DPLAY ordinal2, DirectPlayEnumerateA, stdcall8.
The old generic ordinal fallback used MFC-like EAX and no argument cleanup for
this non-MFC DLL. A subsequent guest callback fault was observed. Names and
ordinals1/2/3 now bind to explicit DirectPlayCreate/EnumerateA/EnumerateW refusal:
DPERR_UNSUPPORTED/E_NOTIMPL, correct stdcall12/8, no callbacks or providers, and no
COM object. Create clears a validated output slot; invalid output gets E_INVALIDARG.
This is an honest unsupported boundary, not a networking implementation or an
empty-provider success. Ordinal mapping is corroborated by the public
[Wine dplayx export specification](https://github.com/wine-mirror/wine/blob/master/dlls/dplayx/dplayx.spec)
and signatures/HRESULTs by public MinGW dplay.h. No external code is copied.

With all three changes, the private new-UI host trace reaches the original
launcher's dialog/message loop and no longer hits those startup failures. A
separate diagnostic driver with the real production composition/present callback
renders the original launcher with New Game and Cancel. The ordinary winrun final
capture reads the underlying screen surface and still shows the splash; that
capture alone is not a composed-window oracle. A real scripted mouse click on
New Game reaches the next missing import, ADVAPI32!RegCreateKeyA, and stops there.
No DirectDraw call/frame or gameplay has been reached. Configuration controls,
game input/audio and physical Switch acceptance remain unverified. Existing
COMCTL32 ordinal17 (InitCommonControls) fallback is still observed/unresolved;
this package does not turn it into a fake successful initializer.

## Public evidence

launcher_contract.c is an authored CRT-free PE32 fixture. Its import library uses
real DPLAY ordinal1/2/3 imports through launcher_dplay.def. It exercises:

- Empty A/W and Ex paths versus a real dot directory and actual candidate-file
  reads, missing files/non-directory errors and CP1252/UTF16 names.
- Real36-byte file information and independently created eight-byte fixture data.
- UserName A/W size/NUL semantics, environment mutation independent of account
  identity and fresh environment defaults between guest sessions.
- Actual x86 ordinal calls returning unsupported HRESULTs, no provider callbacks
  and a null object. Native dispatch additionally checks named aliases/stack ABI.

The native inspector checks exact stdcall stack cleanup, output guards, malformed
input/output ranges, overflow, missing terminators and refusal without output
mutation. Four normal new/legacy x86 sessions plus two genuinely parked aborts
exercise session restart and continuation draining. The full host/MinGW/Mesa suite
runs this fixture and CI publishes stage-d-launcher-contract with authored EXE,
source/DEF, log, inspector, exact implementation and this contract. No proprietary
EXE/DLL/WAD/archive/image/private trace is committed or published.

Strict source/inspector compilation and focused ASan/UBSan cover these changed
implementations and actual guest runs; other runtime objects remain ordinary,
with the established ucontext caveat and leak scanning disabled for those runs.
A negative probe removing empty-path refusal must fail the public oracle.
Local devkitA64/libnx is unavailable; exact-SHA CI builds NRO. Host evidence never
substitutes for physical Switch acceptance.

## Next

Implement legacy RegCreateKey A/W over real registry keys with a public
reproducer, then trace actual launch again. Audit launcher configuration and
replace Common Controls fallback only with real supported class initialization.
Then implement bounded DirectDraw COM lifetime,
surfaces/locking/palettes/presentation through public pixel/input/lifetime oracles.
DirectSound real sources, shared media bus, MIDI/joystick, virtual display modes,
DirectPlay and broader APIs remain separate incomplete blocks. Keep CPU GDI
production default, Luna, L/ZL overrides, F7/F8 controls/view and closed SD diagnosis.
