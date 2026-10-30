# D10 — audio reset recovery, indexed MP3 seek and file-backed MMIO

This package fixes a production audout reset ordering defect and two actual
sound-enabled startup prerequisites. It extends the common runtime without
checking an EXE name. It does not certify a game or physical Switch output.

## Reset and seek

Audout Reset previously queried Contains before retrieving released buffers.
A released tag can still belong to that queue until GetReleased retrieves it;
Contains can therefore truthfully return true after native stop/flush. The old
order refused reset and MediaPlayer reported `Audio reset failed; buffers retained`.
The host transport model incorrectly excluded its released queue from Contains.

Reset now stops/flushes, drains the released queue, and only then confirms absence
of every retained descriptor. Drain is bounded to four descriptors plus one empty
read; unknown tags, duplicates, inconsistent counts and IPC errors refuse reset.
All descriptor/PCM owners remain alive throughout, including on refusal. Owners
are released only after revocation is confirmed. A failed restart still leaves
Available false rather than claiming successful playback. This order is also used
by stop, replacement and the shared mixer; there is no player-specific fake reset.
The transport model now includes queued **and released** tags in Contains. The
previous production adapter fails this corrected model; the new adapter passes.
The model is software evidence, not a certification of every Horizon version.

MP3 seeks previously decoded forward from the start when moving backward, and
MediaPlayer explicitly rewound before every seek. AudioDecoder now creates a
stable Impl-owned dr_mp3 seek index at open: roughly half-second spacing, at most
14400 points / 345600 bytes with the existing two-hour/input bounds. The pinned
third-party header remains unmodified. Index failure refuses replacement while
preserving the previously loaded decoder. Normal valid input seeks use nearby
points and the decoder's required history; malformed stream recovery can fall
back within the bounded input, so no universal wall-time guarantee is made.
Open now pays the index scan. Seek revokes playback without the redundant rewind;
Stop still rewinds. Paused/EOF/replay policy and failed-reset ownership remain.

An outside-repository five-minute authored MP3 probe measured the same seek/read
positions on the host: before about6.8–130ms, after about0.29–0.53ms. Open changed
from metadata-only to an approximately2.9ms scan on that input. These are local
observations with no Switch latency/FPS guarantee. Public tests independently
compare sought PCM to linear decoding for VBR/CBR/raw fixtures, repeat real player
rail seeks forward/backward/EOF while playing/paused, and reject failed reset
without freeing queued audio. EOF is a genuine Finished state; replay is explicit.

## sndPlaySound and MMIO

A no-device host launch bypassed the application's sound startup branch. An
outside-repository sound-enabled host diagnostic reached missing sndPlaySoundA,
then missing mmioOpenA. These are separate from rendering and keyboard binding.

sndPlaySoundA/W now have actual stdcall8 entries. They share PlaySound's real
null-stop, checked asynchronous memory-WAV snapshot, replacement/NOSTOP, device
and ownership/error path. PlaySound retains stdcall12/resource playback. Legacy
resource identifiers, filename/alias lookup, sync/loop and broader sound routing
remain explicit unsupported requests; this is not success returned from a stub.
The existing authored x86 audio fixture now exercises both legacy A/W imports,
actual submitted PCM, cross-entry replacement/stop and unsupported flags.

The new read-only MMIO implementation provides mmioOpenA/W, Close, Read, Descend
and Ascend with actual stdcall12/8/12/16/12 signatures. Disk paths use the shared
VFS/CWD and CP1252/UTF16 conversion. Each open takes a checked regular-file byte
snapshot (maximum8MiB;16 handles/32MiB aggregate per Process). Short/grown/read/
close errors refuse admission. Handles are opaque monotonic IDs with Process-owned
storage, not guest pointers or borrowed FILE objects. Process destruction and
continuation abort free snapshots; old IDs cannot alias a new session.

Supported opens are MMIO_READ with optional MMIO_ALLOCBUF and null or zeroed
MMIOINFO. The optional structure is fully mapped before use; wErrorRet receives
real errors. Custom procedures, memory I/O, write/create/delete/share modes,
GetInfo/SetInfo/Advance/Seek/CreateChunk and full buffered MMIO semantics remain
unsupported. No guest I/O callback is invoked. Read has checked signed lengths,
mapped destinations, a real cursor/EOF and returns -1 on invalid access.

Descend supports next chunk, FINDCHUNK, FINDRIFF and FINDLIST. It validates actual
8-byte headers, declared size, container type, parent metadata and odd-byte pad
against the owning snapshot; writes MMCKINFO only after success. Parent chunks
must match the file's real header/type. Ascend validates the chunk and advances
to its actual padded end. Chunk traversal has at most one pass over the bounded
snapshot. Bad/truncated/overflowing chunks return errors, not fabricated PCM.

The public CRT-free mmio_read.exe runs four normal new/legacy x86 sessions plus
two owning-handle aborts. It creates authored WAV bytes with odd JUNK and nested
LIST/INAM chunks, checks the actual format/sample bytes/read offsets, ascending,
EOF,A/W opens,unsupported writes,bad pointers/lengths/parents,malformed RIFF size,
stale handles and fresh sessions. Removing the RIFF bound fails its oracle.
The host runner publishes these sources/PE/logs and exact changed modules in
stage-d-audio-recovery, alongside retained D/audio/F7/GPU evidence.

## Current acceptance and next step

Private sound-enabled host launch now passes these imports and renders the table,
ball and text through the real1280x720 ShellSession path. No private executable,
asset, screenshot or trace is published. This is limited host startup evidence;
black-screen recovery on the actual Switch remains unverified until a fresh
exact-version run. Legacy USER32 LoadIconA remains separate work.

The full host/MinGW/Mesa suite, strict compilation, focused sanitizers, previous
reset and missing-bound negative controls and exact published-SHA all-six CI/NRO/
Pages/public-source audits remain gates. Local devkitA64/libnx is absent; CI builds
the NRO. Hardware music playback was observed by the user; seeking, pause/stop,
relaunch, game audio/graphics/input and fallback still need their own physical
checks. Do not generalize playback into full WinMM/game sound acceptance.

After recovery verification continue shared MCI/MediaPlayer/PlaySound + mixed
waveOut sources, then DirectSound/MIDI and the dynarec/D3D9 queues. CPU GDI default,
Claude Luna, L legacy/ZL software and the closed SD recovery status are preserved.
See [D4d](D4D_AUDIO_MEDIA.md),[D4f](D4F_WAVE_MIXER.md),[F6c](F6C_MEDIA_VISUALS.md),
[D9](D9_CLASS_MENUS.md),[F7](F7_CONTROLLER_BINDINGS.md).
