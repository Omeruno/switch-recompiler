"""Replay an indexed 3D frame captured from the demo on desktop OpenGL."""
import argparse
import ctypes as ct
import json
from pathlib import Path
import struct

import numpy as np
from PIL import Image

from dx9_asm import Shader
from dx9_glsl import commands, register_index, translate


PTR, UINT, INT, FLOAT = ct.c_void_p, ct.c_uint, ct.c_int, ct.c_float


def api(lib, name, result, *arguments):
    function = getattr(lib, name)
    function.restype = result
    function.argtypes = arguments
    return function


def int_array(*values):
    return (INT * len(values))(*values)


def open_context(width, height):
    egl = ct.CDLL('libEGL.so.1')
    display = api(egl, 'eglGetPlatformDisplay', PTR, UINT, PTR, PTR)(0x31DD, None, None)
    if not display or not api(egl, 'eglInitialize', UINT, PTR, PTR, PTR)(display, None, None):
        raise RuntimeError('could not initialize EGL surfaceless display')
    if not api(egl, 'eglBindAPI', UINT, UINT)(0x30A2):
        raise RuntimeError('could not bind desktop OpenGL')
    config, count = PTR(), INT()
    attributes = int_array(0x3033, 1, 0x3040, 8, 0x3024, 8, 0x3023, 8,
                           0x3022, 8, 0x3021, 8, 0x3025, 24, 0x3038)
    choose = api(egl, 'eglChooseConfig', UINT, PTR, PTR, PTR, INT, PTR)
    if not choose(display, attributes, ct.byref(config), 1, ct.byref(count)) or not count.value:
        raise RuntimeError('no EGL configuration with RGBA and depth')
    create_surface = api(egl, 'eglCreatePbufferSurface', PTR, PTR, PTR, PTR)
    surface = create_surface(display, config, int_array(0x3057, width, 0x3056, height, 0x3038))
    create_context = api(egl, 'eglCreateContext', PTR, PTR, PTR, PTR, PTR)
    context = create_context(display, config, None, int_array(0x3098, 4, 0x30FB, 5, 0x30FD, 1, 0x3038))
    make_current = api(egl, 'eglMakeCurrent', UINT, PTR, PTR, PTR, PTR)
    if not surface or not context or not make_current(display, surface, surface, context):
        raise RuntimeError('could not create OpenGL 4.5 context')
    return egl, display, surface, context


