"""Read the original level script needed by Lua's loose-file interface."""
from io import BytesIO
from pathlib import PurePosixPath
import struct


_SCRIPTS = {'data/locations/mountain_2/logic/levelscript.lua': 0x7A6686C4}
_HEADER = struct.Struct('<3I')
_ENTRY = struct.Struct('<2I')
_SIZE = struct.Struct('<I')


def open_level_script(assets, name, mode):
    path = PurePosixPath(name.replace('\\', '/')).as_posix().lower()
    key = _SCRIPTS.get(path)
    if assets is None or key is None or mode not in ('r', 'rb'):
        return None
    with (assets / 'Data2.bnd').open('rb') as archive:
        total = archive.seek(0, 2)
        archive.seek(0)
        signature, _, count = _HEADER.unpack(archive.read(_HEADER.size))
        index_size = count * _ENTRY.size
        index_at = total - index_size
        if signature != 0x1B356852 or not 0 < count <= 65536 or index_at < _HEADER.size:
            raise ValueError('invalid Data2.bnd header')
        archive.seek(index_at)
        offsets = [offset for entry_key, offset in _ENTRY.iter_unpack(archive.read(index_size))
                   if entry_key == key]
        if len(offsets) != 1 or not _HEADER.size <= offsets[0] <= index_at - _SIZE.size:
            raise ValueError('invalid or missing level script archive record')
        archive.seek(offsets[0])
        size, = _SIZE.unpack(archive.read(_SIZE.size))
        if not 0 < size <= min(index_at - archive.tell(), 1024 * 1024):
            raise ValueError('invalid level script size')
        data = archive.read(size)
        if len(data) != size:
            raise ValueError('incomplete level script')
    if mode == 'r':
        data = data.replace(b'\r\n', b'\n')
    return BytesIO(data)
