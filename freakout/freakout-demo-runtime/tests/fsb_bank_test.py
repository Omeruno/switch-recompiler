import struct
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

from audio_probe import AudioProbe
from fsb_bank import parse_bank


def bank_bytes():
    first = struct.pack("<H30s6I4H", 64, b"one.wav", 2, 4, 0, 1, 0x131, 44100, 0, 0, 0, 1)
    basic = struct.pack("<2I", 1, 2)
    return (struct.pack("<4s5I", b"FSB3", 2, len(first) + len(basic), 6, 0x30001, 2)
            + first + basic + b"\x01\x02\x03\x04\x05\x06")


class FsbBankTest(unittest.TestCase):
    def test_reads_basic_header_with_inherited_mode(self):
        one, two = parse_bank(bank_bytes())
        self.assertEqual((one.name, one.frames, bytes(one.payload)), (b"one.wav", 2, b"\x01\x02\x03\x04"))
        self.assertEqual((two.name, two.frames, bytes(two.payload)), (b"", 1, b"\x05\x06"))
        self.assertEqual((two.mode, two.rate, two.channels), (one.mode, one.rate, one.channels))

    def test_rejects_samples_exceeding_data(self):
        data = bytearray(bank_bytes())
        struct.pack_into("<I", data, 24 + 64 + 4, 4)
        with self.assertRaisesRegex(ValueError, "invalid FSB sample data"):
            parse_bank(data)

    def test_bank_owns_sample_handles(self):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / "MENU.fsb"
            path.write_bytes(bank_bytes())
            handles = iter(range(1, 4))
            boot = SimpleNamespace(asset_path=lambda name: Path(root) / name,
                                   allocate=lambda size: next(handles))
            audio = AudioProbe(boot)
            self.assertEqual(audio.load_bank("missing.fsb"), 0)
            bank = audio.load_bank(path.name, 0x2002)
            one, two = audio.banks[bank]
            self.assertIn(one, audio.samples)
            self.assertIn(two, audio.samples)
            self.assertEqual((audio.sample_modes[one] & 0x2007, audio.payload_bytes), (0x2002, 6))
            self.assertTrue(audio.free_bank(bank))
            self.assertFalse(audio.free_bank(bank))
            self.assertEqual((audio.banks, audio.samples, audio.sample_modes), ({}, {}, {}))
            self.assertEqual((audio.payload_bytes, audio.peak_payload_bytes), (0, 6))


if __name__ == "__main__":
    unittest.main()
