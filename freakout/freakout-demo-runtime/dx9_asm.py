"""Assemble the Direct3D 9 shader profiles used by the public FreakOut demo."""
from dataclasses import dataclass
import re
import struct


# Opcode and register values follow the Direct3D token definitions.
_ARITHMETIC = {
    'mov': (1, 1), 'add': (2, 2), 'sub': (3, 2), 'mad': (4, 3),
    'mul': (5, 2), 'rcp': (6, 1), 'rsq': (7, 1), 'dp3': (8, 2),
    'dp4': (9, 2), 'min': (10, 2), 'max': (11, 2), 'slt': (12, 2),
    'sge': (13, 2), 'lrp': (18, 3), 'frc': (19, 1), 'm4x4': (20, 2),
    'm4x3': (21, 2), 'm3x4': (22, 2), 'm3x3': (23, 2), 'm3x2': (24, 2),
    'pow': (32, 2), 'nrm': (36, 1), 'mova': (46, 1), 'cnd': (80, 3), 'cmp': (88, 3),
}
_USAGE = {
    'position': 0, 'blendweight': 1, 'blendindices': 2, 'normal': 3,
    'psize': 4, 'texcoord': 5, 'tangent': 6, 'binormal': 7, 'tessfactor': 8,
    'positiont': 9, 'color': 10, 'fog': 11, 'depth': 12, 'sample': 13,
}
_SHIFT = {'x2': 1, 'x4': 2, 'x8': 3, 'd2': 15, 'd4': 14, 'd8': 13}
_COMPONENT = {name: index for index, pair in enumerate(zip('xyzw', 'rgba')) for name in pair}
_VERSION = re.compile(r'([vp])s[._]([12])[._](\d+)')
_REGISTER = re.compile(r'(opc|oc|od|ot|opos|ofog|opts|odepth|[rvctsao])(\d*)')
_RELATIVE = re.compile(r'^c\[a0\.x((?:[+-]\d+)*)\]')
_OPERAND = re.compile(r'^(-|1-)?(.+?)(?:_(bias|bx2|x2|dz|dw|abs))?(?:\.([xyzwrgba]{1,4}))?$')
_SRCMOD = {None: 0, 'bias': 2, 'bx2': 4, 'x2': 7, 'dz': 9, 'dw': 10, 'abs': 11}


@dataclass(frozen=True)
class Shader:
    kind: str
    major: int
    minor: int
    tokens: tuple

    def bytes(self):
        return struct.pack(f'<{len(self.tokens)}I', *self.tokens)


def parameter(text, destination, kind):
    text = ''.join(text.split())
    # The period in a0.x belongs to the address expression, not a swizzle.
    relative = _RELATIVE.match(text)
    if relative:
        index = sum(map(int, re.findall(r'[+-]\d+', relative[1])))
        text = f'c{index}' + text[relative.end():]
    match = _OPERAND.fullmatch(text)
    if not match:
        raise ValueError(f'invalid shader operand {text!r}')
    prefix, operand, modifier, components = match.groups()
    if operand == 'opc':
        operand = 'opts'
    register = _REGISTER.fullmatch(operand)
    if not register:
        raise ValueError(f'unsupported register {operand!r}')
    name, number = register.groups()
    types = {'r': 0, 'v': 1, 'c': 2, 't': 3, 'a': 3, 'od': 5,
             'ot': 6, 'o': 6, 'oc': 8, 'odepth': 9, 's': 10,
             'opos': 4, 'ofog': 4, 'opts': 4}
    if name in ('opos', 'ofog', 'opts', 'odepth'):
        if number:
            raise ValueError(f'unexpected index in {operand!r}')
        number = {'opos': 0, 'ofog': 1, 'opts': 2, 'odepth': 0}[name]
    else:
        if not number:
            raise ValueError(f'missing register index in {operand!r}')
        number = int(number)
    if number < 0 or number > 2047 or (relative and (destination or kind != 'v')):
        raise ValueError(f'unsupported register addressing {operand!r}')
    regtype = types[name]
    token = 0x80000000 | number | ((regtype & 7) << 28) | ((regtype & 24) << 8)
    if destination:
        if prefix or modifier:
            raise ValueError(f'unsupported destination modifier in {text!r}')
        lanes = [_COMPONENT[x] for x in components or 'xyzw']
        if lanes != sorted(set(lanes)):
            raise ValueError('destination mask must be ordered and unique')
        return token | (sum(1 << index for index in lanes) << 16), bool(relative)
    lanes = [_COMPONENT[x] for x in components or 'xyzw']
    lanes.extend([lanes[-1]] * (4 - len(lanes)))
    srcmod = _SRCMOD[modifier]
    if prefix == '-':
        if srcmod in (9, 10):
            raise ValueError('negated projection modifier')
        srcmod += 1
    elif prefix == '1-':
        if srcmod:
            raise ValueError('complement cannot be combined with another modifier')
        srcmod = 6
    return token | (srcmod << 24) | (sum(lane << (2 * i) for i, lane in enumerate(lanes)) << 16), bool(relative)


