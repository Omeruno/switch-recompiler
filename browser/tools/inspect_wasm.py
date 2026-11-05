"""Inspect a WASM interface without instantiating or executing the module.

This reads structural metadata only. Use a real validator for function bodies.
The game binary is an external research input and is never copied into the repo.
"""
import argparse
import collections
import hashlib
import json
from pathlib import Path


class Reader:
    def __init__(self, data):
        self.data = memoryview(data)
        self.position = 0

    def remaining(self):
        return len(self.data) - self.position

    def take(self, size):
        if size < 0 or size > self.remaining():
            raise ValueError('truncated WASM field')
        result = self.data[self.position:self.position + size]
        self.position += size
        return result

    def byte(self):
        return self.take(1)[0]

    def unsigned(self, bits=32):
        result = 0
        for shift in range(0, bits, 7):
            value = self.byte()
            result |= (value & 127) << shift
            if not value & 128:
                if result >= 1 << bits:
                    raise ValueError('WASM integer overflow')
                return result
        raise ValueError('overlong WASM integer')

    def signed(self, bits=33):
        result = 0
        for shift in range(0, bits, 7):
            value = self.byte()
            result |= (value & 127) << shift
            if not value & 128:
                if value & 64:
                    result -= 1 << (shift + 7)
                if not -(1 << (bits - 1)) <= result < 1 << (bits - 1):
                    raise ValueError('WASM signed integer overflow')
                return result
        raise ValueError('overlong WASM signed integer')

    def string(self):
        return bytes(self.take(self.unsigned())).decode('utf-8')

    def value_type(self):
        code = self.byte()
        if code in (0x63, 0x64):
            return {'reference': hex(code), 'heap_type': self.signed()}
        names = {0x7f: 'i32', 0x7e: 'i64', 0x7d: 'f32', 0x7c: 'f64',
                 0x7b: 'v128', 0x70: 'funcref', 0x6f: 'externref'}
        if code not in names:
            raise ValueError(f'unsupported value type 0x{code:x}')
        return names[code]

    def limits(self, memory=False):
        flags = self.unsigned()
        if flags & ~7:
            raise ValueError(f'unsupported limits flags: {flags}')
        bits = 64 if flags & 4 else 32
        minimum = self.unsigned(bits)
        maximum = self.unsigned(bits) if flags & 1 else None
        if maximum is not None and minimum > maximum:
            raise ValueError('invalid WASM limits')
        result = {'minimum': minimum, 'maximum': maximum}
        if memory:
            if flags & 2 and maximum is None:
                raise ValueError('shared memory requires a maximum')
            result.update(shared=bool(flags & 2), memory64=bool(flags & 4),
                          minimum_bytes=minimum * 65536,
                          maximum_bytes=maximum * 65536 if maximum is not None else None)
        return result


SECTION_NAMES = {0: 'custom', 1: 'type', 2: 'import', 3: 'function', 4: 'table',
                 5: 'memory', 6: 'global', 7: 'export', 8: 'start', 9: 'element',
                 10: 'code', 11: 'data', 12: 'data_count', 13: 'tag'}
KINDS = ['function', 'table', 'memory', 'global', 'tag']


def inspect(data, full_imports=False):
    reader = Reader(data)
    if bytes(reader.take(8)) != b'\0asm\x01\0\0\0':
        raise ValueError('not a WASM v1 binary')
    imports, exports, memories, tables, sections, features = [], [], [], [], [], []
    definitions = 0
    while reader.remaining():
        code = reader.byte()
        if code not in SECTION_NAMES:
            raise ValueError(f'unknown WASM section {code}')
        size = reader.unsigned()
        section = Reader(reader.take(size))
        detail = {'name': SECTION_NAMES[code], 'bytes': size}
        if code == 0:
            detail['custom_name'] = section.string()
            if detail['custom_name'] == 'target_features':
                for _ in range(section.unsigned()):
                    prefix = chr(section.byte())
                    name = section.string()
                    features.append({'prefix': prefix, 'name': name})
        elif code == 2:
            detail['count'] = section.unsigned()
            for _ in range(detail['count']):
                module, name, kind = section.string(), section.string(), section.byte()
                if kind >= len(KINDS):
                    raise ValueError('unknown WASM import kind')
                item = {'module': module, 'name': name, 'kind': KINDS[kind]}
                if kind == 0:
                    item['type_index'] = section.unsigned()
                elif kind == 1:
                    item.update(element=section.value_type(), **section.limits())
                    tables.append(item.copy())
                elif kind == 2:
                    item.update(section.limits(memory=True))
                    memories.append(item.copy())
                elif kind == 3:
                    item.update(value_type=section.value_type(), mutable=section.byte())
                else:
                    item.update(attribute=section.unsigned(), type_index=section.unsigned())
                imports.append(item)
        elif code == 4:
            detail['count'] = section.unsigned()
            for _ in range(detail['count']):
                tables.append({'element': section.value_type(), **section.limits()})
        elif code == 5:
            detail['count'] = section.unsigned()
            memories.extend(section.limits(memory=True) for _ in range(detail['count']))
        elif code == 7:
            detail['count'] = section.unsigned()
            for _ in range(detail['count']):
                name, kind, index = section.string(), section.byte(), section.unsigned()
                if kind >= len(KINDS):
                    raise ValueError('unknown WASM export kind')
                exports.append({'name': name, 'kind': KINDS[kind], 'index': index})
        elif code in (1, 3, 6, 9, 10, 11, 12, 13):
            detail['count'] = section.unsigned()
            if code == 3:
                definitions = detail['count']
        elif code == 8:
            detail['function_index'] = section.unsigned()
        sections.append(detail)
    result = {
        'analysis': 'structural metadata only; function bodies are not validated or executed',
        'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest(),
        'sections': sections, 'imports_total': len(imports),
        'imports_by_kind': dict(collections.Counter(i['kind'] for i in imports)),
        'imports_by_module': dict(collections.Counter(i['module'] for i in imports)),
        'defined_functions': definitions, 'exports_total': len(exports),
        'exports': exports, 'memories': memories, 'tables': tables,
        'declared_target_features': features,
        'non_function_imports': [i for i in imports if i['kind'] != 'function'],
        'wasi_imports': [i['name'] for i in imports if i['module'].startswith('wasi_')],
    }
    if full_imports:
        result['imports'] = imports
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('wasm', type=Path)
    parser.add_argument('--full-imports', action='store_true')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    output = json.dumps(inspect(args.wasm.read_bytes(), args.full_imports), indent=2, ensure_ascii=False) + '\n'
    if args.output:
        args.output.write_text(output)
    else:
        print(output, end='')