class GL:
    def __init__(self):
        lib = ct.CDLL('libGL.so.1')
        for (name, result, *types) in (
            ('GetString', ct.c_char_p, UINT), ('GetError', UINT),
            ('CreateShader', UINT, UINT), ('CreateProgram', UINT),
            ('GetShaderiv', None, UINT, UINT, PTR), ('GetProgramiv', None, UINT, UINT, PTR),
            ('GetShaderInfoLog', None, UINT, INT, PTR, PTR),
            ('GetProgramInfoLog', None, UINT, INT, PTR, PTR),
            ('ShaderSource', None, UINT, INT, PTR, PTR), ('CompileShader', None, UINT),
            ('AttachShader', None, UINT, UINT), ('DeleteShader', None, UINT),
            ('LinkProgram', None, UINT), ('UseProgram', None, UINT),
            ('Uniform1i', None, INT, INT),
            ('GenBuffers', None, INT, PTR), ('BindBuffer', None, UINT, UINT),
            ('BufferData', None, UINT, ct.c_ssize_t, PTR, UINT),
            ('BindBufferBase', None, UINT, UINT, UINT),
            ('GenVertexArrays', None, INT, PTR), ('BindVertexArray', None, UINT),
            ('VertexAttribPointer', None, UINT, INT, UINT, ct.c_ubyte, INT, PTR),
            ('EnableVertexAttribArray', None, UINT), ('DisableVertexAttribArray', None, UINT),
            ('GenTextures', None, INT, PTR), ('BindTexture', None, UINT, UINT),
            ('ActiveTexture', None, UINT), ('TexParameteri', None, UINT, UINT, INT),
            ('TexImage2D', None, UINT, INT, INT, INT, INT, INT, UINT, UINT, PTR),
            ('GenFramebuffers', None, INT, PTR), ('BindFramebuffer', None, UINT, UINT),
            ('FramebufferTexture2D', None, UINT, UINT, UINT, UINT, INT),
            ('CheckFramebufferStatus', UINT, UINT),
            ('GenRenderbuffers', None, INT, PTR), ('BindRenderbuffer', None, UINT, UINT),
            ('RenderbufferStorage', None, UINT, UINT, INT, INT),
            ('FramebufferRenderbuffer', None, UINT, UINT, UINT, UINT),
            ('ClipControl', None, UINT, UINT), ('Viewport', None, INT, INT, INT, INT),
            ('DepthRange', None, ct.c_double, ct.c_double),
            ('Enable', None, UINT), ('Disable', None, UINT), ('DepthMask', None, ct.c_ubyte),
            ('DepthFunc', None, UINT), ('ClearDepth', None, ct.c_double),
            ('ClearColor', None, FLOAT, FLOAT, FLOAT, FLOAT), ('Clear', None, UINT),
            ('FrontFace', None, UINT), ('BlendFunc', None, UINT, UINT),
            ('DrawElements', None, UINT, INT, UINT, PTR),
            ('ReadPixels', None, INT, INT, INT, INT, UINT, UINT, PTR),
        ):
            setattr(self, name, api(lib, 'gl' + name, result, *types))

    def generate(self, generator):
        result = UINT()
        generator(1, ct.byref(result))
        return result.value

    def require_status(self, resource, query, status, log):
        result = INT()
        query(resource, status, ct.byref(result))
        if not result.value:
            query(resource, 0x8B84, ct.byref(result))
            buffer = ct.create_string_buffer(max(1, result.value))
            log(resource, len(buffer), None, buffer)
            raise ValueError(buffer.value.decode(errors='replace'))

    def program(self, vertex, fragment):
        program = self.CreateProgram()
        for kind, source in ((0x8B31, vertex), (0x8B30, fragment)):
            shader = self.CreateShader(kind)
            strings = (ct.c_char_p * 1)(source.encode())
            self.ShaderSource(shader, 1, strings, None)
            self.CompileShader(shader)
            self.require_status(shader, self.GetShaderiv, 0x8B81, self.GetShaderInfoLog)
            self.AttachShader(program, shader)
            self.DeleteShader(shader)
        self.LinkProgram(program)
        self.require_status(program, self.GetProgramiv, 0x8B82, self.GetProgramInfoLog)
        return program

    def upload(self, target, buffer, data):
        self.BindBuffer(target, buffer)
        self.BufferData(target, data.nbytes, data.ctypes.data, 0x88E0)

    def setting(self, capability, enabled):
        (self.Enable if enabled else self.Disable)(capability)


FIXED_VERTEX = """#version 450
layout(location = 0) in vec4 position;
layout(location = 1) in vec4 diffuse;
layout(location = 2) in vec4 texcoord;
layout(location = 0) out vec4 color;
layout(location = 1) out vec2 uv;
void main() {
    gl_Position = position;
    color = diffuse;
    uv = texcoord.xy;
}
"""

FIXED_PIXEL = """#version 450
layout(location = 0) in vec4 color;
layout(location = 1) in vec2 uv;
layout(location = 0) out vec4 result0;
layout(binding = 0) uniform sampler2D texture_0;
layout(location = 2) uniform int modulate_rgb;
layout(location = 3) uniform int modulate_alpha;
void main() {
    result0 = texture(texture_0, uv);
    if (modulate_rgb == 1) result0.rgb *= color.rgb;
    if (modulate_alpha == 1) result0.a *= color.a;
}
"""

