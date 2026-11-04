#!/usr/bin/env python3
"""Reject an NRO that silently omitted the embedded browser font RomFS."""

import struct
import sys
from pathlib import Path


def main(path: Path) -> None:
    data = path.read_bytes()
    if len(data) < 0x80:
        raise ValueError("NRO is too small")
    magic, _, code_size, _ = struct.unpack_from("<4I", data, 0x10)
    if magic != 0x304F524E:
        raise ValueError("not an NRO")
    if code_size + 56 > len(data):
        raise ValueError("NRO has no asset header")
    asset = struct.unpack_from("<2I6Q", data, code_size)
    if asset[0] != 0x54455341 or asset[1] != 0:
        raise ValueError("invalid NRO asset header")
    romfs_offset, romfs_size = asset[6:8]
    start = code_size + romfs_offset
    if not romfs_offset or romfs_size < 759720 or start + romfs_size > len(data):
        raise ValueError("browser font RomFS is missing or truncated")
    print(f"browser RomFS: PASS ({romfs_size} bytes)")


if __name__ == "__main__":
    main(Path(sys.argv[1]))
