# F0: legacy UI/MFC session lifetime

GuestSession now also opens LegacySession after binding Process and before the
USER/GDI scope. Compatibility globals remain the existing frontend facade, but
image/resource bindings, legacy window classes/windows, message/paint/quit queue,
timers, menu/dialog/about data, Paint CDC/View/request flags and dynamic MFC stub
slots are exchanged with fresh owned storage for every guest. On scope exit the
previous native context is restored and the session's host objects are discarded.
GuestExecution drains first; presentation/frontend cleanup follows; only then are
legacy/GUI resources discarded, Process state destroyed and guest memory freed.
Final discard executes no guest callbacks.

Host tick/pump/audio/console callbacks, trace configuration, stack diagnostic
pointer and selected UI layer are inherited and restored if changed during a
launch. Device ownership remains with the frontend. The Switch pump/stack binding
now occurs inside RunInterpreter's session, so a completed GameThread does not
leave its stack marker as a new persistent binding. The EXE/resource pointer is
cleared before loading and restored/removed before its Module image disappears.

Private legacy HLE guest-address caches are Process-owned: MFC application and
empty CString, document/view/window/message-map relations, current MSG/focus/
activation/capture/cursor, CDC vtable, synthetic font counter, error state,
MFC-frame registration and stock system brushes. They use the existing RTTI-free
LocalState owner and cannot be selected merely by reusing an old native address.
The fallback callback nesting counter is also process-local.

Registry values intentionally remain host-owned application settings. Only open
HKEY path handles are exchanged; an inner/new guest cannot use an outer guest's
open handle before opening its own key, while registry value changes survive
session exit. No registry file is deleted/replaced by session cleanup. Native
restoration checks verify this distinction without writing to SD.

Legacy frontend scratch is reset at entry and exit: menu/toolbar/palette/dialog
hit rectangles, open menu/tool/color selection, content mapping, cursor/touch
history, click ownership/remembered HWNDs and polling throttles. Pad/framebuffer/
audio services remain frontend-owned. New USER's 1280x720 geometry, GDI/ROP,
output/fallback and input paths retain their existing implementation.

## Automatic coverage

The public gui_session_legacy fixture uses ordinary x86 USER/GDI and the existing
synthetic MFC ABI via runtime ordinal lookup. It contains no proprietary Paint or
MFC binary and does not claim general MFC compatibility. Its static document
address is asserted equal across all nine launches in the same host process and
placement-new GuestSession storage.

Five ordinary runs alternate with a sleeping host abort, ExitProcess from
DialogBox initialization, parked DialogBox abort and parked ShellAbout abort.
Checks include fresh document/view state, valid empty CString after CDC creation,
valid callable CDC vtable, GDI stock-brush pixels, actual WndProc dispatch, loaded
menu/resource data, inherited/restored callbacks, fresh class/window/queue/timer
state and no resumed guest continuation after cancellation. The fixture leaves
WM_QUIT, timers, windows and pending Paint command pointers for final discard.
Native nested binding checks restore outer window/queue/timer/image/stub state;
a failed EXE load is destroyed before the first successful launch.

The initial fixture called unsupported legacy USER32 SetFocus. It was corrected
to use the already implemented MFC CWnd::SetFocus ordinal. No new SetFocus
implementation or compatibility claim was introduced to hide that test error.

## Subsequent frontend integration

The production return-to-picker loop was later integrated in F0_LAUNCH_LOOP.md:
picker selection/cancellation, per-launch mode/output rebind, startup-held input
quarantine and normal/failed/aborted return. It reuses core/new-USER/legacy scopes
and has a dedicated host controller regression. The next software milestone is
F1–F5 in F_SHELL_PLAN.md, not another implementation of the same F0 loop.
Hardware remains one combined GPU/software/Lite input/Paint/picker/relaunch
checklist in STAGE_E_PERFORMANCE.md. No physical console timing, memory, real
Paint or final F0 shell acceptance is asserted from these host fixtures.
