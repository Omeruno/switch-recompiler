# D4a — owning PCM output, checked audout and honest device queries

Implemented after D3, 2026-10-28. This common audio foundation replaces the old
void PlaySound callback and four-slot code that freed an in-flight buffer when
all slots were busy. No fabricated device counts or successful silent writes.

## Ownership and device contract

`AudioOutput` admits at most4 immutable canonical PCM blocks, each at most5s of
48000Hz stereo signed16. Each block owns4096-aligned, zero-padded storage. Inputs
are copied before acceptance. Monotonic ticket IDs prevent a stale completion
from releasing a newer block. Backpressure refuses admission; no active slot is
stolen. Device readiness, submission acceptance, playback completion and reset
cancellation are separate states/counters.

The device receives shared ownership and must retain it until confirmed release
or revoke, including ambiguous submit failure. Poll/unknown/duplicate completion
and failed reset are sticky faults; new submission is blocked. Failed reset
retains tickets/device owners, prevents detach and never reports completion.
Successful reset cancels tickets only after revocation. Even queue destruction
cannot free memory a failed-reset device still owns. A bound device must outlive
its binding; callers must check detach. No retained guest pointers or callbacks.

GuestSession owns an AudioScope. Enter/leave resets the one physical device and
changes the active owner; stale/suspended-owner submission is refused and owner
identity is restored after nesting. Switching nested sessions cancels previous
pending audio, rather than promising simultaneous independent processes. The
queue is not a mixer, and no multi-process audio isolation/mixing is claimed.

## Production Switch adapter

`AudoutDevice` initializes and checks start plus actual48000/2/Int16 parameters.
Each fixed native AudioOutBuffer owns the same PCM block until libnx reports its
release. Append errors propagate and conservatively retain the binding. Reset
checks stop/flush, verifies every native buffer is no longer contained, drains
old release descriptors, then drops owners and checks restart. A failed restart
can acknowledge revocation while reporting unavailable; it is not playback.
Failed revoke keeps all storage and disables admission. Shutdown closes the
IAudioOut session before dropping native storage; main detaches only afterwards.

Audio is polled in normal guest/frontend/native input service, not just on the
next sound. Lifetime ends after native continuation drain and before guest memory
destruction. Legacy and new USER paths share the same service. Old callback save/
restore and the unsafe pool are removed. Init failure gives a real unavailable
result and does not fabricate a driver. There is no blocking unbounded drain.

## WinMM subset

Six real exports:

- `PlaySoundA/W`: checked asynchronous `SND_RESOURCE` (numeric or bounded named
  WAVE resource from an actual module) and `SND_MEMORY` RIFF input. New playback
  replaces current playback only after a confirmed reset; `SND_NOSTOP` refuses
  while pending. NULL sound stops via checked reset. TRUE means accepted output,
  not observed speaker completion. Invalid/missing/unsupported/no-device/busy/
  device failure returns FALSE with an error, never unconditional success.
- `waveOutGetNumDevs`:1 only for an available bound device and active guest owner,
  otherwise0. An ordinary host winrun has no output device, so0 is truthful.
- `waveOutGetDevCapsA/W`: actual device name and canonical48000 stereo16 format;
  bounded A/W structure copy with checked guest ranges and real MM errors.
- `waveOutOpen`: only `WAVE_FORMAT_QUERY` is implemented, validating actual device
  and canonical PCM format without writing a handle. Streaming opens explicitly
  return MMSYSERR_NOTSUPPORTED; no dummy HWAVEOUT is created.

Synchronous playback, filenames/default aliases, looping, independent streams,
prepared guest buffers, waveOutWrite/reset/close handles, callbacks, position,
volume, mixing, DirectSound/XAudio and MCI/MIDI remain open. The four-slot PCM
queue is native infrastructure, not a declaration of full waveOut streaming.

RIFF parsing checks form/chunk/padding bounds, formatPCM1, mono/stereo,8/16bit,
8000–96000Hz, alignment/byte rate/data boundaries, duplicates, duration and1MiB
input. Conversion preserves stereo channels, duplicates mono, handles unsigned8
and signed16 and uses bounded nearest-neighbor resampling. This is not arbitrary
compressed WAV decoding or high-quality interpolation. Resource names are bounded
to255 units; A names are ASCII. Only actual mapped guest bytes are copied.

## Public evidence and limits

`COMPOSITION_GL_TEST=1 sh tools/run_host_tests.sh` runs native tests and public
CRT-free PE32 `audio_output.exe` with an authored WAVE resource; no private game.
Existing host CI job publishes `stage-d-audio-ownership`, preserving six core jobs.

- `test_audio_output`: actual aligned immutable PCM, four-slot saturation,
  out-of-order/stale completions, refused detach, ambiguous submit/poll/reset
  faults, driver-held storage after queue destruction, nested owner restoration,
  bounded stereo/mono/8/16bit conversion, malformed/truncated/overflow RIFF.
- `test_audio_audout`: compiles the **production adapter** with a deliberately
  minimal host transport model. Covers init/start/append/poll/stop/flush/contains/
  restart errors, four native descriptor slots, no fifth append, reset drain and
  close-before-free. The shim is not libnx or hardware emulation/acceptance.
- `test_audio_api` + public PE: four normal-exit0 sessions for no-device, PCM
  capture, ambiguous submit error and restart. Actual resource/memory snapshots,
  stereo values, replacement/NOSTOP/release/stop, caps/format-query/MM errors,
  unsupported streaming-open handle unchanged. Host inspection verifies all10
  accepted blocks,2 explicit completions and8 cancellations separately.
- Strict O2/no-exceptions/no-RTTI core/bridge/adapter-model/inspectors and focused
  ASan/UBSan complement full C/E/F/G regressions. Native core/adapter model run
  with leak detection; API inspector/core/bridge/GuestSession are instrumented,
  other runtime objects ordinary, leak scan disabled for fiber/driver integration
  and known makecontext/swapcontext warning retained.
- Negative controls outside git destroy stereo channels or free device owners
  before native close. Public PCM assertions and adapter ownership assertion
  rejected those regressions. Captured PCM is not audible-output acceptance.

Local devkitA64/libnx is absent. Exact-SHA CI compiles the adapter/main with the
real SDK. Actual audible output, reset behavior, latency and repeated console
launches remain deferred hardware gates. No Switch sound/game/FPS acceptance.

## Next block

D4b: genuine bounded waveOut open/prepare/write/unprepare/reset/close with actual
buffer completion and supported callback delivery, then a public x86 streaming
fixture and session/abort/relaunch gates. Preserve the owning device contract;
never signal WHDR_DONE from queue admission or recycle memory before release.

Private host Pinball passes waveOutGetNumDevs using a truthful no-device profile
and next stops at missing WINMM!mciSendCommandA. This adds an MCI/MIDI dependency;
it does not supersede the planned waveOut lifecycle work or certify the game.
MCI/music decoding/output is a later real implementation, not a signature-only
success. D1 registry persistence, dynarec/D3D9 and broader shell remain separate.
Hardware/Paint/GPU/fallback/input/relaunch deferred, SD recovery remains closed.
