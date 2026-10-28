# Authored audio fixtures (CC0)

All audio was generated specifically for this repository from mathematical sine
waves. No music, game recordings or third-party audio samples are included.
The authors dedicate these generated fixture signals to the public domain (CC0).

Generated with FFmpeg/libmp3lame; FFmpeg is a fixture-generation tool, not a runtime
or CI dependency. The committed `manifest.json` pins fixture and decoder bytes.

- stereo-vbr.mp3:8seconds,44.1kHz stereo,220Hz left/660Hz right, amplitude0.2;
  libmp3lame -q:a4, ID3 title, Xing/gapless metadata.
- mono-cbr.mp3:1second,22.05kHz mono,440Hz sine; libmp3lame -b:a48k, ID3 title.
- stereo-raw.mp3:1second,48kHz stereo,330Hz left/990Hz right, amplitude0.15;
  libmp3lame -b:a128k -write_xing0 -id3v2_version0 (raw frames/encoder padding).

Example source: `aevalsrc=0.2*sin(2*PI*220*t)|0.2*sin(2*PI*660*t):s=44100:d=8`.
Acceptance verifies actual PCM duration/stereo/tone spectra and bounded streaming,
not file extension or a fabricated successful decoder return. Raw-frame MP3 has
no gapless metadata; padding is expected and is not claimed to be trimmed.
