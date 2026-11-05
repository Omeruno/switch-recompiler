"""Small synthetic interfaces; no game binary is needed in CI."""
import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from inspect_wasm import inspect


def u(number):
    result = bytearray()
    while number > 127:
        result.append((number & 127) | 128)
        number >>= 7
    result.append(number)
    return bytes(result)


def text(value):
    raw = value.encode()
    return u(len(raw)) + raw


def section(code, content):
    return bytes([code]) + u(len(content)) + content


HEADER = b'\0asm\x01\0\0\0'


class WasmInspectionTest(unittest.TestCase):
    def test_shared_interface(self):
        binary = HEADER + section(1, b'\x01\x60\0\0')
        imports = (u(2) + text('./glue.js') + text('draw') + b'\0\0'
                   + text('./glue.js') + text('memory') + b'\x02\x03' + u(266) + u(65536))
        binary += section(2, imports) + section(3, b'\x01\0')
        binary += section(7, b'\x01' + text('memory') + b'\x02\0')
        binary += section(10, b'\x01\x02\0\x0b')
        binary += section(0, text('target_features') + b'\x01+' + text('atomics'))
        result = inspect(binary, full_imports=True)
        self.assertEqual(result['imports_total'], 2)
        self.assertEqual(result['defined_functions'], 1)
        self.assertEqual(result['memories'][0]['minimum_bytes'], 17432576)
        self.assertEqual(result['memories'][0]['maximum_bytes'], 4294967296)
        self.assertTrue(result['memories'][0]['shared'])
        self.assertFalse(result['memories'][0]['memory64'])
        self.assertEqual(result['declared_target_features'], [{'prefix': '+', 'name': 'atomics'}])
        self.assertEqual(result['exports'][0]['kind'], 'memory')
        self.assertEqual(result['imports'][0]['name'], 'draw')

    def test_imported_wasi_and_defined_memory(self):
        binary = HEADER + section(2, b'\x01' + text('wasi_snapshot_preview1') + text('fd_write') + b'\0\0')
        binary += section(5, b'\x01\0\x01')
        result = inspect(binary)
        self.assertEqual(result['wasi_imports'], ['fd_write'])
        self.assertEqual(result['memories'][0]['minimum_bytes'], 65536)
        self.assertIsNone(result['memories'][0]['maximum'])
        self.assertNotIn('imports', result)

    def test_truncation(self):
        for binary in [b'\0asm', HEADER + b'\x02\x10\0', HEADER + b'\x02\x02\x01\x08']:
            with self.subTest(binary=binary), self.assertRaises(ValueError):
                inspect(binary)

    def test_integer_overflow(self):
        with self.assertRaises(ValueError):
            inspect(HEADER + b'\x01\xff\xff\xff\xff\x7f')

    def test_shared_requires_maximum(self):
        with self.assertRaises(ValueError):
            inspect(HEADER + section(5, b'\x01\x02\x01'))

    def test_bad_header_and_import_kind(self):
        with self.assertRaises(ValueError):
            inspect(b'not wasm')
        with self.assertRaises(ValueError):
            inspect(HEADER + section(2, b'\x01' + text('host') + text('bad') + b'\xff'))


if __name__ == '__main__':
    unittest.main()
