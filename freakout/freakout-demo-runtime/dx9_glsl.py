"""Translate the demo's SM1/SM2 bytecode to GLSL for uam."""
import argparse
import math
from pathlib import Path
import struct

from dx9_asm import _ARITHMETIC, assemble


_ARITY = {opcode: sources + 1 for opcode, sources in _ARITHMETIC.values()}
_OPS = {opcode: name for name, (opcode, _) in _ARITHMETIC.items()}
_LANES = 'xyzw'
_ARITY.update({0: 0, 31: 2, 64: 1, 66: 1, 81: 5})
_FLOATING = 0x80000000


def commands(shader):
    cursor = 1
    while cursor < len(shader.tokens):
        token = shader.tokens[cursor]
        opcode = token & 65535
        if token == 65535 and cursor == len(shader.tokens) - 1:
            return
        if opcode == 65534:
            cursor += 1 + ((token >> 16) & 32767)
            continue
        size = (token >> 24) & 15 if shader.major >= 2 else _ARITY.get(opcode)
        if size is None or cursor + size >= len(shader.tokens):
            raise ValueError(f'invalid instruction at token {cursor}')
        yield token, shader.tokens[cursor + 1:cursor + 1 + size]
        cursor += 1 + size
    raise ValueError('missing shader end token')


def register_type(word):
    return ((word >> 28) & 7) | ((word >> 8) & 24)


def register_index(word):
    return word & 2047


def register_name(word, vertex):
    category, index = register_type(word), register_index(word)
    names = {0: ('r', 32), 1: ('v', 16 if vertex else 2),
             5: ('out_color', 2), 6: ('out_tex', 8), 8: ('result', 1),
             10: ('texture_', 16)}
    if category == 3:
        names[3] = ('addr', 1) if vertex else ('t', 8)
    if vertex and category == 4 and index == 0:
        return 'position'
    if category in names:
        name, limit = names[category]
        if index < limit:
            return f'{name}{index}'
    raise ValueError(f'unhandled {"vertex" if vertex else "pixel"} register {category}:{index}')


def swizzle(word):
    return ''.join(_LANES[(word >> (16 + 2 * i)) & 3] for i in range(4))


def constant_at(word, addressing):
    index = register_index(word)
    return f'load_constant({addressing} + {index})' if addressing else f'load_constant({index})'


def source(word, vertex, addressing=None):
    category = register_type(word)
    if not word & _FLOATING:
        raise ValueError('expected a source parameter token')
    register = constant_at(word, addressing) if category == 2 else register_name(word, vertex)
    value = f'{register}.{swizzle(word)}'
    modifier = (word >> 24) & 15
    patterns = {
        0: '{}', 1: '-({})', 2: '({} - 0.5)', 3: '(0.5 - {})', 4: '({} * 2.0 - 1.0)',
        5: '(1.0 - {} * 2.0)', 6: '(1.0 - {})', 7: '({} * 2.0)', 8: '({} * -2.0)',
        11: 'abs({})', 12: '-abs({})',
    }
    if modifier not in patterns:
        raise ValueError(f'unhandled source modifier {modifier}')
    return patterns[modifier].format(value)


def operands(words, shader):
    values = []
    cursor = 0
    while cursor < len(words):
        word = words[cursor]
        cursor += 1
        address = None
        if word & 0x2000:
            if shader.kind != 'v' or register_type(word) != 2:
                raise ValueError('relative addressing outside the vertex constant file')
            lane = 'x'
            if shader.major >= 2:
                addr_word = words[cursor]
                cursor += 1
                if register_type(addr_word) != 3 or register_index(addr_word):
                    raise ValueError('invalid relative address register')
                lane = swizzle(addr_word)[0]
            address = f'addr0.{lane}'
        values.append((word, address))
    return values


def arithmetic(name, arguments, vertex):
    values = [source(word, vertex, relative) for word, relative in arguments]
    a, b, c = (*values, '', '', '')[:3]
    patterns = {
        'mov': '{a}', 'mova': '{a}', 'add': '{a} + {b}', 'sub': '{a} - {b}',
        'mul': '{a} * {b}', 'mad': '({a} * {b}) + {c}', 'rcp': 'vec4(1.0 / ({a}).x)',
        'rsq': 'vec4(inversesqrt(abs(({a}).x)))', 'dp3': 'vec4(dot(({a}).xyz, ({b}).xyz))',
        'dp4': 'vec4(dot({a}, {b}))', 'slt': 'vec4(lessThan({a}, {b}))',
        'sge': 'vec4(greaterThanEqual({a}, {b}))', 'min': 'min({a}, {b})', 'max': 'max({a}, {b})',
        'lrp': 'mix({c}, {b}, {a})', 'frc': 'fract({a})',
        'pow': 'vec4(pow(abs(({a}).x), ({b}).x))', 'nrm': 'vec4(normalize(({a}).xyz), 0.0)',
        'cnd': '(({a}).x > 0.5 ? ({b}) : ({c}))', 'cmp': 'mix({c}, {b}, greaterThanEqual({a}, vec4(0.0)))',
    }
    if name in patterns:
        return patterns[name].format(a=a, b=b, c=c), 'xyzw' if name != 'nrm' else 'xyz'
    if name.startswith(('m3x', 'm4x')):
        word, relative = arguments[1]
        length, rows = int(name[1]), int(name[-1])
        lanes = _LANES[:length]
        terms = [f'dot(({a}).{lanes}, ({source(word + i, vertex, relative)}).{lanes})'
                 for i in range(rows)]
        return 'vec4(' + ', '.join(terms + ['0.0'] * (4 - rows)) + ')', _LANES[:rows]
    raise ValueError(f'unhandled arithmetic {name!r}')


