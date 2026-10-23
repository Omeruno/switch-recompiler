# F0: new USER/GDI session ownership

GuestSession now opens a GuiSession after binding its Process. The session owns
fresh active USER State and both GDI object stores behind the existing global
facade. Destruction exchanges the previous native context back and discards the
session's objects while guest memory still exists. No guest callback runs during
this final discard. GuestExecution must drain before presentation cleanup, which
must finish before GuestSession destruction (the existing production declaration
order is preserved).

## Storage and cache rules

USER State is owned by a unique_ptr and exchanged by pointer. It must not be
moved: WindowLayers contains capture/version pointers into its own slots. Native
nested binding tests retain a live capture across an inner scope and verify the
same capture/owner addresses and outer GDI objects survive restoration. This is
native context restoration, not nested running GuestExecution support.

GDI stores are exchanged by move, preserving their heap-owned object addresses.
The active screen points to the separately stable USER State. System colors now
belong to the active Gdi object. USER State owns system-brush handles, placement
cascade, clipboard text and polling throttle in addition to its existing windows,
classes, queues, timers, focus, capture, screen and retained layers.

Process-owned HLE data now includes menu trees/system-menu/font handles, popup
modal state/class registration, accelerators, icons/system-image handles, hooks
and nesting depth, scrollbar data/stock pattern, deferred window positions,
cursor handles/count, standard-dialog metadata/pending creation/about icon,
common file/color dialog request maps/errors and status-bar data. Request maps
remain non-owning views of native frames: continuation draining removes those
frames before their Process storage is discarded.

The host-procedure registry, cached host-only DefDlgProc/About thunk addresses,
frontend Platform callbacks and registered message-name namespace remain host
infrastructure. They contain no borrowed guest callback addresses or buffers.
Registered messages retain the existing 16-bit namespace/exhaustion behavior.
Font resources remain host-owned. No Win32 drawing/ROP/readback implementation,
output geometry or fallback selection is replaced by this ownership change.

EnableUi before GuestSession remains supported: configured desktop size is
inherited and initialized for the new state. Main's initialization after binding
also remains supported. The existing raw Process native harnesses retain their
historical active USER facade; production uses GuestSession.

## Automatic evidence

`test_gui_sessions` runs in one host process, deliberately reusing placement-new
GuestSession storage. At 1280x720 it executes:

- Four real x86 window sessions, alternating normal exit and cancellation after
  painting. The public fixture leaves class/menu/accelerator/hook/icon/DC/DIB,
  timer/focus/capture and changed system colors for teardown. Restart requires an
  unregistered custom class, no old hook invocation, fresh DIB bytes, default
  colors/common-dialog error and independent objects.
- Four standard DialogBox sessions and four ChooseColor sessions, alternating
  ExitProcess from an initialization/hook callback and host cancellation while
  the modal native continuation is parked.
- Failed EXE loading after GUI initialization, plus native nested restoration
  with live window-layer capture and GDI object references.

All continuations must be dead, only the desktop may remain and focus/capture
must be released before final host-resource discard. An owned presentation frame
is rendered before teardown, then redrawn after windows/DIB/layers/guest memory
are destroyed and compared over the whole output. The software renderer is
reused across the four sessions. Existing full CPU/actual-GL composition and
fallback tests remain mandatory; the new regression is not physical GPU evidence.

## Remaining frontend gate

The legacy UI/MFC and image/callback lifetime follow-up is now implemented; see
[F0_LEGACY_SESSIONS.md](F0_LEGACY_SESSIONS.md). It adds nine sequential legacy
fixture runs and scopes the remaining runtime/frontend scratch bindings.
That later bounded picker-return-picker launch loop and per-launch mode/output/
input integration is now implemented in F0_LAUNCH_LOOP.md. This document's
USER/GDI regression alone did not enable it and does not certify physical
relaunch/Paint behavior. Next software milestone: F3–F5 in F_SHELL_PLAN.md.

Keep the physical GPU/forced software/Lite input/Paint checks in
STAGE_E_PERFORMANCE.md. No physical memory/timing result is added here.
