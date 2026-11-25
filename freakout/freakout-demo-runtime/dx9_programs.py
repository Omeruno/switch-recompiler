"""Shader assembly and COM resources for the Direct3D 9 trace."""
from dataclasses import dataclass
import hashlib
import struct

from d3dx_math import handle_math
from dx9_asm import assemble, shader_byte_length
from surface_blit import load_surface


INVALID_CALL = 0x8876086C
INVALID_DATA = 0x88760B59
NOT_IMPLEMENTED = 0x80004001
NO_INTERFACE = 0x80004002
_DECL_END = (255, 0, 17, 0, 0, 0)
_DECL = struct.Struct('<HHBBBB')
_COMMON = ('QueryInterface', 'AddRef', 'Release', 'GetDevice')
_INTERFACES = {
    'VertexDeclaration': 'IDirect3DVertexDeclaration9',
    'VertexShader': 'IDirect3DVertexShader9',
    'PixelShader': 'IDirect3DPixelShader9',
    'Buffer': 'ID3DXBuffer',
}
_IUNKNOWN = bytes.fromhex('0000000000000000c000000000000046')
_INTERFACE_IDS = {
    'VertexDeclaration': bytes.fromhex('9cc513ddfa369840a8fbc7ed39dc8546'),
    'VertexShader': bytes.fromhex('7e55c5ef656213468a9443857889eb36'),
    'PixelShader': bytes.fromhex('dcdb3b6d025b1544b852ce5e8bccb289'),
    'Buffer': bytes.fromhex('08fba58b9551e240ac580d989c3a0102'),
}


def read_declaration(read, address):
    elements = []
    for index in range(65):
        element = _DECL.unpack(read(address + _DECL.size * index, _DECL.size))
        if element == _DECL_END and elements:
            return (*elements, element)
        stream, _, data_type, method, usage, usage_index = element
        if stream >= 16 or data_type >= 17 or method > 6 or usage > 13 or usage_index > 15:
            break
        elements.append(element)
    raise ValueError('invalid or unterminated vertex declaration')


@dataclass
class Resource:
    kind: str
    data: bytes
    refs: int = 1
    guest_data: int = 0


