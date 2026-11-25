import unittest

from d3dx_math import invert, multiply, transform
from dx9_asm import assemble, shader_byte_length
from dx9_glsl import commands, translate
from guest_heap import GuestHeap


class ShaderTest(unittest.TestCase):
    def test_vs11_position_passthrough_has_d3d_tokens(self):
        shader = assemble('vs.1.1\nmov oPos, v0\n')
        self.assertEqual(shader.tokens, (0xFFFE0101, 1, 0xC00F0000, 0x90E40000, 0xFFFF))
        self.assertEqual(shader_byte_length(lambda offset: shader.tokens[offset // 4], 0, 'v'), 20)

    def test_sm2_relative_constant_preserves_address_token(self):
        shader = assemble('vs_2_0\ndcl_position v0\nmova a0.x, v0.x\nmov oPos, c[a0.x + 20]\n')
        instructions = list(commands(shader))
        self.assertEqual(instructions[0][1][0], 0x80000000)
        self.assertEqual(instructions[2][0] >> 24, 3)
        self.assertTrue(instructions[2][1][1] & 0x2000)
        self.assertIn('addr0.x + 20', translate(shader))

    def test_pixel_coissue_reads_preceding_register_before_write(self):
        shader = assemble('ps_1_1\ntex t0\nmov r0, t0\nmul r0.rgb, r0, t0\n+mov r0.a, r0.x\n')
        code = translate(shader)
        compute_alpha = code.index('vec4 q3 =')
        write_rgb = code.index('r0.xyz = (q2)')
        self.assertLess(compute_alpha, write_rgb)


class MathTest(unittest.TestCase):
    def test_inverts_non_symmetric_affine_transform(self):
        matrix = (2, 0, 0, 0, 0, 3, 0, 0, 0, 0, -4, 0, -6, 11, 8, 1)
        inverse, determinant = invert(matrix)
        self.assertEqual(determinant, -24)
        for index, value in enumerate(multiply(matrix, inverse)):
            self.assertAlmostEqual(value, 1 if index % 5 == 0 else 0)
        vector = (1, 2, 3, 1)
        for actual, expected in zip(transform(transform(vector, matrix), inverse), vector):
            self.assertAlmostEqual(actual, expected)
        self.assertIsNone(invert((0,) * 16))


class HeapTest(unittest.TestCase):
    def test_reuses_aligned_space_and_coalesces(self):
        heap = GuestHeap(0x100, 0x200)
        first, second, guard = [heap.allocate(size) for size in (17, 33, 1)]
        heap.free(second)
        heap.free(first)
        self.assertEqual(heap.allocate(65), first)
        heap.free(first)
        heap.free(guard)
        self.assertEqual(heap.top, 0x100)

    def test_preserves_data_across_growth_and_failed_realloc(self):
        heap = GuestHeap(0x10, 0x80)
        memory = bytearray(0x80)

        def read(address, count):
            return memory[address:address + count]

        def write(address, data):
            memory[address:address + len(data)] = data

        first = heap.allocate(10)
        write(first, b'0123456789')
        with self.assertRaises(MemoryError):
            heap.resize(first, 200, read, write)
        self.assertEqual(heap.allocations[first], 10)
        new = heap.resize(first, 33, read, write)
        self.assertEqual(read(new, 10), b'0123456789')
        self.assertEqual(heap.allocate(10), first)


if __name__ == '__main__':
    unittest.main()
