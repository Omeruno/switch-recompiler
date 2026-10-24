> Current integration: [retained workspace layers](F4C_WORKSPACE_LAYERS.md),
> [tree/persisted links](F3C_SHORTCUTS_TREE.md) and [F0–F5 acceptance](F_ACCEPTANCE.md)
> now implement the software baseline. Historical “remaining” sections below
> describe their original package; physical acceptance remains pending.

# F4b.2: persistent frontend and output lifetime

Follows clean `450f1d3` (CI 40081220508, six successful jobs). The functional
system queue takes precedence over further Luna artwork. This package changes
frontend/output ownership; file mutation/document tools remain separate work.

## One owner, independent guest sessions

One explicitly 2 MiB native frontend worker now runs selection, loading, guest
fibers, native shell input/rendering and shutdown. Main initializes services,
starts/joins that worker once and then cleans services. This keeps the deliberately
large native stack required by the old stack-overflow investigation. There is no
new worker/context transfer for each EXE and no concurrent renderer owner.

`RunWorkspace` scopes guest begin/run/end/return independently from final Close.
`OutputLifetime` opens the actual device once for a requested policy and retains
it through normal exit, failed load, host abort and subsequent selection. Same-
mode launches do not detach Presentation, destroy EGL, recreate framebuffer or
reinitialize shaders. RunInterpreter still creates a fresh GuestSession, and
GuestExecution continuations drain before that session disappears. A persistent
display does not mean reusing guest memory/windows/queues/handles.

Selection and return render the actual host-owned DesktopShell through the same
Presentation and software/GPU output as the active guest. The shell frame storage
is allocated once for this frontend lifetime. Loading gets a real shell frame
before interpreted startup; return draws shell state before the next catalog
scan. Initial sole-EXE autolaunch is preserved after an initial desktop frame.
No normal transition restores the text console or retains a fake application
window. Presenter snapshots are owned copies and may survive guest destruction.

Pointer position is preserved between native and new-UI guest phases (legacy
retains its existing separate cursor mapping); launch-held keys/
touch still require release. The real USER cursor state is initialized from that
position after EnableUi. Keyboard versus pointer cursor visibility is explicitly
submitted. Idle frames do not create guest input-correlation samples.

## Explicit transitions and recovery

L requests legacy UI (software framebuffer), ZL new UI with software output;
default new UI prefers GPU. Changing that policy can legitimately close/reopen
the device. EGL and libnx framebuffer are never intentionally owned together.
GPU initialization, upload/draw or swap failure selects the existing CPU snapshot
and software fallback. Fallback stays stable across same-policy launches; a
policy change or a restarted frontend permits a new GPU attempt. This is not a
promise of zero physical blink while changing modes or recovering a failed device.

Legacy output still writes the framebuffer directly. Returning from it explicitly
invalidates software output history so an unchanged native snapshot is repainted
rather than incorrectly treating the legacy framebuffer as preserved desktop pixels.

A render failure latches an unhealthy output state. The guest returns through
normal drain/cleanup; native selection can reopen the device. If allocation or
output cannot support the shell, cleanup precedes the existing console fallback.
A whole-frontend worker creation/start failure is shown on the initial console;
no output has been initialized by the failed worker. Applet shutdown never
restores selection/console. Final Close is idempotent on the owner worker.

## Metrics without device recreation

Guest Begin resets Presentation and GPU counters rather than destroying their
backends. `OpenGlPresentation::ResetStats` preserves shaders, textures and upload
revision caches while discarding old ended-query samples without waiting for
results. The fixed query objects can be reused in command order. End still writes
the per-guest renderer report, disables raster/input measurement and drops guest
layer snapshots; device shutdown belongs to the frontend. Fallback counts in the
report are the delta within that guest's measurement scope. No speedup, total
memory stability or physical latency is claimed.

## Checks

`test_workspace_output` uses the production RunWorkspace/OutputLifetime controller
and a real software pixel output. Four x86 sessions cover normal completion,
failed load, host abort and restart with exactly one device open and one final
close. Actual guest GDI pixels are checked, then the complete native desktop and
padding are checked on every return. Owned replay is checked after guest windows
and memory disappear. Mode changes, partial-open failure/retry and shutdown at
selection or within a guest verify resource cleanup and initial-only autolaunch
permission. The software device intentionally tests the fallback contract; it
is not a claim of Switch EGL execution.

The existing live-guest EGL/Mesa oracle additionally resets GPU metrics within a
window scene, verifies retained texture memory and zero repeat uploads, rejects
old timer samples and compares every output pixel again. Both native and classic
sizes, the full host/MinGW/Mesa suite and strict native compilation remain gates.
Switch main is excluded from host builds, so the exact published-SHA Switch CI
is essential. Local devkitA64/libnx is unavailable (official package endpoints
currently return HTTP 403). Physical Paint/GPU/fallback/Lite/input/relaunch checks
remain deferred; SD diagnosis remains closed.

## Remaining work

Native/guest independent GPU planes remain the F4a conservative flat composition
boundary, despite persistent EGL/context ownership. Next functional package:
shared real file operations and stat-backed properties, then consistent context/
controller input, persisted settings/associations and TXT/image tools. Complete
public save -> find -> reopen (F5) before further visual polish. Full Windows,
multiple guest Processes and non-yielding-host-call preemption are not delivered.