class Dx9Programs:
    def __init__(self, probe):
        self.probe = probe
        self.boot = probe.boot
        self.resources = {}
        self.bindings = {}
        self.constants = {}

    def create_buffer(self, data):
        pointer = self.boot.allocate(len(data))
        self.boot.cpu.mem_write(pointer, data)
        methods = ('QueryInterface', 'AddRef', 'Release', 'GetBufferPointer', 'GetBufferSize')
        handle = self.probe.make_interface(_INTERFACES['Buffer'], methods)
        self.resources[handle] = Resource('Buffer', data, guest_data=pointer)
        return handle

    def assemble_file(self, arg, finish):
        path_address, defines, include, flags, output, errors = (arg(i) for i in range(6))
        if not path_address or not output:
            finish(INVALID_CALL, 24)
            return
        self.boot.put(output, 0)
        if errors:
            self.boot.put(errors, 0)
        if defines or include or flags:
            finish(NOT_IMPLEMENTED, 24)
            return

        filename = self.boot.c_string(path_address).decode('ascii', 'replace')
        path = self.boot.asset_path(filename)
        try:
            if path is None:
                raise ValueError('shader source path is outside the asset root')
            with path.open('rb') as stream:
                source = stream.read(131073)
            if len(source) > 131072:
                raise ValueError('shader assembly source exceeds 128 KiB')
            shader = assemble(source.decode('latin1'))
            data = shader.bytes()
        except (OSError, ValueError) as error:
            self.probe.events.append(('shader error', filename, str(error)))
            if errors:
                diagnostic = f'{filename}: {error}'.encode('latin1', 'replace') + b'\0'
                self.boot.put(errors, self.create_buffer(diagnostic))
            finish(INVALID_DATA, 24)
            return
        self.boot.put(output, self.create_buffer(data))
        self.probe.events.append(('assembled shader', filename, shader.kind, shader.major, shader.minor,
                                  len(data), hashlib.sha256(data).hexdigest()))
        finish(0, 24)

    def create_program(self, kind, arg, finish):
        pointer, output = arg(1), arg(2)
        if not pointer or not output:
            finish(INVALID_CALL, 12)
            return
        self.boot.put(output, 0)
        try:
            if kind == 'VertexDeclaration':
                elements = read_declaration(self.boot.cpu.mem_read, pointer)
                data = b''.join(_DECL.pack(*element) for element in elements)
                detail = ('vertex declaration', elements[:-1])
            else:
                size = shader_byte_length(self.boot.get, pointer, 'v' if kind == 'VertexShader' else 'p')
                data = bytes(self.boot.cpu.mem_read(pointer, size))
                detail = (kind, size, hashlib.sha256(data).hexdigest())
        except ValueError as error:
            self.probe.events.append(('invalid program', kind, str(error)))
            finish(INVALID_CALL, 12)
            return
        method = 'GetDeclaration' if kind == 'VertexDeclaration' else 'GetFunction'
        handle = self.probe.make_interface(_INTERFACES[kind], (*_COMMON, method))
        self.resources[handle] = Resource(kind, data)
        self.probe.events.append(detail)
        self.boot.put(output, handle)
        finish(0, 12)

    def release(self, handle):
        item = self.resources[handle]
        item.refs -= 1
        if not item.refs:
            del self.resources[handle]
        return item.refs

    def unbind(self, kind):
        old = self.bindings.pop(kind, 0)
        if old:
            self.release(old)

    def set_program(self, kind, requested, finish):
        if requested and (requested not in self.resources or self.resources[requested].kind != kind):
            finish(INVALID_CALL, 8)
            return
        if requested:
            self.resources[requested].refs += 1
        self.unbind(kind)
        if requested:
            self.bindings[kind] = requested
        self.probe.device_state['Set' + kind] = requested
        if kind == 'VertexDeclaration':
            self.probe.device_state.pop('SetFVF', None)
        finish(0, 8)

    def set_constants(self, name, arg, finish):
        start, address, count = arg(1), arg(2), arg(3)
        limit = (256 if 'Vertex' in name else 32) if name.endswith('F') else 16
        if start > limit or start + count > limit or (count and not address):
            finish(INVALID_CALL, 16)
            return
        unit = 4 if name.endswith('B') else 16
        content = bytes(self.boot.cpu.mem_read(address, count * unit)) if count else b''
        registers = self.constants.setdefault(name, {})
        registers.update((start + i, content[i * unit:(i + 1) * unit]) for i in range(count))
        finish(0, 16)

    def handle(self, dll, name, arg, finish):
        if dll.lower() == 'd3dx9_30.dll':
            if name == 'D3DXAssembleShaderFromFileA':
                self.assemble_file(arg, finish)
                return True
            if name == 'D3DXLoadSurfaceFromSurface':
                load_surface(self.probe, arg, finish)
                return True
            return handle_math(self.boot.cpu, name, arg, finish)
        if dll not in _INTERFACES.values():
            return False
        handle = arg(0)
        item = self.resources.get(handle)
        if item is None or _INTERFACES[item.kind] != dll:
            self.probe.stop(f'invalid {dll} this pointer at {name}')
        elif name == 'AddRef':
            item.refs += 1
            finish(item.refs, 4)
        elif name == 'Release':
            finish(self.release(handle), 4)
        elif name == 'QueryInterface':
            if not arg(1) or not arg(2):
                finish(INVALID_CALL, 12)
            else:
                iid = self.boot.cpu.mem_read(arg(1), 16)
                accepted = iid == _IUNKNOWN or iid == _INTERFACE_IDS.get(item.kind)
                self.boot.put(arg(2), handle if accepted else 0)
                if accepted:
                    item.refs += 1
                finish(0 if accepted else NO_INTERFACE, 12)
        elif name == 'GetDevice':
            if not arg(1):
                finish(INVALID_CALL, 8)
            else:
                self.probe.device_refs += 1
                self.boot.put(arg(1), self.probe.device)
                finish(0, 8)
        elif name == 'GetBufferPointer':
            finish(item.guest_data, 4)
        elif name == 'GetBufferSize':
            finish(len(item.data), 4)
        elif name in ('GetDeclaration', 'GetFunction') and arg(2):
            unit = _DECL.size if name == 'GetDeclaration' else 1
            capacity = self.boot.get(arg(2))
            size = len(item.data) // unit
            self.boot.put(arg(2), size)
            if arg(1) and capacity < size:
                finish(INVALID_CALL, 12)
            else:
                if arg(1):
                    self.boot.cpu.mem_write(arg(1), item.data)
                finish(0, 12)
        else:
            return False
        return True
