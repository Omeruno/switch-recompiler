"""Full-level surface copies used by the demo's D3DX9 texture loader."""
from io import BytesIO

from dx9_formats import BLOCK_BYTES, PIXEL_BYTES


_DDS_FORMATS = {
    int.from_bytes(b'DXT1', 'little'): (1, 'DXT1'),
    int.from_bytes(b'DXT3', 'little'): (2, 'DXT3'),
    int.from_bytes(b'DXT5', 'little'): (3, 'DXT5'),
}
_RAW_FORMATS = {20: 'BGR', 21: 'BGRA', 22: 'BGRX'}


def storage_size(fmt, width, height):
    if fmt in BLOCK_BYTES:
        return ((width + 3) // 4) * BLOCK_BYTES[fmt] * ((height + 3) // 4)
    if fmt in PIXEL_BYTES:
        return ((width * PIXEL_BYTES[fmt] + 3) & ~3) * height
    return 0


def convert_level(data, width, height, source, dest, colorkey):
    if source == dest and not colorkey:
        return data
    from PIL import Image
    if source in _RAW_FORMATS:
        mode = 'RGBA' if source == 21 else 'RGB'
        pitch = (width * PIXEL_BYTES[source] + 3) & ~3
        image = Image.frombytes(mode, (width, height), data, 'raw', _RAW_FORMATS[source], pitch)
    else:
        image = Image.frombytes('RGBA', (width, height), data, 'bcn', _DDS_FORMATS[source])
    image = image.convert('RGBA')
    if colorkey:
        import numpy as np
        rgba = np.frombuffer(image.tobytes(), '<u4').copy()
        rgb = (colorkey & 0xFF00FF00) | (colorkey & 0xFF) << 16 | (colorkey >> 16 & 0xFF)
        rgba[rgba == rgb] = 0
        image = Image.frombytes('RGBA', (width, height), rgba.tobytes())
    if dest in _DDS_FORMATS:
        dds = BytesIO()
        image.save(dds, format='DDS', pixel_format=_DDS_FORMATS[dest][1])
        return dds.getvalue()[128:]
    if dest == 21:
        return image.tobytes('raw', 'BGRA')
    raw = image.convert('RGB').tobytes('raw', _RAW_FORMATS[dest])
    line = width * PIXEL_BYTES[dest]
    padding = bytes(-line % 4)
    return b''.join(raw[i:i + line] + padding for i in range(0, len(raw), line))


def load_surface(probe, arg, finish):
    dest, dest_palette, dest_rect, source, source_palette, source_rect, filtering, colorkey = (
        arg(i) for i in range(8))
    if source not in probe.surfaces or dest not in probe.surfaces:
        finish(0x8876086C, 32)
        return
    src_fmt, *_, width, height = probe.surfaces[source][0]
    dst_fmt, *_, dst_w, dst_h = probe.surfaces[dest][0]
    src_key = probe.surface_levels.get(source, (source, 0))
    dst_key = probe.surface_levels.get(dest, (dest, 0))
    src_data = probe.texture_storage.get(src_key)
    expected_src = storage_size(src_fmt, width, height)
    expected_dst = storage_size(dst_fmt, dst_w, dst_h)
    if (any((dest_palette, source_palette, dest_rect, source_rect)) or
            (width, height) != (dst_w, dst_h) or src_data is None or
            not expected_dst or src_data[1] != expected_src or
            (filtering not in (1, 2, 3, 4, 5, 0xFFFFFFFF)) or
            ((src_fmt != dst_fmt or colorkey) and
             (src_fmt not in (_RAW_FORMATS | _DDS_FORMATS) or dst_fmt not in (_RAW_FORMATS | _DDS_FORMATS)))):
        probe.stop(f'unsupported D3DX surface conversion {src_fmt:#x} -> {dst_fmt:#x}')
        return
    cpu = probe.boot.cpu
    pixels = convert_level(bytes(cpu.mem_read(src_data[0], expected_src)), width, height, src_fmt, dst_fmt, colorkey)
    if len(pixels) != expected_dst:
        raise ValueError('D3DX surface conversion returned an invalid mip size')
    if dst_key not in probe.texture_storage:
        probe.texture_storage[dst_key] = (probe.boot.allocate(expected_dst), expected_dst, dst_fmt)
    cpu.mem_write(probe.texture_storage[dst_key][0], pixels)
    probe.texture_revisions[dst_key[0]] = probe.texture_revisions.get(dst_key[0], 0) + 1
    probe.events.append(('d3dx surface copy', src_fmt, dst_fmt, width, height, colorkey))
    finish(0, 32)
