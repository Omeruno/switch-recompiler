# F6c — XP-era Media Player, PCM visualization, music icons and double-click

Extends [D4d](D4D_AUDIO_MEDIA.md) and the real native [window actions](F6B_NATIVE_WINDOW_ACTIONS.md).
The interface is authored in the style of XP-era Media Player, with no imported
Microsoft artwork/player binary or unsupported library/radio/sync buttons.

## Interface and controls

The player has a silver/blue transport area, dark visualization surface, actual
file/codec/source-rate/time/status, responsive compact/wide layout and an optional
wide Now Playing sidebar. Its existing open/play/pause/resume/stop/±10sec/master
volume controls still call the shared engine. Clickable seek rail performs a real
seek including while paused. Four display choices are spectrum, stereo waveform, PCM-driven radial circles
and a static file icon with visual effects off. Caption/window/task controls stay
real native move/resize/min/max/restore/close; existing one native tool group and
one physical audio-source limits remain. This is audio visualization, not a video
file decoder, playlist/library or an actual album-art extraction feature.

New MusicFile vector art is an authored shaded page, blue musical notes and green
play badge. It is used for MP3/WAV entries, Start's Media Player, player heading
and native task button.16/32/48/64px use the existing supersampled icon cache;
selection/alpha/clipping remain normal shell behavior. Other codecs are not
advertised by assigning the icon. Existing explicit EXE associations still win.

## Actual signal, position and lifetime

MediaPlayer retains at most four bounded owning PCM preview blocks, separate
from device descriptors. A weak observer marks retirement, and completed blocks
are removed before insertion even if AudioOutput's submission poll completes
another block. Stop/seek/reopen clear previews only through the existing owner/
reset policy. They contain no guest/window pointers. Total preview PCM is bounded
by four4096-source-frame converted blocks (less than0.4MiB at the8k minimum).

The display requests256 stereo frames at actual device playback position. It
looks up the matching queued block(s), instead of animating the newest decoded
future block. Paused position gives the same samples; stopped/error/inactive-owner/
retired data returns no live signal. Missing portions are zero. The signal is
pre-master-gain PCM, not a measurement of the console speakers or output latency.

The analyzer uses a256-point Hann-windowed radix2 FFT and separate L/R energy,
32 displayed bands and actual RMS meters. Anti-phase stereo does not disappear
through mono cancellation. Waveform traces draw the actual two sample channels; circles use actual stereo
sample magnitudes as radius, with no random or time-driven phase.
There is no random/time-based generated spectrum, fake moving progress or fake
VU. Frame selection uses the existing played-frame counter, not elapsed guest
clock. The usual owner service drives redraws and audio; hardware timing/underruns
remain unverified, including blocking keyboard-applet behavior from D4d.

## Double-click

Explorer rows now select on the first primary click and open/navigate on the
second within500ms and6 logical pixels. Identity includes the actual parent/path,
so another item, list reordering, changed selection or a new directory cannot
turn a queued click into opening a child. Catalog program rows use the same
selection/double-activation policy and existing validated launch path.

A held press over500ms or a moved press over6px cannot complete a double-click.
Navigation, controls, secondary input, keyboard selection/activation, session
entry/window cancellation break the sequence. The default clock is monotonic
native receipt time, independent of guest fast-forward. Tests inject a clock;
no real blocking sleep is needed. Open/Enter/A/controller and context commands
remain available. Triple clicks cannot launch two items through one double-click.
This does not add native file dragging or change guest USER double-click messages.

## Source sample-unit correction carried with this package

The interrupted D4d audit corrected MCI_FORMAT_SAMPLES to original file sample
rate/count, rather than resampled48k device frames. Original source metadata is
exposed to MCI length/position/seek; exact EOF is preserved. A44.1k8second MP3
therefore reports352800 source frames,2205 at50ms and a176400-sample seek is4000ms.
The new guest assertions run through actual MCI A/W handling. The initial getter
name collided with the existing SourceFrames block-size constant; the final name
is SourceFrameCount, with fresh objects/fixtures. Failed/stale development builds
are not validation. Native file position/preview indexing stays canonical48k.

## Evidence and boundaries

- FFT oracle: silence, independent3kHz peak/band, stereo anti-phase energy, RMS,
  deterministic paused samples.
- Real PCM capture comparison: initial and advanced playback cursors select actual
  device block samples, pause freezes, effect changes preserve audio queue and
  seek rail moves the actual decoder/position.
- Double-click filesystem test: real folders and MP3; single selection, valid
  second click, slow/far/different-item/held/drag/session rejection and playback.
- Existing icon oracle covers the new distinct vector at small/native sizes.
  Native renderer captures use actual Liberation fonts, expanded spectrum and
  compact stereo wave. They are host captures, not Switch screenshots.

The full host/MinGW/Mesa suite and original D4a–d/C/E/F/G fixtures stay required.
The existing host CI job publishes stage-native-media-visuals logs/captures/this
contract. Exact published-SHA all-six CI, Switch NRO, prior audio/MP3/GUI/GPU/source
identity audits remain publication gates. Local devkitA64/libnx is absent; actual
console sound, input/graphics/fallback/relaunch stay deferred. SD recovery closed.
Mixing/DirectSound/MIDI and the observed legacy Pinball OpenFile blocker remain
separate common system work; visual polish does not accept them or complete XP.

[D10 recovery](D10_AUDIO_RECOVERY.md) corrects audout reset ordering after released
queue retrieval,adds an owning MP3 seek index and removes the redundant seek rewind.
Its actual PCM/rail/transport tests extend this package;physical seek recovery
remains a fresh-build gate. D10 also adds real bounded sndPlaySound/MMIO startup.