def instruction(head, arguments, kind, major):
    coissue = head.startswith('+')
    parts = head.removeprefix('+').split('_')
    mnemonic, modifiers = parts[0], parts[1:]
    if coissue and (major != 1 or kind != 'p'):
        raise ValueError('co-issue is only supported in ps_1_1')
    if mnemonic == 'dcl':
        if len(arguments) != 1 or len(modifiers) > 1:
            raise ValueError('invalid shader declaration')
        usage = modifiers[0] if modifiers else ''
        semantic = 0
        if usage in ('2d', 'cube', 'volume'):
            semantic = {'2d': 2, 'cube': 3, 'volume': 4}[usage] << 27
        elif usage:
            match = re.fullmatch(r'([a-z]+)(\d*)', usage)
            if not match or match[1] not in _USAGE or int(match[2] or 0) > 15:
                raise ValueError(f'unsupported declaration semantic {usage!r}')
            semantic = _USAGE[match[1]] | (int(match[2] or 0) << 16)
        destination, _ = parameter(arguments[0], True, kind)
        return (31 | ((2 if major >= 2 else 0) << 24), 0x80000000 | semantic, destination)
    if mnemonic == 'def':
        if len(arguments) != 5 or modifiers or coissue:
            raise ValueError('def needs a constant register and four literals')
        destination, _ = parameter(arguments[0], True, kind)
        values = (float(argument.strip().removesuffix('f')) for argument in arguments[1:])
        literals = struct.unpack('<4I', struct.pack('<4f', *values))
        return (81 | ((5 if major >= 2 else 0) << 24), destination, *literals)
    opcode_flags = 0
    if mnemonic in _ARITHMETIC:
        opcode, sources = _ARITHMETIC[mnemonic]
    elif kind == 'p' and mnemonic in ('tex', 'texcoord') and major == 1:
        opcode, sources = (66 if mnemonic == 'tex' else 64), 0
    elif kind == 'p' and mnemonic in ('texld', 'texldb', 'texldp') and major == 2:
        opcode, sources = 66, 2
        opcode_flags = {'texld': 0, 'texldp': 1 << 16, 'texldb': 2 << 16}[mnemonic]
    else:
        raise ValueError(f'unsupported shader mnemonic {mnemonic!r}')
    if len(arguments) != sources + 1:
        raise ValueError(f'{mnemonic} requires {sources + 1} operands')
    destination, _ = parameter(arguments[0], True, kind)
    for modifier in modifiers:
        if modifier == 'sat':
            destination |= 1 << 20
        elif modifier in _SHIFT:
            destination |= _SHIFT[modifier] << 24
        else:
            raise ValueError(f'unsupported instruction modifier {modifier!r}')
    tokens = [destination]
    for argument in arguments[1:]:
        token, relative = parameter(argument, False, kind)
        tokens.append(token | (0x2000 if relative else 0))
        if relative and major >= 2:
            tokens.append(parameter('a0.x', False, kind)[0])
    word = opcode | opcode_flags | (0x40000000 if coissue else 0)
    if major >= 2:
        word |= len(tokens) << 24
    return (word, *tokens)


def assemble(source):
    tokens = []
    profile = None
    for number, line in enumerate(source.splitlines(), 1):
        line = re.split(r';|//', line, maxsplit=1)[0].strip().lower()
        if not line:
            continue
        try:
            if profile is None:
                match = _VERSION.fullmatch(line)
                if not match or (int(match[2]), int(match[3])) not in ((1, 1), (2, 0)):
                    raise ValueError(f'unsupported shader profile {line!r}')
                kind, major, minor = match[1], int(match[2]), int(match[3])
                profile = (kind, major, minor)
                tokens.append((0xfffe0000 if kind == 'v' else 0xffff0000) | (major << 8) | minor)
            else:
                # Shader sources also use tabs between mnemonics and operands.
                split = line.split(None, 1)
                head = split[0]
                arguments = split[1].split(',') if len(split) == 2 else []
                tokens.extend(instruction(head, arguments, kind, major))
        except (ValueError, OverflowError) as error:
            raise ValueError(f'line {number}: {error}') from error
    if profile is None or len(tokens) < 2:
        raise ValueError('shader is empty')
    return Shader(*profile, (*tokens, 0xffff))


def shader_byte_length(read_word, address, kind):
    """Walk a shader's tokens without treating parameter data as opcodes."""
    version = read_word(address)
    major = (version >> 8) & 255
    if (version >> 16 != (0xfffe if kind == 'v' else 0xffff)
            or (major, version & 255) not in ((1, 1), (2, 0))):
        raise ValueError(f'unsupported shader bytecode version {version:#x}')
    arity = {opcode: sources + 1 for opcode, sources in _ARITHMETIC.values()}
    arity.update({31: 2, 81: 5, 64: 1, 66: 1})
    cursor = address + 4
    while cursor - address < 32768:
        token = read_word(cursor)
        opcode = token & 65535
        if token == 65535:
            return cursor + 4 - address
        if opcode == 65534:
            operands = (token >> 16) & 32767
        elif major == 2:
            operands = (token >> 24) & 15
        elif opcode in arity:
            operands = arity[opcode]
        elif opcode == 0:
            operands = 0
        else:
            raise ValueError(f'unknown shader 1.x opcode {opcode}')
        cursor += 4 * (1 + operands)
    raise ValueError('shader end token not found within 32 KiB')
