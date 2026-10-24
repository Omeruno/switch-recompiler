# F4a: one active application on the native desktop

This package follows F3b at `9c6e4d4`. The user's new priority moves the active
workspace before F3c preferences; F3c is still open, not skipped as completed.

## Launch and visible behavior

All EXEs, **including Paint**, select the new USER/GDI path by default. Hold **L**
at launch for the explicit legacy compatibility path. Hold **ZL** for software
output while retaining the new UI and native desktop. Selecting legacy still
uses its separate old frontend; it does not participate in the F4a workspace.
This selects a renderer/runtime path, not a different Paint executable. Real MFC
DLL loading rules remain unchanged; private application binaries are not shipped.

The host-owned DesktopShell survives each guest. On the worker it supplies the
background for exposed USER desktop pixels, plus a native taskbar and Start/menu/
Explorer overlay above the guest's real windows. Guest window coordinates are
screen coordinates, not a scaled screenshot inside a fabricated application frame.
The logical screen stays 1280x720; SPI_GETWORKAREA/GetMonitorInfo and top-level
maximize reserve the bottom 40 pixels. Moving/hiding/destroying a guest window
repaints the same blue desktop instead of the old separate blank environment.

The single task button minimizes the visible top-level windows of the one
application (bounded at 64) and restores those handles in reverse z-order. A
window minimized via its own titlebar can also be restored with the task button.
Normal and maximized placement are retained across minimize/restore. No new
Process or simultaneous application is implied. The app keeps executing when
minimized. The native shell returns to its prior Explorer path/panel after normal
exit, host abort or failed load. GuestSession/GuestExecution still own teardown.

R toggles Start while the new workspace is active; stick/touch/A operate the
native controls and guest windows. X/Y/arrows/B operate native panels while they
have focus. Clicking a guest window returns keyboard routing to it. Start can
open the real Computer/Documents provider and About during execution. Address
input uses the native keyboard; launch confirmation is quarantined until release.
F4b.1 supersedes the original pump dependency: native service now runs between
executor quanta; guest events and owner-window commands wait for real USER
dispatch. See [F4B_INPUT_SERVICE.md](F4B_INPUT_SERVICE.md). This still does not
preempt host APIs that never yield.
Run, document association changes and homebrew exit are disabled while an app
runs. Launching another EXE is blocked in both pointer and keyboard paths; folder
navigation remains usable. PLUS retains the existing guest-close/abort route.

## Ownership and composition

Main thread selection completes before the worker accesses DesktopShell, and
main waits until worker join before touching it again. ShellSession is declared
before GuestExecution and destroyed after its continuations drain, before the
GuestSession releases USER state/memory. It restores prior Platform hooks and
work area, clears its binding and releases its three fallible 1280x720 RGB buffers
(11,059,200 bytes). Allocation failure returns to the picker; no half-active
workspace is retained.

The optional Platform background callback writes only exposed desktop regions
through Surface.Set, preserving CPU screen semantics and capture invalidation.
Native overlay revisions request presentation independently of the CPU GDI change
counter, so Start/clock changes do not masquerade as guest raster changes in E8
input correlation. The input filter runs before USER enqueues events. Native press/release pairs
are consumed together. A guest drag/capture keeps its route while crossing
native chrome; shell gestures do not release into a guest. Start navigation
cannot steal a held guest gesture. Unconsumed guest key releases stay deliverable.

F4a uses a **flat CPU composition of native chrome and canonical guest pixels**
followed by the existing GPU or software presentation. Native overlays do not
modify guest window pixels or appear in guest GetPixel reads. GPU output and
software fallback remain available, but this adapter intentionally does not use
E6 independent window textures; existing non-shell E6 paths/tests are retained.
No performance improvement is claimed. F4b must extend the overlay to retained
layers and validate its complete CPU/GL ownership oracle before removing this
conservative path. There is no concurrent main-thread compositor or output owner.

## Theme and checks

The shell now uses an XP-like plain blue desktop, blue titlebar highlight,
red close control, blue/green taskbar, single active task and icon-bearing
Start columns. Existing pinned public-domain Tango icons and OFL Liberation
fonts remain the only imported art. This is original native drawing, not Microsoft
logos, proprietary XP resources, imported XP.css or a claim of pixel-identical XP.
Guest USER controls still use their supported classic theme; that visual work
remains separate from this native-shell refinement.

`tests/guest/shell_workspace.c` is a public MinGW x86 app with two owned windows.
`tests/test_shell_workspace.cpp` executes it through normal exit → host abort →
normal restart with the production shell/filter. It checks actual work-area
APIs, desktop exposure, native overlays, Start without input leakage, real folder
browsing, blocking second launches, task minimize/restore, maximized restoration,
guest click delivery, cross-boundary gestures and hook/session cleanup. Optional
PPM output captures the actual native composition. The complete MinGW/Mesa suite
retains F1/F3b/lifecycle, Stage C/E, CPU/actual-GL and input/dialog regressions.

Local devkitA64/libnx remains unavailable. Exact-SHA Switch CI compiles the adapter;
Paint/GPU/fallback/Lite/input/relaunch physical checks remain deferred in
[STAGE_E_PERFORMANCE.md](STAGE_E_PERFORMANCE.md). Host tests do not certify them.

## Remaining order

Latest priority: persistent frontend/output lifetime and retained composition;
real file operations/properties and context/input actions; F3c preferences and
TXT/image tools; F5 public save/find/reopen. Visual Luna refinement is deferred. G profiling remains available, optimization deferred.
F4a does not complete Stage F, arbitrary EXE compatibility or full Windows XP.
