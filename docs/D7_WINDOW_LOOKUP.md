# D7 — shared checked window lookup, real new/legacy stores

FindWindowA/W and FindWindowExA/W are always-registered stdcall8/16 providers.
They search the actual active USER store, never a synthetic success handle or
another UI's windows. This follows a private legacy Pinball import, but acceptance
uses the authored redistributable window_lookup.exe and native inspector.

## Lookup contract

FindWindow searches top-level windows, including hidden and owned popup windows;
it does not descend into children. FindWindowEx searches only direct children of
its actual parent (NULL selects top-level), beginning strictly after hwndChildAfter.
The latter must be a member of that same direct-child list; invalid parent/after
refuses with error1400. New USER uses its real front-to-back children order. Legacy
has no activation/Z-order reordering: lookup uses its recorded newest-first creation
order, not unordered_map iteration. This does not add a complete legacy Z-order API.

NULL class/title is a wildcard; non-NULL empty text matches only an empty field.
A low16-bit nonzero class argument is an actual registered class atom, not a guest
address or wildcard. Names and titles compare without ASCII case distinctions;
other UTF16 code units compare literally. Full Unicode case folding/localization
is outside this fixed profile. A strings decode through the actual runtime CP1252
conversion, including Euro0x80. W strings retain UTF16 units. Lookup reads each
supplied string before searching, validates mapping and32-bit wrap, and accepts at
most32767 units plus NUL. Bad memory refuses998; no terminator in32768 units refuses
206. No guest/native pointer escapes the synchronous call. Hit and ordinary miss
preserve LastError. This is not an exhaustive Windows last-error precedence oracle.

Neither store implements message-only windows; HWND_MESSAGE refuses120 explicitly
rather than accidentally searching desktop children. This is not a cross-process
window namespace or WM_GETTEXT/hung-window timeout implementation. Reading actual
stored text does not call a guest procedure or create a continuation.

## Legacy metadata and lifetime

The existing legacy WindowManager now owns a UTF16 title for windows created through
CreateWindowExW, real class atoms, and deterministic creation order. RegisterClassW
keeps its class name in UTF8 instead of lossy ASCII narrowing. CreateWindowExW resolves
actual class atoms, preserves Unicode titles, and records a parent only for WS_CHILD;
owned popups remain top-level for lookup. Invalid child parents refuse1400.
The shared bounded reader validates these W strings too. Existing non-client,
callbacks, rendering and restricted legacy class-registration semantics are not
replaced or certified as complete Windows behavior by this package.

LegacySession already exchanges the whole owning manager. New metadata/order is
therefore discarded/restored with the existing window/class storage. No lookup
cache survives a guest session. Legacy DestroyWindow/SetWindowText and general
legacy creation/activation semantics are not added here. New USER rename/destruction
uses its existing real implementations; no parallel title cache is introduced.

## Public evidence and validation

- tests/guest/window_lookup.c creates actual W classes and hidden top-level,
  child/grandchild/owned/empty-title windows through interpreted x86 imports.
  It checks A/W CP1252, real class atoms, ASCII case variants, wildcard versus
  empty, direct-child boundaries, after ordering, hits/misses/LastError and bad
  pointers. In new USER it also checks real rename, parent/child/owned destruction
  and absence from lookup. Legacy intentionally leaves windows for session cleanup.
- tests/test_window_lookup.cpp runs four fresh normal sessions alternating new and
  legacy, inspecting actual window/atom/title identity, then aborts a fifth new-UI
  session parked in Sleep with live windows. A subsequent fresh session has no
  prior windows in either store. Native real dispatch checks exact stdcall cleanup,
  maximum/overlong names, mapping-end failure and message-only refusal.
- Full host/MinGW/Mesa with the prior C/E/F/GPU/D/audio gates remains required.
  Focused ASan/UBSan and strict changed-provider/manager builds supplement it;
  unrelated runtime objects remain ordinary and fiber leak scanning is disabled.
  A negative constant-miss provider must fail the public x86 window identity test.
- Existing six-job CI publishes stage-d-window-lookup: public PE32, guest/native
  sources, log and this contract. No private game file/image/trace is published.
  Exact-SHA CI/Pages/NRO/public-artifact source identity closes publication.

## Actual application boundary and next work

Private legacy diagnostic now passes FindWindowA (a genuine miss before any matching
window exists) and reaches missing USER32!LoadIconA. It has not produced an accepted
main game frame. D6's new-UI first table/logo/score-panel frame remains the observed
new-UI boundary; a bounded new-UI regression must preserve it. No ball/flipper input,
sustained animation, sound/MIDI, FPS or Switch acceptance follows from lookup.

Next concrete block: authored main-loop/input/message/timer/frame-progress oracle
before claiming game interaction. Legacy icon/resource APIs need a separate public
reproducer and real resource/lifetime semantics, not a successful placeholder.
Shared MCI/MediaPlayer/PlaySound sources with mixed waveOut and then DirectSound/MIDI
remain open. CPU GDI default, Claude Luna, L legacy and ZL software stay intact.
Local devkitA64/libnx is absent; CI builds NRO. Hardware sound/Paint/GPU/fallback/
input/relaunch stays deferred; SD recovery is closed.

Reference: [FindWindowW](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-findwindoww),
[FindWindowExW](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-findwindowexw).