FIXED_VERTEX_2UV = (FIXED_VERTEX.replace('void main() {',
    'layout(location = 3) in vec4 texcoord1;\nlayout(location = 2) out vec2 uv1;\nvoid main() {')
    .replace('    uv = texcoord.xy;', '    uv = texcoord.xy;\n    uv1 = texcoord1.xy;'))

ALPHA_TEST = """
layout(location = 0) uniform int alpha_test;
layout(location = 1) uniform int alpha_ref;
void main() {
    d3d_pixel();
    int alpha = int(floor(clamp(result0.a, 0.0, 1.0) * 255.0 + 0.5));
    if (alpha_test == 1 ||
        (alpha_test == 2 && alpha >= alpha_ref) ||
        (alpha_test == 3 && alpha != alpha_ref) ||
        (alpha_test == 4 && alpha > alpha_ref) ||
        (alpha_test == 5 && alpha <= alpha_ref) ||
        (alpha_test == 6 && alpha == alpha_ref) ||
        (alpha_test == 7 && alpha < alpha_ref)) discard;
}
"""


def argb(value):
    return np.array([(value >> shift & 255) / 255 for shift in (16, 8, 0, 24)])


def vertex_field(raw, count, stride, offset, width, dtype='<f4'):
    return np.ndarray((count, width), dtype, raw, offset, (stride, np.dtype(dtype).itemsize))


def normalized(rows):
    length = np.linalg.norm(rows, axis=-1, keepdims=True)
    return rows / np.where(length == 0, 1.0, length)


def fixed_attributes(draw, raw, second_texture=False):
    fvf = draw['fvf']
    if fvf not in (0x102, 0x112, 0x142, 0x144, 0x152):
        raise ValueError(f'unsupported fixed vertex format {fvf:#x}')
    _, _, _, count, _, _ = draw['args']
    stride = draw['stride']
    matrices = {int(id): np.asarray(value).reshape((4, 4)) for id, value in draw['matrices'].items()}
    local = np.ones((count, 4))
    local[:, :3] = vertex_field(raw, count, stride, 0, 3)
    world = matrices[256]
    position = local @ world
    camera = position @ matrices[2]
    screen_space = (fvf & 0xe) == 4
    if screen_space:
        x, y, w, h, near, far = draw['viewport']
        rhw = vertex_field(raw, count, stride, 12, 1)[:, 0]
        clip = np.column_stack(((local[:, 0] + 0.5 - x) * 2 / w - 1,
                                1 - (local[:, 1] + 0.5 - y) * 2 / h,
                                (local[:, 2] - near) / (far - near), local[:, 3]))
        clip /= rhw[:, None]
    else:
        clip = camera @ matrices[3]

    source_uv = vertex_field(raw, count, stride, stride - 8, 2)

    def stage_uv(stage):
        transform = draw['stages'].get(f'{stage}:24', 0)
        input_set = draw['stages'].get(f'{stage}:11', 0)
        uv = np.zeros((count, 4))
        if input_set == 0:
            uv[:, :2] = source_uv
            uv[:, 2] = 1
        elif input_set == 0x20000 and not screen_space:
            uv = camera
        elif input_set == 0x30000 and fvf & 0x10:
            view_world = world @ matrices[2]
            normals = vertex_field(raw, count, stride, 12, 3)
            normals = normalized(normals @ np.linalg.inv(view_world[:3, :3]).T)
            incidence = normalized(camera[:, :3])
            uv[:, :3] = incidence - 2 * np.sum(incidence * normals, axis=1, keepdims=True) * normals
            uv[:, 3] = 1
        else:
            raise ValueError(f'unsupported texture coordinate source {input_set:#x}')
        if transform == 2:
            return uv @ matrices[16 + stage]
        if transform != 0:
            raise ValueError(f'unsupported texture transform {transform}')
        return uv

    diffuse = np.ones((count, 4))
    if fvf & 0x40:
        color_offset = (16 if screen_space else 12) + (12 if fvf & 0x10 else 0)
        diffuse = vertex_field(raw, count, stride, color_offset, 4, 'u1')[:, [2, 1, 0, 3]] / 255
    state = draw['render']
    if state['137'] and not screen_space:
        material = np.asarray(draw['material'])
        ambient_source = diffuse[:, :3] if state['147'] == 1 and fvf & 0x40 else material[4:7]
        diffuse_source = diffuse if state['145'] == 1 and fvf & 0x40 else np.tile(material[:4], (count, 1))
        emissive = diffuse[:, :3] if state['148'] == 1 and fvf & 0x40 else material[12:15]
        light_color = np.zeros((count, 3))
        light_ambient = np.tile(argb(state['139'])[:3], (count, 1))
        normal = np.zeros((count, 3))
        if fvf & 0x10:
            normal = vertex_field(raw, count, stride, 12, 3) @ np.linalg.inv(world[:3, :3]).T
        if state['143']:
            normal = normalized(normal)
        for raw_light in draw['lights'].values():
            light = np.asarray(raw_light)
            if light[0] == 3:
                attenuation = 1.0
                direction = normalized(-light[16:19])
            elif light[0] == 1:
                to_light = light[13:16] - position[:, :3]
                distance = np.linalg.norm(to_light, axis=-1, keepdims=True)
                denominator = light[21] + light[22] * distance + light[23] * distance ** 2
                attenuation = np.where(distance <= light[19], 1.0 / np.maximum(denominator, 1e-10), 0)
                direction = normalized(to_light)
            else:
                raise ValueError(f'unsupported light type {light[0]}')
            intensity = np.maximum(np.sum(normal * direction, axis=-1, keepdims=True), 0)
            light_color += attenuation * intensity * light[1:4]
            light_ambient += attenuation * light[9:12]
        diffuse = np.column_stack((emissive + ambient_source * light_ambient +
                                   diffuse_source[:, :3] * light_color, diffuse_source[:, 3]))
        diffuse = np.clip(diffuse, 0, 1)
    attributes = {0: clip, 1: diffuse, 2: stage_uv(0)}
    if second_texture:
        attributes[3] = stage_uv(1)
    return attributes


