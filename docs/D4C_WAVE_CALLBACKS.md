# D4c — scheduled guest waveOut function callbacks and real event objects

Current extension: [D4d](D4D_AUDIO_MEDIA.md) adds real device controls, more PCM formats, MP3/MCI and native Media Player; historical package bounds/next steps below are not the current global queue.

Extends [D4b](D4B_WAVE_STREAMING.md) without changing its PCM format, one-client
exclusivity, four-device-buffer budget or release/reset ownership rules.
CALLBACK_FUNCTION and CALLBACK_EVENT now have implementations. Position,
pause/restart, other formats/resampling, mixing and MCI/MIDI remain separate work.

## Guest function execution

A function-mode open validates a nonzero mapped callback address and creates one
reusable service GuestThread per Process, through the production thread allocator.
It has its own CPU, stack, TEB, dynamic/static TLS and normal module thread attach/
detach. The worker runs on an ordinary GuestExecution continuation, using the
existing guest stdcall interpreter/SEH/host-call path. Each notification supplies
five DWORDs: HWAVEOUT, WOM_OPEN/DONE/CLOSE, original dwInstance, WAVEHDR for DONE
(or zero for OPEN/CLOSE), zero. Guest function code never runs inside audio Poll,
reset, a driver observer or the notification producer. This is not a user APC and
needs no alertable wait in the submitting application.

A Process-owned queue contains at most128 value-only notifications. One additional
notification may be executing/parked on the service continuation. The dispatcher
sleeps when empty. Enqueue wakes only its own idle wait, never a guest callback's
WaitForSingleObject/Sleep/other wait. A blocking callback therefore preserves
ordinary scheduler fairness and backpressure. OPEN/DONE/CLOSE retain enqueue order
across close/reopen; records already accepted before a successful close can still
run afterward with that original handle/instance/header value. They do not retain
borrowed guest buffer or Stream pointers. The caller must keep code, instance and
header storage valid until its callbacks finish.

A full queue refuses OPEN/CLOSE (NOMEM and no successful close/handle publication).
A completion that cannot be enqueued latches the D4b stream error after actual
buffer retirement; no fictitious DONE or unlimited allocation. Function-mode
write also refuses a dead/exiting dispatcher. A callback that explicitly exits
its worker via ExitThread makes that Process's dispatcher unavailable; this
bounded implementation does not silently replay callbacks or create unbounded
replacement threads. Reset can revoke PCM, but CLOSE cannot claim success if its
CLOSE callback cannot be queued. Session teardown safely releases that state.
Invalid/unhandled guest callback code retains the existing interpreter/SEH error
policy; mapped code alone does not certify arbitrary callback correctness.

The service thread is background metadata in GuestScheduler: it receives normal
quanta but does not count toward application process liveness. The last real
application ExitThread follows normal process shutdown and drains every service
continuation. Callback ExitProcess uses normal process-exit/detach machinery;
callback ExitThread affects only that thread while the application remains live.
Host cancellation unwinds parked native callback frames before guest TEB/stack/
memory are reclaimed. GuestSession disables and discards queued notifications
without executing guest code during its destructor. Nested sessions own separate
queues/workers/events and cannot write or dispatch into the suspended Process.

## Events

Event-mode open requires an actual Process Event object and EVENT_MODIFY_STATE
access on the supplied handle. Null/stale/wrong-kind/read-only references fail
INVALPARAM without publishing HWAVEOUT. A strong object reference belongs to the
stream, so closing the opening HANDLE cannot retarget subsequent notifications;
existing aliases still observe the same object. This explicitly bounded owned
reference policy does not certify every native Windows close-during-use behavior.

OPEN/DONE/CLOSE signal the captured object through the same signaled bit and
SignalObject path used by kernel SetEvent. Waiters on every alias use existing
manual-reset/auto-reset and synchronization semantics. These are coalescing event
signals, not a count of completed headers. Clients must inspect their WAVEHDRs.
No USER queue or alertable wait is required. DONE is signaled only after the same
actual release/confirmed cancellation used by all other callback modes.

## Public tests and evidence

`wave_callbacks.exe` is a CRT-free, repository-authored MinGW PE32 with authored
PCM and ordinary kernel32/winmm imports. The native inspector captures actual
immutable PCM and controls release/cancellation; this is not a speaker test.
The full host/MinGW/Mesa runner executes it and publishes
`stage-d-wave-callbacks` (PE32/log/this contract), retaining all six core CI jobs.

Ten isolated sessions exercise function/event modes, reopen,128-record pressure,
non-alertable blocked callbacks, separate CPU/FS/TEB/TLS/last-error state, five
stdcall arguments, actual WAVEHDR state, manual/auto-reset waits, event type and
rights, retained aliases, callback ExitProcess/ExitThread, last application
ExitThread, nested owner restoration and one deliberately parked callback abort.
ExitProcess37 and last-thread55 are intentional process exit statuses, not
ordinary exit0. The abort is not a normal process termination. Combined PCM
accounting is27 accepted,9 confirmed releases and18 confirmed cancellations.
The scheduler regression separately checks service fairness/application liveness.

D4a's old unsupported-function request now selects an unsupported callback type
mask instead; D4b's NULL/WINDOW/THREAD inspector remains unchanged. These are
adaptations to expanded functionality, not bypasses of the new behavior.

Local devkitA64/libnx is absent. Exact-published-SHA CI must build the Switch NRO
and pass host, CPU oracle, actual Mesa GL, ARM64/QEMU continuation ABI, Switch and
isolated JIT probe jobs. No physical sound, reset latency, Paint/GPU/input/relaunch
or game acceptance is inferred. SD recovery stays closed. Private Pinball's
truthful no-device host path still reaches missing mciSendCommandA; MCI/music is a
later common backend dependency, never a successful signature-only replacement.

## Next block

D4d: real frame-position accounting and pause/restart, then selected streaming
formats with continuous resampler state across buffer boundaries. Preserve
confirmed played-versus-cancelled accounting, all callback modes and ownership/
reset/error/teardown gates. Only advertise formats/capabilities with real support.