def assignment(destination, name, lanes, vertex):
    selected = ''.join(lane for i, lane in enumerate(_LANES)
                       if ((destination >> (16 + i)) & 1) and lane in lanes)
    if not selected:
        raise ValueError('empty write mask')
    shift = (destination >> 24) & 15
    if shift:
        if shift not in (1, 2, 3, 13, 14, 15):
            raise ValueError('bad destination scale')
        name = f'({name} * {float(2 ** (shift if shift < 8 else shift - 16))})'
    modifier = (destination >> 20) & 15
    if modifier & ~1:
        raise ValueError('unsupported destination modifier')
    if modifier & 1:
        name = f'clamp({name}, 0.0, 1.0)'
    register = register_name(destination, vertex)
    if vertex and register == 'addr0':
        return f'{register}.{selected} = ivec{len(selected)}(roundEven(({name}).{selected}));' if len(selected) > 1 else f'{register}.{selected} = int(roundEven(({name}).{selected}));'
    return f'{register}.{selected} = ({name}).{selected};'


def translate(shader):
    vertex = shader.kind == 'v'
    limit = 256 if vertex else 32
    code = ['#version 460', f'layout(std140, binding = 0) uniform Registers {{ vec4 constants[{limit}]; }};']
    if vertex:
        code += [f'layout(location = {i}) in vec4 v{i};' for i in range(16)]
        code += [f'layout(location = {i}) out vec4 out_color{i};' for i in range(2)]
        code += [f'layout(location = {i + 2}) out vec4 out_tex{i};' for i in range(8)]
    else:
        code += [f'layout(location = {i}) in vec4 v{i};' for i in range(2)]
        code += [f'layout(location = {i + 2}) in vec4 coord{i};' for i in range(8)]
        code += ['layout(location = 0) out vec4 result0;']

    stream = list(commands(shader))
    samplers = {}
    definitions = {}
    for token, words in stream:
        opcode = token & 65535
        if opcode == 31 and register_type(words[1]) == 10:
            sampler_type = {2: 'sampler2D', 3: 'samplerCube', 4: 'sampler3D'}.get((words[0] >> 27) & 15)
            if not sampler_type:
                raise ValueError('unsupported sampler dimension')
            samplers[register_index(words[1])] = sampler_type
        elif shader.major == 1 and opcode == 66:
            samplers[register_index(words[0])] = 'sampler2D'
        elif opcode == 81:
            vector = struct.unpack('<4f', struct.pack('<4I', *words[1:]))
            if not all(math.isfinite(value) for value in vector):
                raise ValueError('nonfinite shader literal')
            definitions[register_index(words[0])] = vector

    for slot, dimension in sorted(samplers.items()):
        code.append(f'layout(binding = {slot}) uniform {dimension} texture_{slot};')
    code += ['vec4 load_constant(int index) {']
    for i, components in sorted(definitions.items()):
        code.append(f'    if (index == {i}) return vec4({", ".join(map(str, components))});')
    code += [f'    return index >= 0 && index < {limit} ? constants[index] : vec4(0.0);', '}', 'void main() {']
    code += [f'    vec4 r{i} = vec4(0.0);' for i in range(32)]
    if vertex:
        code += ['    vec4 position = vec4(0.0);', '    ivec4 addr0 = ivec4(0);']
        code += [f'    out_color{i} = vec4(0.0);' for i in range(2)]
        code += [f'    out_tex{i} = vec4(0.0);' for i in range(8)]
    else:
        code += [f'    vec4 t{i} = coord{i};' for i in range(8)]
        code += ['    result0 = vec4(0.0);']

    pending = []
    for index, (token, words) in enumerate(stream):
        opcode = token & 65535
        if opcode in (0, 31, 81):
            continue
        dest = words[0]
        if opcode in (64, 66) and shader.major == 1 and not vertex:
            slot = register_index(dest)
            value = f'clamp(vec4(coord{slot}.xyz, 1.0), 0.0, 1.0)' if opcode == 64 else f'texture(texture_{slot}, coord{slot}.xy)'
            lanes = 'xyzw'
        elif opcode == 66 and shader.major == 2 and not vertex:
            uv = source(words[1], False)
            slot = register_index(words[2])
            dims = 'xy' if samplers[slot] == 'sampler2D' else 'xyz'
            flags = (token >> 16) & 255
            coords = f'({uv}).{dims}'
            if flags == 1:
                coords += f' / ({uv}).w'
            bias = f', ({uv}).w' if flags == 2 else ''
            value, lanes = f'texture(texture_{slot}, {coords}{bias})', 'xyzw'
        elif opcode in _OPS:
            value, lanes = arithmetic(_OPS[opcode], operands(words[1:], shader), vertex)
        else:
            raise ValueError(f'unhandled shader opcode {opcode}')
        target = f'q{index}'
        code.append(f'    vec4 {target} = {value};')
        pending.append(assignment(dest, target, lanes, vertex))
        if index + 1 == len(stream) or not stream[index + 1][0] & 0x40000000:
            code.extend(f'    {line}' for line in pending)
            pending.clear()
    if vertex:
        code.append('    gl_Position = position;')
    elif shader.major == 1:
        code.append('    result0 = r0;')
    code.append('}')
    return '\n'.join(code) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    args.output.write_text(translate(assemble(args.source.read_text(encoding='latin1'))))


if __name__ == '__main__':
    main()
