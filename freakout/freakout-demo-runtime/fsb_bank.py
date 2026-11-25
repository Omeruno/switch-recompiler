"""Read the FSB3 sample banks shipped in the FreakOut Windows demo."""
from dataclasses import dataclass
import struct


@dataclass(frozen=True)
class Sample:
    name: bytes
    frames: int
    mode: int
    rate: int
    channels: int
    payload: memoryview


def parse_bank(data):
    if len(data) < 24:
        raise ValueError("truncated FSB header")
    magic, count, header_size, data_size, version, flags = struct.unpack_from("<4s5I", data)
    if magic != b"FSB3" or version != 0x30001 or flags & ~2 or not count:
        raise ValueError("unsupported FSB format")
    payload_start = 24 + header_size
    if count > header_size // 8 or payload_start + data_size != len(data):
        raise ValueError("invalid FSB bank length")

    source = memoryview(data)
    samples = []
    cursor = 24
    offset = payload_start
    for index in range(count):
        if index and flags & 2:
            if cursor + 8 > payload_start:
                raise ValueError("truncated basic FSB sample header")
            frames, size = struct.unpack_from("<2I", data, cursor)
            name = b""  # Basic sample headers have no stored name.
            cursor += 8
        else:
            if cursor + 64 > payload_start:
                raise ValueError("truncated FSB sample header")
            size_header, name, frames, size, _, _, mode, rate = struct.unpack_from("<H30s6I", data, cursor)
            channels, = struct.unpack_from("<H", data, cursor + 62)
            if size_header < 64 or cursor + size_header > payload_start:
                raise ValueError("invalid FSB sample header length")
            name = name.split(b"\0", 1)[0]
            cursor += size_header
        if not (frames and size and rate and 1 <= channels <= 2 and offset + size <= len(data)):
            raise ValueError("invalid FSB sample data")
        samples.append(Sample(name, frames, mode, rate, channels, source[offset:offset + size]))
        offset += size
    if cursor != payload_start or offset != len(data):
        raise ValueError("unused bytes after FSB records")
    return tuple(samples)
