# D4b — real bounded waveOut streaming and queued notifications

Current extension: [D4d](D4D_AUDIO_MEDIA.md) adds real device controls, more PCM formats, MP3/MCI and native Media Player; historical package bounds/next steps below are not the current global queue.

Historical package scope: [D4c](D4C_WAVE_CALLBACKS.md) now adds real scheduled guest function/event callbacks; old unsupported-mode checks select CALLBACK_TYPEMASK. The original package boundaries below describe their implementation stage.

Implemented after D4a, 2026-10-28. Ordinary x86 WinMM calls now own real stream
and prepared-buffer state, submit PCM to the D4a device and retire WAVEHDR only
on confirmed release or cancellation. No dummy handle or admission-time DONE.

## Supported surface

waveOutOpen (query and streaming), waveOutPrepareHeader, waveOutWrite,
waveOutUnprepareHeader, waveOutReset and waveOutClose. Existing device count/
A/W caps and PlaySound use the same real availability/PCM provider.

One open stream per active Process/device, canonical PCM1/48000/stereo16 only,
matching existing caps/format-query. One globally monotonic opaque HWAVEOUT is
allocated after validation; old handles are never reused across fresh sessions.
A second open or admission while PlaySound is pending returns ALLOCATED. An open
stream claims output exclusively: both new PlaySound and PlaySound(NULL) refuse
with busy, so they cannot reset someone else's queued PCM. No mixer/multiple
stream or arbitrary legacy-game format compatibility is inferred.

CALLBACK_NULL, CALLBACK_WINDOW and CALLBACK_THREAD are implemented. Window/thread
targets must be live in this Process and already have a new USER queue; legacy
USER callbacks explicitly refuse. WINDOW/THREAD post MM_WOM_OPEN/CLOSE/DONE with
actual HWAVEOUT in wParam and WAVEHDR in lParam for DONE. They use real checked
USER queues and guest WndProc/DispatchMessage/PeekMessage delivery. Notifications
never execute guest code on a device or scheduler-service stack. Destroyed or
closed targets discard later notifications; a1024-posted-message limit latches
stream error rather than growing audio notification memory without limit.

CALLBACK_FUNCTION and CALLBACK_EVENT, looping, pause/restart/position/volume,
multiple streams, mixing and additional formats are unimplemented and are not
certified by these tests. Unsupported open flags/callback modes return
NOTSUPPORTED without writing a handle. Initial query still only validates format
and leaves the output handle untouched. APIs not listed here are separate work.

## Prepared buffers and submission

Guest WAVEHDR layout is checked32bytes (cbwh>=32); no request-sized allocation.
Preparation records real data address/capacity and monotonic header generation,
checks mapped input, whole4-byte frames and at most5s PCM, and sets PREPARED.
Up to64 prepared headers are allowed; duplicate preparation is idempotent when
not queued. No fake PREPARED bit can replace provider-side preparation.

Write rechecks header/data mapping, fixed prepared data pointer and length not
exceeding prepared capacity, rejects unsupported loop flags, then snapshots the
actual little-endian PCM. Admission only sets PREPARED|INQUEUE and clears DONE.
Four queued blocks maximum; refusal preserves header flags and preparation.
Unprepared, already queued, bad pointers/size/data and stale handles return real
MM errors. Unprepare/close while a header remains queued return STILLPLAYING.
Data/flags/user fields modified by the guest cannot alter already-owned PCM.
The service leaves dwUser and other unrelated WAVEHDR fields untouched.

D4a tickets now retain a host-only retirement observer; FALSE means actual device
release, TRUE means confirmed reset cancellation. Observer only appends bounded
weak-session retirement records (at most4) and executes no guest function. The
normal GuestExecution service polls the device and applies active-Process
retirements even if the app makes no further WinMM call. DONE/INQUEUE and matching
window/thread notification are updated once, with header generation and mapped
range revalidation. Unmapped header retirement latches an error and never writes
stale memory. Such a stream refuses writes; close can dispose its metadata safely.

