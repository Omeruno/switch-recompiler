"""Compare a native startup log and PPM to the original x86 demo trace."""
from pathlib import Path
import argparse
import json
import re


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('reference', type=Path)
    parser.add_argument('log', type=Path)
    parser.add_argument('frame', type=Path, nargs='?')
    args = parser.parse_args()
    reference = json.loads(args.reference.read_text())
    log = args.log.read_text()
    native_reads = re.findall(r'Archive read #\d+ offset=(\d+) size=(\d+) sha256=([0-9a-f]{64})', log)
    original_reads = reference['archive_reads']['Data2.bnd']
    if [[int(offset), int(size), digest] for offset, size, digest in native_reads] != original_reads:
        parser.error('archive reads differ from the x86 trace')
    native_uploads = re.findall(r'(DXT[1-5]) mip=(\d+) size=(\d+) sha256=([0-9a-f]{64})', log)
    if [[fmt, int(mip), int(size), digest] for fmt, mip, size, digest in native_uploads] != reference['uploads']:
        parser.error('compressed texture uploads differ from the x86 trace')
    geometry = [event[3:5] for event in reference['draw_events'] if event[0] == 'geometry']
    native_geometry = re.findall(r'\bvb=([0-9a-f]{64}) ib=([0-9a-f]{64})', log)
    last_present = re.findall(r'^Present #(\d+),', log, re.M)
    if ([list(pair) for pair in native_geometry] != geometry or
            not last_present or int(last_present[-1]) != reference['presents'] or
            'status: first game-rendered frame' not in log):
        parser.error('draw geometry or frame boundary differs from the x86 trace')
    print(f'{len(native_reads)} archive reads, {len(native_uploads)} texture uploads and '
          f'{len(geometry)} draw calls matched the x86 hashes at Present #{last_present[-1]}.')
    if args.frame is not None:
        magic, dimensions, limit, pixels = args.frame.read_bytes().split(b'\n', 3)
        width, height = map(int, dimensions.split())
        if magic != b'P6' or limit != b'255' or (width, height) != (1024, 768) or len(pixels) != width * height * 3:
            parser.error('invalid 1024x768 RGB framebuffer')
        nonblack_pixels = sum(bool(r or g or b) for r, g, b in zip(pixels[::3], pixels[1::3], pixels[2::3]))
        if nonblack_pixels < width * height // 2:
            parser.error('expected the textured loading screen')
        print(f'{width}x{height} frame verified, {nonblack_pixels} nonblack pixels.')


if __name__ == '__main__':
    main()
