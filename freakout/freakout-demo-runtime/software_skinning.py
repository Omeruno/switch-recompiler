"""Feed software-skinned demo vertices to its original normal/specular shader."""
import hashlib
import math
import struct

from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EBP, UC_X86_REG_ECX, UC_X86_REG_EDX

from d3dx_math import multiply, transpose


_SKIN_INSTANCE = 0x6B7F50
_DEFORM = 0x6430F0
_SKIN_MATRIX = 0x5C777D
_SHADER = 'e46611cd552c091a8174a4b67e2c6624ed6a8e55dbc340802a59e08e6cbd712c'
_MATRIX = struct.Struct('<16f')
_VERTEX_STRIDE = 72


class SoftwareSkinning:
    def __init__(self, probe):
        self.probe = probe
        self.boot = probe.boot
        self.frame = -1
        self.deformed = set()
        self.pose = None
        self.shader = None
        self.boot.cpu.hook_add(UC_HOOK_CODE, self.note_deformation, begin=_DEFORM, end=_DEFORM)
        self.boot.cpu.hook_add(UC_HOOK_CODE, self.note_matrices, begin=_SKIN_MATRIX, end=_SKIN_MATRIX)

    def note_deformation(self, cpu, address, size, _):
        skin = cpu.reg_read(UC_X86_REG_ECX)
        if not skin or self.boot.get(skin) != _SKIN_INSTANCE:
            return
        if self.frame != self.probe.present_count:
            self.frame = self.probe.present_count
            self.deformed.clear()
        self.deformed.add(skin)

    def note_matrices(self, cpu, address, size, _):
        skin = self.boot.get(cpu.reg_read(UC_X86_REG_EBP) + 0x14)
        if self.frame != self.probe.present_count or skin not in self.deformed or self.boot.get(skin + 0x28):
            return
        renderer = self.boot.get(cpu.reg_read(UC_X86_REG_EDX) + 0x34)
        world, view, projection = (
            _MATRIX.unpack(cpu.mem_read(renderer + offset, _MATRIX.size))
            for offset in (0x780, 0x7C0, 0x800)
        )
        if all(map(math.isfinite, (*world, *view, *projection))):
            self.pose = (self.probe.draw_count + 1, world, view, projection)

    def draw(self, resource, stride, vertices):
        if not self.pose or self.pose[0] != self.probe.draw_count or stride != _VERTEX_STRIDE or resource is None:
            return vertices, {}
        if self.shader is not resource:
            if hashlib.sha256(resource.data).hexdigest() != _SHADER:
                return vertices, {}
            self.shader = resource
        _, world, view, projection = self.pose
        self.pose = None

        # The guest already animated positions and the normal/tangent basis in
        # object space. Use one blend entry to move that result to world space.
        vertices = bytearray(vertices)
        for offset in range(0, len(vertices), stride):
            vertices[offset + 12:offset + 28] = bytes(16)
        view_projection = transpose(multiply(view, projection))
        world = transpose(world)
        constants = {index: view_projection[index*4:index*4+4] for index in range(4)}
        constants.update({12 + row: world[row*4:row*4+4] for row in range(3)})
        return vertices, constants
