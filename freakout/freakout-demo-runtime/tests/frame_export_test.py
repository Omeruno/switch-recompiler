import hashlib
import struct
import unittest

from export_frame import DEFAULT_CLEAR, FrameExport, frame_clear

PRESENT = ("IDirect3DDevice9", "Present")
DRAW = ("draw", "DrawIndexedPrimitive", ())


class FrameClearTest(unittest.TestCase):
    def test_accepts_split_full_viewport_clear(self):
        color = ("Clear", (), 1, 0xFF203040, 0, 0)
        depth = ("Clear", ((0, 0, 1024, 768),), 6, 0, 0x3F800000, 0)
        self.assertEqual(frame_clear([PRESENT, color, depth, DRAW, PRESENT]), ((0xFF203040, 0x3F800000), set()))

    def test_preserves_depth_clear_between_draws(self):
        clear = ("Clear", (), 3, 0xFF000000, 0x3F800000, 0)
        depth = ("Clear", (), 2, 0, 0x3F800000, 0)
        self.assertEqual(frame_clear([PRESENT, clear, DRAW, depth, DRAW, depth, PRESENT]),
                         (DEFAULT_CLEAR, {1}))
        with self.assertRaisesRegex(ValueError, "unsupported color"):
            frame_clear([PRESENT, clear, DRAW, clear, DRAW, PRESENT])

    def test_version_2_marks_depth_reset_on_the_following_draw(self):
        capture = FrameExport()
        capture.draws = [bytes(20), bytes(20)]
        packet = capture.serialize(depth_resets={1})
        self.assertEqual(struct.unpack_from("<I", packet, 4)[0], 2)
        self.assertEqual(struct.unpack_from("<I", packet, 52 + 8 + 20 + 16)[0], 32)

    def test_uses_v2_only_for_nondefault_clear(self):
        capture = FrameExport()
        v1 = capture.serialize(DEFAULT_CLEAR)
        self.assertEqual(struct.unpack_from("<I", v1, 4)[0], 1)
        v2 = capture.serialize((0xFF203040, 0x3F800000))
        self.assertEqual(struct.unpack_from("<I", v2, 4)[0], 2)
        self.assertEqual(struct.unpack_from("<2I", v2, 52), (0xFF203040, 0x3F800000))
        self.assertEqual(hashlib.sha256(v2[52:]).digest(), v2[20:52])


if __name__ == "__main__":
    unittest.main()
