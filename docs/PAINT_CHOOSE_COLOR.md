# Paint: missing ChooseColorW export and common color dialog

## Problem

Paint dynamically resolves ChooseColorW through COMDLG32. This export was absent
from the host registry, leaving the application with a null delay-load pointer.
Implement the standard API and modal color picker rather than changing application
addresses or substituting an unconditional successful return.

## Change

ChooseColorA/W and CommDlgExtendedError resolve regardless of the ui2 layer.
With ui2 disabled, ChooseColor returns FALSE with CDERR_INITIALIZATION rather
than leaving a null callable address. The functional dialog uses the normal new
USER32/GDI controls and the existing modal message queue. No Paint-specific
address, menu offset or fixed MFC pointer is used.

The default dialog provides 48 basic colors, 16 custom slots, preview, numeric
RGB, Define custom colors, Add to custom colors, OK/Cancel and optional Help.
Expanded size is 580x420 (454 high with Help); compact width is 284. It fits the
1280x720 Switch Lite desktop and the host's 800x600 regression desktop.
COLORREF conversion is explicit and shared by A/W; the 32-bit CHOOSECOLOR size
is validated as 36 bytes. RGBINIT, FULLOPEN, PREVENTFULLOPEN and ENABLEHOOK are
handled. Unsupported custom dialog templates fail with an extended error; they
are not silently ignored or claimed implemented. The UI uses RGB entry rather
than the native Windows hue/saturation plane.

CCHookProc receives WM_INITDIALOG after the controls exist, with the original
CHOOSECOLOR pointer and caller's lCustData. Registered SETRGBSTRING updates the
selection. COLOROKSTRING exposes the proposed rgbResult and honors a veto.
The common-dialog code and guest RegisterWindowMessage share one registry, so
ANSI/Unicode callers receive identical registered IDs. Callback reentry uses
request-local state, and guest window handles are revalidated after callbacks.

Modal state is owned by the HLE call's stack and removed on unwind. Normal exit
restores the owner's previous enabled state and focus; cancellation does not
commit rgbResult. Custom slot edits are written to lpCustColors immediately,
including when the user subsequently cancels. Quit is reposted, callbacks can
close the dialog, and host cancellation/ExitProcess stop further guest work.
CPU GDI painting feeds the existing software/GPU presentation path unchanged.

## Validation

- gui_choosecolor.exe: actual x86 dynamic lookup A/W, structure/hook errors,
  RGB/custom slots, cancel, hook confirmation veto, nested dialog, self-destroy,
  quit repost, compact expansion/PREVENTFULLOPEN and owner restore.
- Scripted real mouse selection/OK without a guest hook returns the chosen red;
  the dialog was rendered and visually inspected with the real bundled fonts.
- test_choose_color: actual x86 callback ExitProcess(42), plus a modal native
  continuation parked in platform input then canceled by host executor teardown.
  It verifies that all thread windows, focus and capture are released. Each mode
  runs in a fresh host process, matching normal launch and avoiding unrelated
  process-global CRT command-line state across test scenarios.
- Full host/guest suite and native/GL CI remain required for the new exact SHA.

Switch check: start Paint's new UI with L as before, open its color picker,
choose a basic color, confirm, and draw with it. Also change numeric RGB, add a
custom color, reopen the dialog, and cancel once. Preserve the renderer/execution
reports after normal Paint exit. This is the remaining application check for the
reported path; GPU geometry/input tests need not be repeated separately.
