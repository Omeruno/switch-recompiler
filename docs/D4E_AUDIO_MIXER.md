# D4e — bounded native PCM mixer and real audout path

This is an implemented native source core over the existing production AudioOutput,
with owning PCM and actual audout-adapter tests. It does **not yet migrate WinMM,
MCI or shell MediaPlayer**: their existing exclusive-source behavior remains until
the following source-lifetime/API integration package. No simultaneous game sound,
DirectSound, MIDI or physical sound acceptance is claimed by this native milestone.

## Source and memory contract

AudioMixer takes an AudioOutput reference and explicitly acquires one owner/client
lease. It refuses to steal a legacy client's queue. Begin requires an actual native
played-frame counter; unsupported devices refuse without leaving a client claim.
The output/device and owning AudioScope must outlive the mixer. Nested scopes must
return before End/destruction of an outer mixer; inactive-owner writes/End refuse.
A restored source whose old mixed descriptors were cancelled faults and needs End.
No guest execution, guest pointers, decoded file/VFS ownership or background thread
is added by this core. Native callers service Pump explicitly.

There are at most8 sources,4 packets per source and48000 total retained stereo
frames per source. Each accepted Write owns a PCM copy; retained storage, including
a partially played packet's prefix, counts against that bound. Total source PCM is
<=1,536,000 bytes plus bounded packet/map metadata. Input is canonical48k stereo
signed16; existing PcmStream/file decoders remain the earlier format-conversion
layers. New IDs are not reused within a mixer object, including across End/Begin.
IDs are scoped to that instance, not a new global guest HANDLE namespace.

Pump emits at most4 native blocks of at most256 frames each: at most1024 frames
queued at once (21.33ms of PCM, **not measured console latency**). It scans bounded
source packets, sums independent L/R samples with per-source uint16 channel gains
using signed32 accumulation, then clamps once to signed16. No per-source wrapping,
automatic averaging or fake attenuated success. Shorter sources contribute silence
only after their actual packet end; other source data keeps playing. Source gaps
start at the next unsubmitted mixed block; no fabricated source timeline advancement.
Aligned immutable native blocks remain owned by AudioOutput/AudoutDevice through
release or confirmed revocation. The mixer journal is also limited to4 records,
even if a driver releases blocks before its played counter catches up.

## Actual positions, pause and independent cancellation

Source position is the last genuinely observed played frame from Pump/control
synchronization, mapped through each mixed block's source spans. Submitting or
releasing a descriptor alone never advances it. Packet completion occurs only when
its last source sample is confirmed played; partial packets retain owned original
PCM. Observers report false=played and true=confirmed cancellation. A packet that
played a prefix and was then reset is cancelled, not reported fully played.
Observers are host-only; they may enqueue notifications, never execute guest code
or mutate the global output owner. Mutating reentry into this mixer is refused.

SetPaused, SetGain, ResetSource and CloseSource synchronously stop the actual
output, observe its frozen played counter, and require successful Reset/revocation.
All surviving source cursors rewind to their confirmed played frames; subsequent
Pump regenerates **only unplayed** mixed tails with the new source state. It never
edits a descriptor already borrowed by the native device. This lets one source
pause/reset/close while another retains its exact sample position and remaining
PCM. Controls are independent logically; native stop/reset/restart happens globally
for a bounded reconstruction. No physically gapless transition is claimed.
ResetSource cancels only that source's remaining packets and resets its position/
queue/pause, retaining its channel gains. End revokes the whole mixer and releases
the client. Callers Pump after source controls to refill the native queue.

Poll/position regression/overrun/pause/submit/reset errors latch a fault. On ambiguous
append or failed revoke, device blocks and source packets stay owned and no fake
played/cancelled notification is sent. End can retry genuine reset; after confirmed
revoke it cancels remaining packets and releases the lease. Destruction follows the
same best-effort End policy; callers requiring error recovery must explicitly keep
the mixer/output/device alive until End succeeds. Underlying device ownership of
immutable mixed bytes remains independent of the source object on failure.
The counter must represent consumed queued PCM frames, not a clock or synthesized
underrun estimate. Real console counter/timing/underrun behavior remains a hardware
gate. No multi-threaded public access or real-time scheduling guarantee is added.

## Reproducible public tests

`test_audio_mixer.cpp` exercises the actual AudioMixer/AudioOutput with an owning
PCM-capture device. Independent int64 sample expectations compare channels,
saturated sums, eight simultaneous sources, gain and actual retained input copies.
It checks a100-frame partial cursor, one-source pause/continuation,17-frame gain
change, peer-only replay/close/completion, different packet boundaries, all-paused
silence, reset after31 frames, EOF, packet/source/retained-memory limits and stale
IDs. A descriptor release with a stalled counter cannot complete packets or grow
the journal. Fault injection covers ambiguous submit, poll/control/reset failure,
retained ownership/retry, overrun, nested owner restoration, competing clients and
reentry from retirement observers. No wall-clock sleep or random spectrum is used.

`test_audio_audout.cpp` additionally runs the same core through **production
AudoutDevice**, compiled with __SWITCH__ against an explicit libnx transport model.
It inspects actual aligned AudioOutBuffer bytes for both stereo sums, advances the
native sample counter by37 frames, pauses one source, inspects regenerated peer-only
PCM, cancels the paused source and confirms the other source completes at512.
Existing production adapter fault/reset/close-before-free tests remain intact.
This is real adapter code with a host IPC model, not execution on Horizon/speakers.

The existing host job publishes stage-d-audio-mixer: native executable, core test/
adapter logs, public source/header and this contract. Still six CI jobs; all prior
WinMM/MCI/MP3/GUI/GPU artifacts and public x86 fixtures stay gates. Full host/MinGW/
Mesa, strict native compilation, standalone ASan/UBSan/leak scan, negative premature
credit/wrapping-sum tests, exact-SHA CI, Switch compilation and Pages/artifact/source
identity checks are required. Local Switch SDK is absent; CI compiles the native
core. Unused core functions may be removed from NRO until their production caller
is integrated; a successful build alone is not product activation.

## Next package

Migrate waveOut's single Holder and client checks to real mixer source handles:
per-stream headers/resampler/position/gain/pause/reset/close and delayed actual
retirement, with public simultaneous x86 streams and existing function/event/window
callback/abort/session gates. Then migrate shared MCI/MediaPlayer/PlaySound without
breaking decoder/previews or legacy device errors. Admission/caps must change only
when that path actually works. DirectSound target-version audit and MIDI/synthesis
remain later independent work. Preserve F6c visuals/double-click, default CPU GDI,
L legacy/ZL software and deferred Paint/GPU/input/sound/relaunch; SD remains closed.

[D4f mixed waveOut](D4F_WAVE_MIXER.md) now integrates the native core with real
WinMM sources/mapper and direct fallback. Earlier exclusivity/activation limits
describe this document’s original package scope. Historical wave fixtures now
explicitly select device0 to retain strict direct-buffer/count oracles; the new
public x86 fixture independently certifies multi-stream mapper behavior. MCI/
MediaPlayer/PlaySound remain exclusive until their shared-source integration.
