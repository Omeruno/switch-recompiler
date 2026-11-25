"""Track menu audio calls and optionally export the original sample data to WAV."""
import array
from dataclasses import dataclass
from pathlib import Path
import struct
import sys
import wave

from fsb_bank import parse_bank


_STEPS = (
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31,
    34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130,
    143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449,
    494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411,
    1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660,
    4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
    11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623,
    27086, 29794, 32767,
)
_INDEX = (-1, -1, -1, -1, 2, 4, 6, 8)
_MAX_BANK_SIZE = 128 * 1024 * 1024
_FREE_CHANNEL = 0xFFFFFFFF


def decode_pcm(sample):
    if sample.mode & 0x400000:
        if sample.channels != 1 or len(sample.payload) % 36 or len(sample.payload) // 36 * 64 != sample.frames:
            raise ValueError('unsupported IMA ADPCM block layout')
        pcm = array.array('h')
        for block in range(0, len(sample.payload), 36):
            previous, index, _ = struct.unpack_from('<hBB', sample.payload, block)
            if index > 88:
                raise ValueError('invalid IMA ADPCM step index')
            pcm.append(previous)
            for i in range(63):
                nibble = sample.payload[block + 4 + i // 2] >> (4 * (i & 1)) & 15
                difference = ((2 * (nibble & 7) + 1) * _STEPS[index]) >> 3
                previous = max(-32768, min(32767, previous - difference if nibble & 8 else previous + difference))
                index = max(0, min(88, index + _INDEX[nibble & 7]))
                pcm.append(previous)
        if sys.byteorder != 'little':
            pcm.byteswap()
        return pcm.tobytes()
    if sample.mode & 0x10 and len(sample.payload) == sample.frames * sample.channels * 2:
        return sample.payload
    raise ValueError(f'unsupported FSB sample mode {sample.mode:#x}')


@dataclass
class Voice:
    source: object
    end: int
    looping: bool
    paused_at: int | None
    mode: int
    volume: int = 255
    pan: int = 128
    frequency: int = 44100
    position: tuple | None = None
    velocity: tuple | None = None
    distance: tuple | None = None


@dataclass
class Stream:
    samples: tuple
    mode: int
    selected: int = 0
    channel: int | None = None
    callback: tuple | None = None


class MenuAudioTrace:
    def __init__(self, boot, wave_directory=None):
        self.boot = boot
        self.audio = boot.audio
        self.wave_directory = Path(wave_directory) if wave_directory else None
        self.voices = {}
        self.streams = {}
        self.exported = set()
        self.distances = {}

    @property
    def frame(self):
        return self.boot.d3d.present_count

    def active(self, channel):
        voice = self.voices.get(channel)
        return voice is not None and (voice.looping or voice.paused_at is not None or self.frame < voice.end)

    def export(self, sample, key):
        if self.wave_directory is None or key in self.exported:
            return
        data = decode_pcm(sample)
        self.wave_directory.mkdir(parents=True, exist_ok=True)
        name = self.wave_directory / f'sample-{len(self.exported) + 1:02}.wav'
        with wave.open(str(name), 'wb') as output:
            output.setnchannels(sample.channels)
            output.setsampwidth(2)
            output.setframerate(sample.rate)
            output.writeframes(data)
        self.exported.add(key)

    def start(self, channel, sample, key, mode, paused):
        if channel == _FREE_CHANNEL:
            channel = next((i for i in range(self.audio.channels) if not self.active(i)), _FREE_CHANNEL)
        if channel >= self.audio.channels:
            return _FREE_CHANNEL
        self.export(sample, key)
        frames = (sample.frames * 60 + sample.rate - 1) // sample.rate
        self.voices[channel] = Voice(key, self.frame + frames, bool(mode & 2),
                                     self.frame if paused else None, mode, frequency=sample.rate)
        self.audio.events.append(('play original sample', channel, sample.name.decode('ascii', 'replace'),
                                  sample.frames, sample.rate, sample.channels))
        return channel

    def load_stream(self, arg):
        pathname = self.boot.c_string(arg(0)).decode('ascii', 'replace')
        mode, offset, length = arg(1), arg(2), arg(3)
        path = self.boot.asset_path(pathname)
        if mode & 0x8000 or path is None or not path.is_file() or length > _MAX_BANK_SIZE:
            return 0
        with path.open('rb') as source:
            source.seek(offset)
            data = source.read(length or _MAX_BANK_SIZE + 1)
        if len(data) > _MAX_BANK_SIZE:
            return 0
        samples = parse_bank(data)
        handle = self.boot.allocate(4)
        self.streams[handle] = Stream(samples, mode)
        self.audio.change_payload(sum(len(sample.payload) for sample in samples))
        self.audio.events.append(('stream opened', pathname, len(samples)))
        return handle

    def dispatch(self, name, arg, finish):
        if not name.startswith('_FSOUND_') or '@' not in name:
            return False
        method, pop = name.removeprefix('_FSOUND_').split('@')
        if method.startswith('Stream_'):
            result = self.stream_call(method.removeprefix('Stream_'), arg)
        else:
            result = self.voice_call(method, arg)
        if result is None:
            return False
        finish(result, int(pop))
        return True

    def stream_call(self, method, arg):
        if method == 'Open':
            return self.load_stream(arg)
        index = arg(1) if method in ('Play', 'PlayEx') else arg(0)
        stream = self.streams.get(index)
        if method in ('Play', 'PlayEx'):
            if stream is None:
                return _FREE_CHANNEL
            if method == 'PlayEx' and arg(2):
                raise ValueError('FMOD stream DSP units are unsupported')
            track = stream.selected
            channel = self.start(arg(0), stream.samples[track], (index, track), stream.mode,
                                 method == 'PlayEx' and bool(arg(3)))
            if channel != _FREE_CHANNEL:
                stream.channel = channel
            return channel
        if method == 'GetNumSubStreams':
            return len(stream.samples) if stream else 0
        if method == 'SetSubStream':
            if stream is None or arg(1) >= len(stream.samples):
                return 0
            stream.selected = arg(1)
        elif method == 'SetMode':
            if stream is None:
                return 0
            stream.mode = arg(1)
        elif method == 'SetEndCallback':
            if stream is None:
                return 0
            stream.callback = (arg(1), arg(2))
        elif method in ('Stop', 'Close'):
            if stream is None:
                return 0
            voice = self.voices.get(stream.channel)
            if voice and isinstance(voice.source, tuple) and voice.source[0] == index:
                del self.voices[stream.channel]
            stream.channel = None
            if method == 'Close':
                del self.streams[index]
                self.audio.change_payload(-sum(len(sample.payload) for sample in stream.samples))
                self.boot.heap.free(index)
        else:
            return None
        return 1

    def voice_call(self, method, arg):
        if method in ('PlaySound', 'PlaySoundEx'):
            key = arg(1)
            sample = self.audio.samples.get(key)
            if sample is None:
                return _FREE_CHANNEL
            if method == 'PlaySoundEx' and arg(2):
                raise ValueError('FMOD sound DSP units are unsupported')
            return self.start(arg(0), sample, key, self.audio.sample_modes[key],
                              method == 'PlaySoundEx' and bool(arg(3)))
        methods = ('IsPlaying', 'GetCurrentSample', 'StopSound', 'GetLoopMode', 'SetLoopMode',
                   'SetPaused', 'SetVolume', 'SetPan', 'SetFrequency', 'GetFrequency', 'GetVolume',
                   'Sample_SetMinMaxDistance', '3D_SetMinMaxDistance', '3D_SetAttributes')
        if method not in methods:
            return None
        channel = arg(0)
        voice = self.voices.get(channel)
        active = self.active(channel)
        if method == 'IsPlaying':
            return int(active)
        if method == 'GetCurrentSample':
            return voice.source if active and isinstance(voice.source, int) else 0
        if method == 'StopSound':
            self.voices.pop(channel, None)
            return int(active)
        if method in ('Sample_SetMinMaxDistance', '3D_SetMinMaxDistance'):
            closest, farthest = struct.unpack('<2f', struct.pack('<2I', arg(1), arg(2)))
            if not 0 <= closest <= farthest:
                return 0
            if method == 'Sample_SetMinMaxDistance':
                if channel not in self.audio.samples:
                    return 0
                self.distances[channel] = (closest, farthest)
            elif voice is None:
                return 0
            else:
                voice.distance = (closest, farthest)
            return 1
        if voice is None:
            return 0
        if method == 'GetLoopMode':
            return voice.mode & 7
        if method == 'GetFrequency':
            return voice.frequency if active else 0
        if method == 'GetVolume':
            return voice.volume if active else 0
        if method == 'SetLoopMode':
            voice.mode = voice.mode & ~7 | arg(1) & 7
            voice.looping = bool(voice.mode & 2)
        elif method == 'SetPaused':
            if not active:
                return 0
            if arg(1) and voice.paused_at is None:
                voice.paused_at = self.frame
            elif not arg(1) and voice.paused_at is not None:
                voice.end += self.frame - voice.paused_at
                voice.paused_at = None
        elif method == 'SetVolume':
            if arg(1) > 255:
                return 0
            voice.volume = arg(1)
        elif method == 'SetPan':
            pan = arg(1)
            if pan > 255 and pan != 0xFFFFFFFF:
                return 0
            voice.pan = pan
        elif method == 'SetFrequency':
            frequency = arg(1)
            if not active or not 0 < frequency <= 0x7FFFFFFF:
                return 0
            anchor = self.frame if voice.paused_at is None else voice.paused_at
            remaining = max(0, voice.end - anchor)
            voice.end = anchor + (remaining * voice.frequency + frequency - 1) // frequency
            voice.frequency = frequency
        elif method == '3D_SetAttributes':
            if arg(1):
                voice.position = struct.unpack('<3f', self.boot.cpu.mem_read(arg(1), 12))
            if arg(2):
                voice.velocity = struct.unpack('<3f', self.boot.cpu.mem_read(arg(2), 12))
        return 1
