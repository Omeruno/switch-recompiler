# D4f — real multi-stream WinMM mixer integration

[D4e native mixer](D4E_AUDIO_MIXER.md) is now called by actual waveOut HLE and the
regular GuestExecution owner service. WAVE_MAPPER chooses this route when a genuine
played-frame counter exists. Device1 is the software mixer; device0 is the direct
compatibility route. Both use one physical audout and cannot steal each other's
lease. No counter means one enumerated direct device and mapper fallback; no device
means zero devices. With counter support there are two **logical** outputs, not two
invented physical speakers. Mapper/mixed caps advertise actual per-source stereo
gain; direct caps retain native master-volume availability and no L/R balance.
MCI/MediaPlayer/PlaySound remain separate exclusive clients until the next package.
This does not yet mix those clients together with waveOut or add DirectSound/MIDI.

## Actual source and header lifetime

Each of up to8 open mixed HWAVEOUTs owns a separate mixer source, PCM converter,
source position, stereo gain, prepared-header namespace and retirement queue.
Handles occupy a distinct non-reused native WinMM range; stale, closed and inactive
owner handles cannot operate on another source. Function/event/thread/window
callbacks use the existing real shared dispatcher/queues/kernel events and actual
handle/instance/header values. No guest function runs on a mixer/device stack.

Each stream retains at most64 prepared headers and4 queued owning PCM snapshots.
Prepare/write validate WAVEHDR/data mappings, generation, original address, length,
flags and alignment. Admission copies and converts bytes before returning success;
subsequent guest mutation cannot change accepted audio. Success means acceptance
into a real bounded software playback queue, not proof of playback or durable I/O.
Each header may contain up to5 seconds in the existing PCM8/16 mono/stereo8–96k
subset. Canonical snapshots are <=960000 bytes/header; at most32 such snapshots
across8 streams (30,720,000 bytes), plus bounded raw/conversion temporaries and D4e's
<=1,536,000 source PCM bytes. No request-sized unbounded allocation is added.

Headers feed4096-frame owning chunks into the bounded core. A long header can
therefore span many native quantum buffers without pretending it ended at its
first chunk. Mixer/core quantum limits remain256 frames/four native descriptors.
Original WAVEHDR becomes DONE only after every admitted chunk really played or
a reset/session teardown confirmed revocation. Core chunk retirement does not
retire a still-unfed5-second header. Four-record per-stream retirement queues and
weak observers retain no borrowed guest buffer pointers. Draining checks active
Process/owner and current mapped header/generation before writing flags/signalling.

Positions use actual mixer played spans, converted back to each stream's original
sample rate/frames/bytes/ms, never global mixed-frame count. Per-handle stereo gain
is independent. Pause/restart/reset/close only change the selected source; the
core's real stop/revoke/rebuild preserves peer unplayed bytes and positions.
Normal reset cancels that stream's remaining headers, clears phase/position/pause
and retains gain. Close rejects queued headers, delivers ordered close, clears
prepared flags and releases the last bus client. A full callback queue refuses
notification/close rather than inventing successful delivery. A dead function
callback service still refuses future function-mode writes as before.

The core End path no longer repeats Reset after successful Quiesce/no borrowed
blocks: successful Quiesce already proves revocation, or there was no descriptor
to revoke. Fault recovery still requires real Reset. This prevents redundant native
IPC after a validated source close; there is no weakened revoke-on-error rule.

## Faults and scope boundaries

A submit/poll/control/reset fault latches streams and retains native/source data.
A failed reset does not set DONE or send cancellation callbacks. Retrying reset
can end/revoke the faulted bus: after confirmation **all affected physical-bus
streams** cancel their remaining headers and are recreated. The requested stream
acknowledges reset; peers retain an error until explicitly reset. This is genuine
global device failure recovery, distinct from ordinary independent source reset.
Ambiguous append remains owned until successful revoke. No false successful close
on retained device buffers. Invalid/unmapped header retirement faults its stream;
source queues are native owning snapshots, not borrowed guest allocations.

Nested sessions cannot drain/write a suspended Process's headers. Restoring an
outer session whose native descriptors were reset produces a real fault/recovery
boundary. EndWaveOutSession first ends callback service, disables notifications,
revokes the mixed bus and drains confirmed cancellation before guest memory ends.
On failed revoke the device still owns immutable native blocks independently of
Process lifetime; no later callback can borrow destroyed guest memory.

Mix controls globally stop/reset/restart physical audout for reconstruction;
physical gap/underrun/latency behavior is not accepted. Synchronous guest keyboard
applet/service pauses and real device counter assumptions remain hardware gates.
No native worker thread, sample-clock guess, forced arbitrary guest termination,
looped WAVEHDR, recording, other codecs or full WinMM compatibility is added.

## Public reproducer and preserved reference

`tests/guest/wave_mixer.c` is CRT-free authored x86 PE32. Its native inspector runs
9 normal exit0 sessions plus2 intentionally parked aborts (main and real function
callback). It checks mapper+device1 simultaneous48k stereo16/22.05k mono8, actual
mixed PCM and immutable guest writes, partial100/137-frame positions, peer gain,
pause/reset/close, source EOF and real original-rate counts. The8-source case has
one5-second header, independently checked output/position and no first-chunk DONE;
source9 refuses without corrupting output. Function/event/thread/window dispatch,
blocked function callback with peer progress, reset/ambiguous append retained
buffers, nested owner isolation, stale handles, teardown, missing counter and
no-device outcomes are explicit gates. No private music/game payload is used.

Previous D4b/D4c/D4d wave fixtures explicitly select **device0** to preserve their
strict direct descriptor/release/count/IPC oracles, including35/11/24 and27/9/18.
They are the direct reference, not evidence that the new mapper stays exclusive.
The new actual-x86 mixer fixture certifies changed mapper behavior independently.
D4a now expects two actual logical routes on its counter-capable capture model;
no-device/recovery/count/caps and all original byte/error assertions remain.
Original MCI/native player tests remain exclusive, with no guessed simultaneous
music support. Existing C/E/F/G and native mixer/production audout tests stay gates.

CI's existing host job publishes stage-d-wave-mixer (public PE/C source/native
inspector log/contract). Exact-SHA all6 CI, actual Switch build/NRO, prior artifact/
clean source-manifest audits and Pages/public bytes remain publication gates.
Local devkitA64/libnx is absent. Host software and transport evidence does not
replace physical sound/Paint/GPU/fallback/input/relaunch acceptance; SD closed.

## Pinball observation and next work

At incoming534ad72, fresh diagnostic host runs of the private x86 payload stop at
GDI32!SetSystemPaletteUse with new UI and USER32!LoadStringA with legacy UI. D5's
OpenFile implementation is not proof of later main-frame/audio/game acceptance.
Private traces/binaries remain outside git. Next compatibility work needs public
palette/device-capability and ANSI string-resource reproducers, then real common
implementations and a fresh application trace. Do not add success stubs or claim
Pinball playable. MIDI music remains unsupported independently of waveOut mixing.

Next audio package: migrate MCI/MediaPlayer/PlaySound into a shared owner bus/source
lifetime so music and waveOut can coexist. Preserve decoder/preview/seek/EOF,
callback/error semantics and direct fallback; audit DirectSound target versions
and MIDI/synthesis after actual API prerequisites. F6c/Luna/default CPU/L legacy/
ZL software remain intact.