Reset first requires a successful device revoke; failure keeps pending headers
and callbacks unchanged. Confirmed cancellation marks queued headers DONE and
posts their notifications, while output counters distinguish played/cancelled.
Reset retains the open handle and preparation for reuse. Close requires returned
buffers, checks reset, posts CLOSE and clears owned preparation/claim. A failed
notification enqueue or revoke does not invent successful close. Callback queue
exhaustion/unmapped buffers are bounded error cases, not full Windows fault or
unlimited notification behavior.

## Session and teardown contract

AudioScope snapshots/restores the previous client claim as well as owner identity.
Switching a nested guest cancels the one physical output; retired outer records
wait in the outer Process until it becomes active. They do not update inactive
memory or an inner USER queue. The outer claim is restored on return.

GuestSession explicitly ends stream state and closes AudioScope **before** USER/
legacy teardown and process-context restoration. Native continuations are already
drained. Teardown suppresses notifications, revokes/cancels accepted PCM and drops
weak session state. A failed revoke retains device-owned PCM and never fabricates
DONE; later completion cannot dereference a destroyed Process. There are no guest
buffer/native descriptor pointers held past their supported lifetime.

## Public validation

`COMPOSITION_GL_TEST=1 sh tools/run_host_tests.sh` builds public CRT-free
`wave_stream.exe`. The existing host job publishes `stage-d-wave-streaming` with
PE32, native inspector log, core audio regression log and this contract; still
six core jobs. No private EXE, music or trace is published.

The inspector runs **7 normal-exit0 x86 sessions +1 deliberately parked abort**:
NULL/window/thread/relaunch, unmapped WAVEHDR, nested-owner restoration and
ambiguous submit/reset-failure recovery. It checks actual immutable PCM,64-header
budget,4-block pressure, status bits, real callbacks and stale cross-session
handles. A separate guest receiver thread owns its actual callback queue; the
writer queue stays empty, and the receiver is joined before process exit. Two normal completions are observed before any next MM API in ordinary
sessions. The nested test verifies an outer header stays INQUEUE while an inner
Process is active, then becomes DONE through normal service on resume. The abort
uses a Process-local destruction probe to verify revocation/flags before memory
is destroyed. All modes total35 accepted blocks,11 confirmed played releases,
24 confirmed cancellations; parked execution is not counted as normal exit.

The D4a guest's old non-query-open refusal check now requests the still-unsupported
CALLBACK_FUNCTION mode; the new fixture independently tests actual supported opens.
This adapts the historical client to expanded functionality without weakening
stream/header/ABI gates. Existing ownership/stereo/audout-transport and D1–D3/
C/E/F/G regressions remain required. Focused strict, ASan/UBSan and negative
premature-DONE/ignored-reset-error checks supplement them; sanitizer scope and
native-fiber warning are reported with results, not whole-runtime certification.

Actual audible output, Switch reset behavior/latency/repeated launches remain
hardware gates. Local devkitA64/libnx is absent; exact-SHA CI builds real NRO.
Capture/transport-model PCM is software evidence, not speaker acceptance.

## Next block

D4c: real scheduled guest CALLBACK_FUNCTION worker and event callbacks, then
position/pause/restart and selected streaming format/resampler continuity. Preserve
exclusive admission or implement real mixing/client ownership before accepting
simultaneous PlaySound/streams. Function callbacks must run with a real guest
CPU/TEB/stdcall ABI and cancellation/drain semantics, not on a device stack or
as a pretend user APC. Public x86 callback/exit/abort/relaunch tests precede claims.

Private host Pinball still follows its truthful no-device path and requires
WINMM!mciSendCommandA; MCI/MIDI/music output is separate later compatibility work.
No playable game, full Windows audio, console sound or FPS is accepted. Registry
persistence/dynarec/D3D9/richer shell remain open. SD recovery remains closed;
hardware/Paint/GPU/fallback/input/relaunch checks remain deferred.

[D4f mixed waveOut](D4F_WAVE_MIXER.md) now integrates the native core with real
WinMM sources/mapper and direct fallback. Earlier exclusivity/activation limits
describe this document’s original package scope. Historical wave fixtures now
explicitly select device0 to retain strict direct-buffer/count oracles; the new
public x86 fixture independently certifies multi-stream mapper behavior. MCI/
MediaPlayer/PlaySound remain exclusive until their shared-source integration.