def programmable_attributes(draw, raw, shader, declarations):
    declaration = {(usage, index): (stream, offset, type_index, method)
                   for stream, offset, type_index, method, usage, index in declarations}
    _, _, _, count, _, _ = draw['args']
    attributes = {}
    for token, words in commands(shader):
        if token & 65535 != 31:
            continue
        stream, offset, type_index, method = declaration[(words[0] & 15, words[0] >> 16 & 15)]
        if stream or method or type_index not in range(5):
            raise ValueError(f'unsupported vertex declaration {stream, offset, type_index, method}')
        if type_index == 4:
            data = vertex_field(raw, count, draw['stride'], offset, 4, 'u1')[:, [2, 1, 0, 3]] / 255
        else:
            data = vertex_field(raw, count, draw['stride'], offset, type_index + 1)
        value = np.zeros((count, 4))
        value[:, 3] = 1
        value[:, :data.shape[1]] = data
        attributes[register_index(words[1])] = value
    return attributes


def stage_modulates(stages, operation):
    op = stages[f'0:{operation}']
    args = (stages[f'0:{operation + 1}'], stages[f'0:{operation + 2}'])
    if op == 2 and args[0] == 2:
        return 0
    if op == 4 and args == (2, 0):
        return 1
    raise ValueError(f'unsupported fixed texture stage {op, args}')


