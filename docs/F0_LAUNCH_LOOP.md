# F0 frontend launch/return loop — 2026-10-23

The production Switch main uses `ui::RunLauncher`, the same lifecycle controller
exercised by the native restart/failure/shutdown test. Host services (ROMFS,
registry values, audio device) live across launches. Each launch chooses the
existing Paint/default or L-overridden UI mode and ZL software override again.
It closes the console before creating EGL/framebuffer output, starts one worker,
waits for it, closes its thread handle and output, then restores the console.

The worker owns GuestSession, GuestExecution and presentation teardown. Native
continuations drain before guest memory and windows disappear. In GPU mode the
worker's presentation teardown destroys GL resources and releases/destroys EGL
before returning; the main thread's idempotent output close also covers failed
initialization and thread creation/start. Audio buffers remain host-owned and
are not freed while the output device can still reference them.

Only the first selection may autolaunch a sole EXE. Return always shows a picker,
rescans the bounded catalog, and displays a bounded constant status on load/DLL/
thread/display failure. It never automatically retries a broken sole EXE.
B/PLUS exits the picker. Buttons held on picker entry belong to the previous
screen until release. Both guest UI paths similarly quarantine startup buttons
and touch. PLUS is also sampled every 4096 execution turns to cancel guests
that do not call a GUI pump. This is a host abort, not delivery of WM_CLOSE.

Applet shutdown is sticky: picker, both UI pumps and periodic interpreter polling
observe the same latch. Shutdown drains the guest and output but does not reopen
the console/picker. Polling cannot preempt a host API that never yields/returns.
No simultaneous running guests, desktop shell or new Win32 API support is implied.

Validation: native controller tests cover three sequential launches, initial-only
autolaunch permission, partial display failure with cleanup/retry, picker and
guest applet shutdown, and held-input release/repress. Existing real public x86
core/new USER/legacy regressions validate the GuestSession scopes used by the
worker. Host tests do not execute libnx main, EGL thread transfer or console
reinitialization. Switch CI compilation is necessary; actual repeated launch,
Paint, GPU/fallback, 1280x720 and memory stability remain on the single physical
checklist in STAGE_E_PERFORMANCE.md.

The subsequent G0 census was implemented in STAGE_G_PROFILING.md. After the
2026-10-23 user-priority audit, F1 persistent SD roots/filesystem contracts were implemented next; the current
next package is F2 native desktop/theme in F_SHELL_PLAN.md.
F0 alone is not desktop/Start/explorer acceptance; hardware checks remain deferred.
