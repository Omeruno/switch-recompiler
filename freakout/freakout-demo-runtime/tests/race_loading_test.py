import struct
from pathlib import Path
from tempfile import TemporaryDirectory
from types import SimpleNamespace
import unittest

from crt_format import scan_cdecl
from demo_archive import open_level_script
from surface_blit import convert_level, storage_size
from trace_boot import Boot


class RaceLoadingTest(unittest.TestCase):
    def test_asset_lookup_resolves_original_windows_case(self):
        with TemporaryDirectory() as directory:
            assets = Path(directory).resolve()
            path = assets / 'Sound' / 'Logic' / 'LOGIC.fsb'
            path.parent.mkdir(parents=True)
            path.touch()
            boot = SimpleNamespace(assets=assets)
            self.assertEqual(Boot.asset_path(boot, r'sound\LOGIC\logic.fsb'), path)
            self.assertIsNone(Boot.asset_path(boot, r'..\other\LOGIC.fsb'))

    def test_sscanf_writes_six_floats_and_stops_on_mismatch(self):
        values = (-44000.0, -3000.0, 20000.0, 60000.0, 164000.0, 160000.0)
        args = struct.pack('<6I', *range(100, 124, 4))
        written = {}

        def write(address, data):
            written[address] = struct.unpack('<f', data)[0]

        format = b'%f %f %f %f %f %f\n'
        read = lambda address, size: args[address:address + size]
        self.assertEqual(scan_cdecl(b'-44000 -3000 20000 60000 164000 160000', format, 0, read, write), 6)
        self.assertEqual(tuple(written.values()), values)
        written.clear()
        self.assertEqual(scan_cdecl(b'12.25 no-value', format, 0, read, write), 1)
        self.assertEqual(written, {100: 12.25})

    def test_loads_bounded_level_script_and_normalizes_text_mode(self):
        script = b'return 17\r\n'
        path = r'Data/Locations/Mountain_2\Logic\LevelScript.lua'
        with TemporaryDirectory() as directory:
            root = Path(directory)
            archive = root / 'Data2.bnd'
            header = struct.pack('<3I', 0x1B356852, 0, 1)
            entry = struct.pack('<I', len(script)) + script
            archive.write_bytes(header + entry + struct.pack('<2I', 0x7A6686C4, len(header)))
            self.assertEqual(open_level_script(root, path, 'r').read(), b'return 17\n')
            self.assertEqual(open_level_script(root, path, 'rb').read(), script)
            self.assertIsNone(open_level_script(root, '../' + path, 'r'))
            archive.write_bytes(header + entry + struct.pack('<2I', 0x7A6686C4, 999))
            with self.assertRaisesRegex(ValueError, 'record'):
                open_level_script(root, path, 'r')

    def test_surface_copy_applies_argb_key_and_compresses_small_mip(self):
        dxt5 = int.from_bytes(b'DXT5', 'little')
        color = b'\xff\x90\x89\xff'
        untouched = b'\xff\x90\x89\x80'
        self.assertEqual(convert_level(color + untouched, 2, 1, 21, 21, 0xFF8990FF), bytes(4) + untouched)
        compressed = convert_level(color * 4, 2, 2, 21, dxt5, 0xFF8990FF)
        self.assertEqual(len(compressed), storage_size(dxt5, 2, 2))
        self.assertEqual(convert_level(compressed, 2, 2, dxt5, 21, 0), bytes(16))


if __name__ == '__main__':
    unittest.main()