def fixed_gl_fragment(draw):
    stages = draw['stages']
    source = ['#version 450', 'layout(location = 0) in vec4 color;',
              'layout(location = 1) in vec2 uv;', 'layout(location = 2) in vec2 uv1;',
              'layout(location = 0) out vec4 result0;']
    body = ['void d3d_pixel() {']
    current = 'color'
    for stage, coordinates in ((0, 'uv'), (1, 'uv1')):
        if stages[f'{stage}:1'] == 1:
            break
        texture_name = f'texel{stage}'
        operands = {0: 'color', 1: current, 2: texture_name}
        expressions = []
        for index, channels in ((1, 'rgb'), (4, 'a')):
            operation = stages[f'{stage}:{index}']
            arg1 = stages[f'{stage}:{index + 1}']
            arg2 = stages[f'{stage}:{index + 2}']
            if operation not in (2, 3, 4):
                raise ValueError(f'unsupported fixed texture stage {stage}: {operation}')
            a, b = (f'{operands[arg]}.{channels}' for arg in (arg1, arg2))
            expressions.append({2: a, 3: b, 4: f'{a} * {b}'}[operation])
        if any(texture_name in expression for expression in expressions):
            if str(stage) not in draw['slots']:
                raise ValueError(f'texture missing for fixed stage {stage}')
            source.append(f'layout(binding = {stage}) uniform sampler2D source{stage};')
            body.append(f'    vec4 {texture_name} = texture(source{stage}, {coordinates});')
        current = f'stage{stage}'
        body.append(f'    vec4 {current} = clamp(vec4({", ".join(expressions)}), 0.0, 1.0);')
    body += [f'    result0 = {current};', '}']
    return '\n'.join(source + body) + ALPHA_TEST


def texture(gl, root, info):
    name = gl.generate(gl.GenTextures)
    gl.BindTexture(0x0DE1, name)
    width, height, _, usage, fmt, _ = info['shape']
    levels = info['levels']
    if not levels:
        if usage & 1 and fmt in (21, 22):
            gl.TexImage2D(0x0DE1, 0, 0x8058, width, height, 0, 0x1908, 0x1401, None)
            gl.TexParameteri(0x0DE1, 0x813D, 0)
            return name
        raise ValueError('texture capture contains no data or does not name a color target')
    for i, level in enumerate(levels):
        w, h = max(1, width >> i), max(1, height >> i)
        source = (root / level).read_bytes()
        if fmt in (21, 22):
            image = Image.frombytes('RGBA', (w, h), source, 'raw', 'BGRA')
            if fmt == 22:
                image.putalpha(255)
        else:
            kind = {0x31545844: (1, 'DXT1'), 0x33545844: (2, 'DXT3'), 0x35545844: (3, 'DXT5')}[fmt]
            image = Image.frombytes('RGBA', (w, h), source, 'bcn', kind)
        pixels = np.asarray(image)
        gl.TexImage2D(0x0DE1, i, 0x8058, w, h, 0, 0x1908, 0x1401, pixels.ctypes.data)
    gl.TexParameteri(0x0DE1, 0x813D, len(levels) - 1)
    return name


def render_target(gl, color, width, height):
    framebuffer = gl.generate(gl.GenFramebuffers)
    gl.BindFramebuffer(0x8D40, framebuffer)
    gl.FramebufferTexture2D(0x8D40, 0x8CE0, 0x0DE1, color, 0)
    depth = gl.generate(gl.GenRenderbuffers)
    gl.BindRenderbuffer(0x8D41, depth)
    gl.RenderbufferStorage(0x8D41, 0x81A7, width, height)
    gl.FramebufferRenderbuffer(0x8D40, 0x8D00, 0x8D41, depth)
    if gl.CheckFramebufferStatus(0x8D40) != 0x8CD5:
        raise RuntimeError('incomplete render target for the Direct3D scene')
    return framebuffer


