# D4d — real playback controls, PCM formats, MP3/MCI and native Media Player

Extends [D4c](D4C_WAVE_CALLBACKS.md) in one user-requested audio package. This is
implemented audio/file playback, not completion of every Windows/game audio API.

## Native device and WinMM controls

AudioDevice has optional genuine pause/restart, played-frame position and master
volume operations; unsupported devices return failure. AudoutDevice calls real
libnx Stop/Start without flushing on pause, preserves owning descriptors/PCM,
reads GetAudioOutPlayedSampleCount and uses Set/GetAudioOutVolume. Raw counter
restart epochs are normalized from observed device counts, never elapsed host or
guest time. A reset still confirms actual revocation before any owner is freed,
restarts even an empty paused device and establishes a fresh client baseline.
Position/flush need Horizon4.0+; volume is advertised only with6.0+ support.

New real stdcall exports: waveOutPause/Restart/GetPosition/SetVolume/GetVolume.
MMTIME accepts sample,byte,millisecond positions; unsupported types fall back to
TIME_BYTES with wType updated. Queries validate12 mapped bytes, preserve output
on failure, subtract the stream's device baseline and clamp to admitted source
frames. Reset establishes position0 and resets conversion phase. Cancelled frames
never become played via a timer or a completion counter. DWORD position wrapping
is the32-bit API representation. Counter/read failures do not claim a position.

Volume is actual device master gain. WAVECAPS_VOLUME is advertised when available;
WAVECAPS_LRVOLUME is not. Unequal left/right requests explicitly return NOTSUPPORTED;
there is no fabricated independent channel balance. The setting persists on the
physical device across streams, like a master control. It is not a software mixer.

## PCM conversion

waveOut now accepts PCM1,8–96kHz,mono/stereo,8/16-bit, checked byte rate/alignment,
whole source frames and at most5seconds per input buffer. Common11/22/44/48/96k
formats are reported in caps. The device still receives canonical48k stereo16.
Source bytes are snapshotted, converted with continuous integer phase across
writes and committed only after native submission succeeds. Busy/failed admission
cannot advance the resampler. Mono is duplicated, unsigned8-bit is centered at128,
signed16-bit/stereo signs and channel identity are preserved.

The waveOut streaming policy uses sample-and-hold conversion so every accepted
header owns its output without borrowing a future WAVEHDR. A tiny downsampled
write producing no output frame refuses INVALPARAM without changing phase/flags.
File playback uses linear interpolation with a retained boundary frame and a
bounded final tail. These are basic continuous converters, not a high-quality
anti-aliasing filter, pitch/rate control, float/24-bit PCM or arbitrary compressed
waveOut support. The four-slot,64-header,one-client limits and all callback modes
remain. Loop flags remain unsupported.

## Native MP3/WAV engine

[dr_mp3 provenance/license](../third_party/dr_libs/README.md): unmodified pinned
header, commit a466dbe88fe2576d46c43877d602f3d66b8561a7, SHA256
390e4e0ef0cff80fb86ebf1b461d6947cfa4ed699a4fe107e5a5e69a47db2950, MIT-0 option.
It compiles natively on host/ARM64 with DR_MP3_NO_STDIO; no external executable,
FFmpeg runtime, network media, game payload or proprietary decoder is imported.

AudioDecoder opens a checked regular file through the same C/S/R VFS, snapshots
at most32MiB of compressed/input bytes and rejects short/grown/read/close failures.
It does not promise an atomic snapshot against a same-size concurrent writer.
A successful replacement is transactional; failed input preserves the old loaded
file. Maximum decoded duration is2hours. MP3 VBR/CBR,ID3 and mono/stereo use actual
decoding; PCM WAV8/16 mono/stereo uses checked RIFF chunks. Other containers/codecs
refuse. Raw MP3 without gapless metadata retains encoder padding; damaged-frame
recovery follows dr_mp3 policy, not a claim to reject every damaged MP3.

PCM is decoded in4096-source-frame blocks and converted incrementally, with at
most four output blocks queued. PCM memory does not scale with track duration.
The compressed snapshot and bounded initial duration scan do scale with the
input file; a player is not unlimited-size network/disk streaming. Seeking uses
actual source/decoder positioning, bounded by the file snapshot; linear conversion
phase restarts at the seek point. Seek is quantized to a source frame. EOF seek
produces no extra audio. Stop/reopen drops old buffers only after confirmed revoke.

MediaPlayer is the shared native/MCI engine: open,play,pause,resume,stop,seek,master
volume,actual position,duration and EOF. It owns an exclusive AudioOutput client;
it refuses another source instead of flushing someone else's audio. Submission,
control,position and reset failures retain ownership or require explicit Stop.
Observers contain weak metadata only. Nested/lost owners cannot write into a new
session or discard a claim that an outer AudioScope will restore; Stop refuses
while a newer owner can still restore that suspended outer claim. Cancellation
is an error/recovery boundary, not a silent jump over unplayed media.

## Shell player

Start → Media Player opens an authored native Luna window. Open accepts a VFS
MP3/PCM-WAV path (ordinary spaces allowed, no command-line quoting), or an unassociated MP3/WAV selected in Explorer uses the native
player through normal Open/controller activation. An explicit user association
still wins; context menu also offers native playback. Buttons and keyboard/pad
focus provide play/pause/resume/stop,±10-second seek and volume. Position/duration
come from the real engine. There is no decorative moving progress or fake VU meter.

