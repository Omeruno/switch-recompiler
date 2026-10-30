# D6 — TrueColor palette contract, shared strings, messages and multimedia clock

This common compatibility package follows observed private Pinball imports, but
its acceptance uses authored public resources and tests. No game-specific branch,
proprietary image/resource table or successful signature-only stub is added.

## System palette versus actual display

Both display backends render TrueColor and do not advertise RC_PALETTE. New shared
SetSystemPaletteUse/GetSystemPaletteUse/GetSystemPaletteEntries validate an actual
DC in the active new/legacy GDI store. An invalid/deleted DC refuses (last-error6).
A valid TrueColor DC refuses system palette use/entries (SYSPAL_ERROR/zero,
last-error50), without changing pixels, logical palette objects or output buffers.
Invalid SetSystemPaletteUse modes refuse87. New GetDeviceCaps validates the DC and
reports NUMRESERVED=0, consistent with SIZEPALETTE=0 and no RC_PALETTE. Memory DCs
with indexed DIBs still belong to a TrueColor display: their private color tables
do not create a hardware indexed system palette. The former fabricated256-entry
system table is removed. No fake mutable hardware palette state is introduced.

Logical palettes and DIB_PAL_COLORS remain their existing separate path. The public
pixel oracle selects a two-color logical palette, renders actual indexed DIB pixels
and confirms green/red survive every system-palette refusal. This package does not
claim complete logical palette animation/resizing or a palettized display backend.

## Shared LoadStringA/W for new and legacy UI

A single always-registered provider replaces the separate modern/legacy Win32
implementations. It resolves the actual EXE/DLL HINSTANCE through the live Process,
uses the low16-bit string ID and a checked RT_STRING resource span. Resource tree
lookup validates complete directories/entries/names/data ranges with wide offset
arithmetic; invalid/truncated directories cannot become fabricated zero-valued
entries. Unloaded modules refuse. Existing neutral/requested/first-language policy
remains; this is not a complete Windows localization/resource-update mechanism.

The immutable module image describes directory/length metadata; copied text reads
actual mapped guest payload bytes at that span. This does not add parsing of guest
rewrites of resource directory/string-length metadata. Full source and actual output
ranges/32-bit wrap are checked. Copy mode stages UTF16 text before output, supports
embedded NUL, truncates to cap-1 and writes one terminator without touching canaries.
Only actual copied bytes must be mapped, not an enormous unused advertised capacity.
Missing resources return0/empty copy output; invalid module uses6, missing resource
1814, invalid mapped source/output998. Error reporting is this bounded runtime
contract, not an exhaustive Windows last-error priority matrix.

ANSI conversion uses the same actual CP1252 table as the runtime ACP; Euro is0x80,
Latin1 é is0xe9, unrepresentable UTF16 units use '?'. Full best-fit/surrogate/other
code pages are outside the existing encoding profile. W copy returns UTF16 units;
A copy returns actual bytes. Negative capacity refuses; A capacity0 returns0 without
the W-only pointer convention. W capacity0 returns length and a checked **guest**
resource pointer, with no retained native borrow. Empty present entries return0
and their guest pointer; missing entries leave the pointer untouched. The pointer
is valid only while that module stays loaded; copied strings survive unload.
No-Process legacy globals are not treated as a fabricated live module. MFC's older
resource helpers remain separate; general legacy API completeness is not implied.

## Registered messages and timeGetTime

RegisterWindowMessageA/W now share an always-registered checked provider in both
UI paths. It uses the existing process-independent native registered-message
namespace; repeat names and ASCII case folding preserve IDs across guest sessions.
ANSI names convert CP1252 to Unicode, so A/W Euro names identify the same message.
Names are bounded to255 UTF16/ANSI units plus NUL; empty, overlong and unmapped/
wrapping strings refuse before registration. The existing namespace's ASCII case
folding and C100–FFFF capacity remain; full Unicode case folding is not added.
RegisterClipboardFormat's modern wrapper delegates to the same checked naming
provider instead of keeping an unchecked duplicate path. Registering a name does
not invent a guest message delivery/queue/worker.

timeGetTime reads the actual existing platform GetTickCount hook or monotonic
runtime Clock, with DWORD wrap. It shares virtual uptime/fast-forward behavior with
GetTickCount and Sleep; no constant/fake-increment counter, resolution request or
native timer callback is invented. Multimedia timeBeginPeriod/timeSetEvent, MIDI
and full multimedia timer semantics remain separate future work.

## Reproducible evidence

- Public CRT-free palette_strings.exe and resource_strings.dll both contain authored
  RT_STRING tables. Four normal new/legacy x86 sessions plus one parked DLL-pointer
  abort test TrueColor capabilities/refusals, actual unchanged screen/DIB pixels,
  EXE/DLL strings, CP1252, empty/embedded NUL, truncation/guards/huge capacity,
  W-only pointer lifetime and copied data after actual DLL unload. Registered A/W
  names remain stable across UI/session changes; Sleep37 advances the shared clock.
- Native malformed resource tests cover complete predecessor/selected-string spans,
  overflow, truncated directory/name/data ranges and unloaded module rejection.
- Native dispatch tests verify exact stdcall cleanup, actual platform tick hook,
  DWORD wrap, fast-forward and255/256-unit registered-name boundaries.
- The negative CP1252→UTF8 output mutation and fake successful palette mode fail
  independent x86 byte/capability/pixel assertions.

Full host/MinGW/Mesa and previous D/audio/GUI/GPU/C/E/F gates remain required. CI's
existing host job publishes stage-d-palette-strings with public PE/DLL/C/RC/logs and
this contract; still six jobs. Strict new providers/parser/CP helper and focused
ASan/UBSan plus standalone malformed-parser leak scan are additional evidence.
No private game payload/trace/image is published in git, CI artifacts or the site.
Exact-SHA all6 CI/Pages/NRO/artifact/clean source-manifest checks close publication.
Local Switch SDK is absent; CI builds the actual NRO.

## Observed private application boundary

Incoming e83d416 stopped at SetSystemPaletteUse with new UI and LoadStringA with
legacy. With shared strings/palette behavior implemented, the next imports were
timeGetTime (new) and RegisterWindowMessageA (legacy); those are implemented above.
A fresh bounded host new-UI run (-f12, snapshot at wait-frame8) now visibly renders
the Pinball table, logo and score-panel area and exits0 at scripted teardown.
This is an observed **first main-window/table frame**, not a playable/interactive/
performance/sound/gameplay acceptance. Ball/flipper input, sustained updates,
saving, real SFX/MIDI and physical Switch output remain unverified. Private original
assets and screenshots stay outside public artifacts. Legacy now reaches missing
USER32!FindWindowA; its full window API remains a separate compatibility queue.
No earlier missing-import observation is a permanent universal next blocker.

Next concrete work: public main-loop/input/message/timer and frame-progress oracle
before claiming game interaction; a public legacy FindWindowA/W reproducer before
extending legacy window lookup. In the audio queue, shared MCI/MediaPlayer/PlaySound
sources with mixed waveOut, then DirectSound audit/MIDI remain open. F6c/Luna,
default CPU, L legacy/ZL software preserved. Hardware sound/Paint/GPU/fallback/
input/relaunch remain deferred; SD recovery stays closed.

References: [SetSystemPaletteUse](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-setsystempaletteuse),
[LoadStringW](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-loadstringw),
[LoadStringA](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-loadstringa),
[timeGetTime](https://learn.microsoft.com/en-us/windows/win32/api/timeapi/nf-timeapi-timegettime).
