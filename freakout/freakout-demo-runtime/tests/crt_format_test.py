import struct
import unittest

from crt_format import format_cdecl


class CrtFormatTest(unittest.TestCase):
    def test_formats_race_hud_widths_and_precision(self):
        memory = struct.pack("<did", 12.25, -7, -1.125)
        self.assertEqual(format_cdecl(b"%0.1f %03i %06.2f", 0,
                                      lambda address, size: memory[address:address + size],
                                      lambda address: b""), b"12.3 -07 -01.13")

    def test_mixed_cdecl_arguments_keep_word_alignment(self):
        arguments = struct.pack("<dIdII", -0.0, 0x80000000, 2.5, 0xFFFFFFFF, 0xBEEF)
        self.assertEqual(
            format_cdecl(b"%f %ld %.0f %lx %s %%", 0,
                         lambda address, size: arguments[address:address + size],
                         lambda address: {0xBEEF: b"demo"}[address]),
            b"-0.000000 -2147483648 3 ffffffff demo %",
        )

    def test_rejects_unimplemented_conversion(self):
        with self.assertRaisesRegex(ValueError, "unsupported sprintf"):
            format_cdecl(b"%n", 0, None, None)


if __name__ == "__main__":
    unittest.main()