The player uses the real shared native window move/resize/min/max/restore/close
and taskbar path, including responsive button bounds. Minimized playback is
serviced by the ordinary owner tick. Idle desktop and guest ShellSession both
service it; native state redraw does not require another guest audio call. A
Desktop AudioScope exists before the shell; launch stops native playback before
guest ownership changes. File/decoder state can remain visible for later replay.
Close releases decoder/file snapshot; failed reset leaves the window/error live.

This extends the existing single native tool group. It is not Microsoft's Media
Player binary, a video/visualization/playlist library, multiple independent native
windows or guaranteed uninterrupted playback while an external blocking keyboard
applet suspends the owner's service loop. Physical deadlines/underruns remain
acceptance work, not inferred from the host capture tests.

## MCI file-audio compatibility

Real exports: mciSendCommandA/W, mciGetDeviceIDA/W and mciGetErrorStringA/W.
One monotonic MCI device per Process supports file-backed mpegvideo/waveaudio
(MP3/PCM-WAV audio only), OPEN/CLOSE/PLAY/PAUSE/RESUME/STOP/SEEK/SET TIME_FORMAT/
STATUS/GETDEVCAPS. MCI_FORMAT_SAMPLES reports the file's original sample rate/count, not resampled48k device frames; sample seeks map to representable device frames, with exact EOF. Alias lookup, millisecond/source-sample units, error strings, actual
file/device admission and stale-ID rejection are implemented. Error-string
queries do not report every unknown error as recognized. Unsupported flags,
commands, MIDI/sequencer/CD/video/recording/multiple devices return real MCI errors.
mciSendString, arbitrary DirectShow/COM/codecs and MIDI synthesis are not implemented.

PLAY supports FROM, genuine cooperative WAIT, or asynchronous NOTIFY; TO ranges
and combining WAIT/NOTIFY refuse. WAIT parks its actual guest native continuation
while peers/service ticks advance; exit/host abort unwinds it. Open/seek/stop/
close notifications and actual EOF/abort/failure notifications use real existing
USER window queues. A pending play can be superseded/aborted; no completion is
posted at play admission. Destroyed targets discard notifications.1024 posted
messages bounds admission/delivery; exhaustion latches an error reported on the
next MCI command rather than growing notification storage without limit.

PollMciMedia runs before ordinary executor quanta, without executing guest code.
Session teardown suppresses notifications and stops media before audio/UI/memory
ownership restoration. MIDI remains explicit unsupported: removing an import
fatal is not accepting Pinball music or gameplay. No per-EXE behavior exists.

## Public validation and next scope

Authored [MP3 signals/provenance](../tests/data/audio/README.md) and manifest pin
three files plus the decoder. No third-party music is published. Native tests
check real8-second VBR stereo tone spectra/channel separation,22k CBR mono,raw48k
MP3 padding,PCM-WAV/malformed containers,seek/replay,continuous ramp/partition
oracles,queue bounds,device failure/busy/nested claims and actual shell controls/
minimized service/window lifecycle/Explorer activation. A production audout host
transport model checks actual stop/start calls,retained descriptors/native counts/
volume/error/revoke ordering; this is not a hardware model certification.

Public audio_media.exe runs7 normal x86 exit0 sessions +1 deliberately parked
MCI_WAIT abort. It tests22/44/48/96k mono/stereo PCM8/16,partial device-count position,
pause/restart/reset/master volume,MP3 MCI A/W/aliases/caps/status/WAIT/NOTIFY/seek/
close/stale IDs/MIDI refusal/no-device behavior. The abort is not a normal exit.
The existing D4a/D4b/D4c,C/E/F/G fixtures remain required. The existing host CI job
publishes stage-d-audio-media with public PE,logs,fixtures/manifest/provenance and
this contract; core CI remains six jobs. Decoder/PCM corruption and no-native-pause
negative controls must fail actual signal/transport gates. Focused sanitizers are
additional evidence, not whole-runtime or console acceptance.

Remaining audio work: real multi-source mixing/per-source volume and positions,
DirectSound (audit target-game versions first),MIDI/sequencer synthesis/assets,
PlaySound filename/sync/loop extensions,more codecs/stream sources and measured
underrun/latency/relaunch behavior. Full game audio and all of WinMM are not closed.
Local devkitA64/libnx is absent; exact-SHA CI builds the NRO. Physical sound,
Paint/GPU/input/fallback/relaunch checks stay deferred; SD recovery stays closed.

The authored [F6c media UI](F6C_MEDIA_VISUALS.md) now adds source-backed PCM
visualizations/music icons/double-click; earlier absence of visualization above
describes D4d package scope. This is not video decoding or album-art extraction.

[D4e](D4E_AUDIO_MIXER.md) now implements a bounded8-source native mixer core
with actual audout adapter evidence. This does not yet change D4d MediaPlayer/MCI
or WinMM exclusivity; their source-lifetime migration and x86 fixtures come next.

[D4f mixed waveOut](D4F_WAVE_MIXER.md) now integrates the native core with real
WinMM sources/mapper and direct fallback. Earlier exclusivity/activation limits
describe this document’s original package scope. Historical wave fixtures now
explicitly select device0 to retain strict direct-buffer/count oracles; the new
public x86 fixture independently certifies multi-stream mapper behavior. MCI/
MediaPlayer/PlaySound remain exclusive until their shared-source integration.
