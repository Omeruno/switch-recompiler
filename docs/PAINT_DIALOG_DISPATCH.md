# Standard dialog dispatch: preserve framework subclasses

> Current launch policy: new UI is the default for every EXE, including Paint;
> **L now requests legacy compatibility**, ZL software output. Older L-for-Paint
> instructions below describe their historical build. Active workspace: [F4a](F4_WORKSPACE.md).


## Reproduced defect

CreateFromTemplate unconditionally replaced a standard dialog's window procedure
with DefDlgProc *after* CreateWindowInternal returned. A framework can subclass
the dialog in its HCBT_CREATEWND hook. That subclass received early creation
messages, then was overwritten. WM_INITDIALOG also bypassed the window procedure
by calling the original DLGPROC directly. Later OK/Cancel/WM_CLOSE commands
therefore missed handlers installed by the framework.

A real x86 fixture reproduces this without proprietary application binaries:
a CBT hook installs a window subclass whose command/close handlers finish the
dialog. The preceding implementation fails OK, Cancel and close scenarios, and
never sends WM_INITDIALOG through the subclass. The repaired implementation
passes them. This reproduces the framework pattern used by MFC; physical Paint
acceptance remains required rather than inferred from the synthetic executable.

A second gap was DWL_DLGPROC: the callback lived in DialogInfo, while generic
Get/SetWindowLong accessed a separate, initially zero extra-memory slot. Replacing
that slot did not replace the callback actually invoked by DefDlgProc.

## Change

Dialog state is bound to the new HWND immediately after Window insertion and
before CBT/creation callbacks. The default A/W dialog window procedure is chosen
at this point, before a framework may subclass it. It is never restored over a
subclass afterwards. WM_INITDIALOG goes through SendMsg/current window procedure.

DWL_DLGPROC is initialized in the 32-bit dialog extra-memory slot, and DefDlgProc
reads that same slot. SetWindowLongA/W updates callback encoding for that dialog;
ordinary window extra memory is unaffected. DWL_MSGRESULT starts at zero for each
callback and is read when a callback returns TRUE, preserving documented dialog
result semantics rather than returning the callback's BOOL as a message result.

The pending-creation scope restores its predecessor, including nested dialog
creation from a CBT hook. A pending request claims only its own top-level HWND;
children and unrelated nested windows do not inherit its DialogInfo. Retiring a
window clears dialog metadata even when a subclass suppresses WM_NCDESTROY
forwarding or the runtime forcibly cleans up a stopped GUI thread.

RunModal now respects a false FetchMessage result and execution cancellation;
it no longer loops on empty messages after host abort. Owner style is restored,
and teardown avoids invoking more guest callbacks once cancellation is active.

## Validation

- gui_dialog_subclass: real x86 modal OK/Cancel/close; modeless CreateDialog with
  an application-owned message loop; subclass WM_INITDIALOG; DWL_DLGPROC replacement
  both during initialization and from CBT; nested creation before the outer
  window finishes creation; ANSI replacement receiving a Unicode text message;
  DWL_MSGRESULT handled value and subsequent zero reset.
- test_dialog_lifecycle: parked DialogBox continuation canceled by the host and
  ExitProcess(42) from WM_INITDIALOG, with no surviving windows/focus/capture.
- Existing resource dialogs, color picker/hook/nesting/lifetime, open/save,
  GUI ownership, presentation and Stage C suites remain mandatory.

API references: Microsoft Learn SetWindowLongW/GetWindowLongW (DWL_DLGPROC),
DLGPROC (DWL_MSGRESULT) and WM_INITDIALOG. No Paint addresses, MFC object offsets,
forced OK/Cancel results or application-specific window procedures are used.

Physical check: new-UI Paint (L), test Flip/Rotate, Stretch/Skew and Attributes
using OK, Cancel and the titlebar close button. Confirm that OK applies the
chosen operation and that the following window still responds to input.