def draw_state(gl, draw):
    state = draw['render']
    gl.setting(0x0B71, state['7'])
    gl.DepthMask(bool(state['14']))
    gl.DepthFunc(0x1FF + state['23'])
    gl.setting(0x0B44, state['22'] != 1)
    gl.FrontFace({1: 0x0901, 2: 0x0901, 3: 0x0900}[state['22']])
    gl.setting(0x0BE2, state['27'])
    blend = {1: 0, 2: 1, 3: 0x0300, 4: 0x0301, 5: 0x0302, 6: 0x0303,
             7: 0x0304, 8: 0x0305, 9: 0x0306, 10: 0x0307, 11: 0x0308}
    gl.BlendFunc(blend[state['19']], blend[state['20']])
    gl.Uniform1i(0, state['25'] if state['15'] else 8)
    gl.Uniform1i(1, state['24'] & 255)


def replay(path, output):
    scene = json.loads(path.read_text())
    root = path.parent
    width, height = scene.get('backbuffer', scene['draws'][0]['viewport'][2:4])
    context = open_context(width, height)
    gl = GL()
    gl.ClipControl(0x8CA1, 0x935F)
    gl.BindVertexArray(gl.generate(gl.GenVertexArrays))
    buffers = [gl.generate(gl.GenBuffers) for _ in range(4)]
    fixed_programs = {}
    shaders = {}
    for id, file in scene['programs'].items():
        data = (root / file).read_bytes()
        tokens = struct.unpack(f'<{len(data) // 4}I', data)
        kind = {0xFFFE: 'v', 0xFFFF: 'p'}[tokens[0] >> 16]
        shaders[int(id)] = Shader(kind, tokens[0] >> 8 & 255, tokens[0] & 255, tokens)
    programs = {}
    textures = {int(id): texture(gl, root, info) for id, info in scene['texture_resources'].items()}
    targets = {0: (0, width, height)}
    for id in {draw.get('target', 0) for draw in scene['draws']} - {0}:
        w, h = scene['texture_resources'][str(id)]['shape'][:2]
        targets[id] = (render_target(gl, textures[id], w, h), w, h)

    def bind_target(id):
        framebuffer, w, h = targets[id]
        gl.BindFramebuffer(0x8D40, framebuffer)
        gl.ClipControl(0x8CA2 if id else 0x8CA1, 0x935F)
        return w, h

    clear_events = iter(zip(scene['clears'], scene.get('clear_targets', [0] * len(scene['clears']))))
    clear = next(clear_events, None)
    for i, draw in enumerate(scene['draws']):
        while clear and clear[0][0] == i:
            target_width, target_height = bind_target(clear[1])
            rects, flags, color, raw_depth, stencil = clear[0][1]
            if any(rect != [0, 0, target_width, target_height] for rect in rects) or flags & ~3:
                raise ValueError('partial viewport or stencil clear is unsupported')
            gl.DepthMask(True)
            gl.ClearColor(*argb(color))
            gl.ClearDepth(struct.unpack('<f', struct.pack('<I', raw_depth))[0])
            gl.Clear((0x4000 if flags & 1 else 0) | (0x100 if flags & 2 else 0))
            clear = next(clear_events, None)

        target = draw.get('target', 0)
        _, target_height = bind_target(target)
        pair = draw['vs'], draw['ps']
        raw = (root / draw['vertex_file']).read_bytes()
        if pair == (0, 0):
            fragment = fixed_gl_fragment(draw)
            if fragment not in fixed_programs:
                fixed_programs[fragment] = gl.program(FIXED_VERTEX_2UV, fragment)
            gl.UseProgram(fixed_programs[fragment])
            attributes = fixed_attributes(draw, raw, second_texture=draw['stages']['1:1'] != 1)
        else:
            vs, ps = (shaders[id] for id in pair)
            if pair not in programs:
                vertex = translate(vs).replace('#version 460', '#version 450')
                fragment = translate(ps).replace('#version 460', '#version 450')
                fragment = fragment.replace('binding = 0) uniform Registers', 'binding = 1) uniform PixelRegisters')
                fragment = fragment.replace('constants[', 'pixel_constants[')
                fragment = fragment.replace('void main()', 'void d3d_pixel()') + ALPHA_TEST
                programs[pair] = gl.program(vertex, fragment)
            gl.UseProgram(programs[pair])
            decl = scene['declarations'][str(draw['decl'])]
            attributes = programmable_attributes(draw, raw, vs, decl)
            for slot, count, field in ((0, 256, 'v_constants'), (1, 32, 'p_constants')):
                data = np.zeros((count, 4), np.float32)
                for index, value in draw[field].items():
                    if int(index) < count:
                        data[int(index)] = value
                gl.upload(0x8A11, buffers[slot + 2], data)
                gl.BindBufferBase(0x8A11, slot, buffers[slot + 2])

        x, y, w, h, near, far = draw['viewport']
        gl.Viewport(x, y if target else target_height - y - h, w, h)
        gl.DepthRange(near, far)
        draw_state(gl, draw)
        for i_slot in range(16):
            slot = str(i_slot)
            gl.ActiveTexture(0x84C0 + i_slot)
            gl.BindTexture(0x0DE1, textures.get(draw['slots'].get(slot), 0))
            if slot not in draw['slots']:
                continue
            states = draw['samplers']
            wrap = {1: 0x2901, 2: 0x8370, 3: 0x812F}
            gl.TexParameteri(0x0DE1, 0x2802, wrap[states[f'{slot}:1']])
            gl.TexParameteri(0x0DE1, 0x2803, wrap[states[f'{slot}:2']])
            filt = (states[f'{slot}:6'], states[f'{slot}:7'])
            minfilter = {(1, 0): 0x2600, (2, 0): 0x2601, (1, 1): 0x2700,
                         (2, 1): 0x2701, (1, 2): 0x2702, (2, 2): 0x2703}
            gl.TexParameteri(0x0DE1, 0x2801, minfilter[filt])
            gl.TexParameteri(0x0DE1, 0x2800, {1: 0x2600, 2: 0x2601}[states[f'{slot}:5']])

        topology, _, min_vertex, count, _, _ = draw['args']
        vertices = np.zeros((count, 16, 4), np.float32)
        for location in range(16):
            if location in attributes:
                vertices[:, location, :] = attributes[location]
                gl.EnableVertexAttribArray(location)
            else:
                gl.DisableVertexAttribArray(location)
        gl.upload(0x8892, buffers[0], vertices)
        for location in attributes:
            gl.VertexAttribPointer(location, 4, 0x1406, False, 16 * 16, location * 16)
        index_file = (root / draw['index_file']).read_bytes()
        indices = np.frombuffer(index_file, '<u2' if draw['index_size'] == 2 else '<u4').astype(np.uint32)
        indices -= min_vertex
        if np.any(indices >= count):
            raise ValueError(f'draw {i} has an index outside the captured vertex range')
        gl.upload(0x8893, buffers[1], indices)
        gl.DrawElements({4: 4, 5: 5, 6: 6}[topology], len(indices), 0x1405, None)
        error = gl.GetError()
        if error:
            raise RuntimeError(f'OpenGL error {error:#x} at draw {i}')
    if clear:
        raise ValueError('clear after last draw is unsupported')

    pixels = np.zeros((height, width, 4), np.uint8)
    bind_target(0)
    gl.ReadPixels(0, 0, width, height, 0x1908, 0x1401, pixels.ctypes.data)
    Image.fromarray(pixels[::-1, :, :3]).save(output)
    print(f'{len(scene["draws"])} draws, {len(programs)} shader pairs, GL renderer '
          f'{gl.GetString(0x1F01).decode()}')
    egl, display, surface, egl_context = context
    api(egl, 'eglMakeCurrent', UINT, PTR, PTR, PTR, PTR)(display, None, None, None)
    api(egl, 'eglDestroyContext', UINT, PTR, PTR)(display, egl_context)
    api(egl, 'eglDestroySurface', UINT, PTR, PTR)(display, surface)
    api(egl, 'eglTerminate', UINT, PTR)(display)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('scene', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    replay(args.scene, args.output)


if __name__ == '__main__':
    main()
