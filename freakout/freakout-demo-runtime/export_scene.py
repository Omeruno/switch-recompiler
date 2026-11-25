"""Build a Switch GPU replay packet from a local scene capture and uam."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile

import numpy as np

from dx9_asm import Shader
from dx9_glsl import commands, register_index, register_type, translate
from render_scene import FIXED_PIXEL, FIXED_VERTEX, fixed_attributes, programmable_attributes, stage_modulates


def words(*values):
    return struct.pack(f'<{len(values)}I', *values)


class ShaderSet:
    def __init__(self, tool, temp):
        self.tool = tool
        self.temp = temp
        self.modules = []
        self.ids = {}

    def module(self, stage, source):
        key = stage, source
        if key not in self.ids:
            index = len(self.modules)
            shader = self.temp / f'{index}.glsl'
            binary = shader.with_suffix('.dksh')
            shader.write_text(source)
            subprocess.run([self.tool, '-s', stage, '-o', str(binary), str(shader)], check=True)
            self.modules.append((stage == 'frag', binary.read_bytes()))
            self.ids[key] = index
        return self.ids[key]


def texture_blob(root, texture):
    width, height, _, _, fmt, _ = texture['shape']
    levels = texture['levels']
    result = bytearray(words(width, height, 21 if fmt == 22 else fmt, len(levels)))
    if fmt not in (21, 22, 0x31545844, 0x35545844) or not levels:
        raise ValueError(f'unsupported texture format or mip chain {fmt:#x}')
    for name in levels:
        data = bytearray((root / name).read_bytes())
        size = width * height * 4 if fmt in (21, 22) else (
            ((width + 3) // 4) * ((height + 3) // 4) * (8 if fmt == 0x31545844 else 16))
        if len(data) != size:
            raise ValueError(f'invalid mip size for {name}')
        if fmt == 22:
            data[3::4] = bytes([255]) * (size // 4)
        result.extend(data)
        width, height = max(1, width // 2), max(1, height // 2)
    return result


def captured_shader(path):
    data = path.read_bytes()
    tokens = struct.unpack(f'<{len(data) // 4}I', data)
    kind = {0xFFFE: 'v', 0xFFFF: 'p'}[tokens[0] >> 16]
    return Shader(kind, tokens[0] >> 8 & 255, tokens[0] & 255, tokens)


def shader_slots(shader):
    slots = set()
    for token, operands in commands(shader):
        opcode = token & 65535
        if shader.major == 1 and opcode == 66:
            slots.add(register_index(operands[0]))
        if opcode == 31 and register_type(operands[1]) == 10:
            if (operands[0] >> 27 & 15) != 2:
                raise ValueError('only 2D samplers are supported')
            slots.add(register_index(operands[1]))
    return slots


def fixed_fragment(draw):
    if draw['stages']['1:1'] != 1:
        raise ValueError('more than one active fixed-function stage')
    rgb = stage_modulates(draw['stages'], 1)
    alpha = stage_modulates(draw['stages'], 4)
    return (FIXED_PIXEL.replace('#version 450', '#version 460')
            .replace('layout(location = 2) uniform int modulate_rgb;', f'const int modulate_rgb = {rgb};')
            .replace('layout(location = 3) uniform int modulate_alpha;', f'const int modulate_alpha = {alpha};'))


def sampler_blob(draw, textures, used_slots):
    if not used_slots <= {0, 1, 2, 3}:
        raise ValueError(f'unsupported shader samplers {used_slots}')
    result = bytearray()
    for i in range(4):
        if i not in used_slots:
            result.extend(words(0xFFFFFFFF, 0))
            continue
        texture = textures[str(draw['slots'][str(i)])]
        states = draw['samplers']
        u, v, mag, minimum, mip = (states[f'{i}:{state}'] for state in (1, 2, 5, 6, 7))
        if u not in (1, 2, 3) or v not in (1, 2, 3) or mag not in (1, 2) or minimum not in (1, 2) or mip not in (0, 1, 2):
            raise ValueError(f'unsupported sampler state {u, v, mag, minimum, mip}')
        result.extend(words(texture, (u - 1) | (v - 1) << 2 | (mag - 1) << 4 | (minimum - 1) << 5 | mip << 6))
    return result


def constants(draw):
    chunks = []
    for limit, name in ((256, 'v_constants'), (32, 'p_constants')):
        registers = np.zeros((limit, 4), dtype='<f4')
        for index, value in draw[name].items():
            if not 0 <= int(index) < limit:
                raise ValueError(f'{name} register {index} out of range')
            registers[int(index)] = value
        if not np.isfinite(registers).all():
            raise ValueError(f'nonfinite {name}')
        chunks.append(registers.tobytes())
    return b''.join(chunks)


def draw_blob(draw, root, shaders, declarations, textures, modules):
    state = draw['render']
    topology, _, min_vertex, count, _, primitives = draw['args']
    raw = (root / draw['vertex_file']).read_bytes()
    if topology not in (4, 5, 6) or draw['index_size'] not in (2, 4):
        raise ValueError(f'unsupported topology or index format at {draw["vertex_file"]}')
    if (state['28'], state['52'], state['168'], state['171'], state['194'], state['206']) != (0, 0, 15, 1, 0, 0):
        raise ValueError('unsupported fog, stencil, color mask, blend equation, sRGB or separate alpha state')
    if state['7'] not in (0, 1):
        raise ValueError('unsupported depth-buffer mode')
    prog = bool(draw['vs'] or draw['ps'])
    if prog:
        vs, ps = shaders[draw['vs']], shaders[draw['ps']]
        decl = declarations[str(draw['decl'])]
        attributes = programmable_attributes(draw, raw, vs, decl)
        sources = translate(vs), translate(ps)
        slots = shader_slots(ps)
    else:
        attributes = fixed_attributes(draw, raw)
        sources = FIXED_VERTEX.replace('#version 450', '#version 460'), fixed_fragment(draw)
        slots = {0}
    v_id, p_id = (modules.module(stage, source) for stage, source in zip(('vert', 'frag'), sources))
    mask = sum(1 << location for location in attributes)
    vertex_data = np.stack([attributes[location] for location in sorted(attributes)], axis=1).astype('<f4')
    if not attributes or not np.isfinite(vertex_data).all():
        raise ValueError('invalid vertex attributes')
    index_data = (root / draw['index_file']).read_bytes()
    indices = np.frombuffer(index_data, '<u2' if draw['index_size'] == 2 else '<u4').astype(np.int64)
    indices -= min_vertex
    index_count = primitives * 3 if topology == 4 else primitives + 2
    if len(indices) != index_count or np.any(indices < 0) or np.any(indices >= min(count, 65536)):
        raise ValueError(f'{draw["index_file"]} contains invalid indices')
    flags = int(prog) | state['7'] << 1 | bool(state['14']) << 2 | bool(state['27']) << 3
    alpha_compare = state['25'] if state['15'] else 8
    header = words(v_id, p_id, topology, count, index_count, mask, flags, state['23'], state['22'],
                   state['19'], state['20'], alpha_compare, state['24'] & 255)
    viewport = struct.pack('<4I2f', *draw['viewport'])
    return (header + viewport + sampler_blob(draw, textures, slots) + (constants(draw) if prog else b'') +
            vertex_data.tobytes() + indices.astype('<u2').tobytes()), (v_id, p_id)


def export(scene_path, output, uam):
    scene = json.loads(scene_path.read_text())
    if any(draw.get('target', 0) for draw in scene['draws']) or any(scene.get('clear_targets', ())):
        raise ValueError('FOS3 v1 supports one render target. Render this capture with render_scene.py.')
    root = scene_path.parent
    resources = scene['texture_resources']
    textures = {id: i for i, id in enumerate(resources)}
    shaders = {int(id): captured_shader(root / file) for id, file in scene['programs'].items()}
    texture_data = bytearray()
    for info in resources.values():
        texture_data.extend(texture_blob(root, info))
    clear_data = bytearray()
    for at, (rects, flags, color, depth, stencil) in scene['clears']:
        if any(rect != [0, 0, 1024, 768] for rect in rects) or stencil or flags & ~3:
            raise ValueError('only fullscreen color and depth clears are supported')
        clear_data.extend(words(at, flags, color, depth))
    draws = scene['draws']
    draw_data = bytearray()
    pairs = Counter()
    with tempfile.TemporaryDirectory() as temp:
        modules = ShaderSet(uam, Path(temp))
        for draw in draws:
            blob, pair = draw_blob(draw, root, shaders, scene['declarations'], textures, modules)
            pairs[pair] += 1
            draw_data.extend(blob)
    module_data = bytearray()
    for stage, data in modules.modules:
        module_data.extend(words(stage, len(data)))
        module_data.extend(data)
    payload = texture_data + module_data + clear_data + draw_data
    header = b'FOS3' + words(1, len(textures), len(modules.modules), len(draws), len(scene['clears']))
    output.write_bytes(header + hashlib.sha256(payload).digest() + payload)
    print(f'{len(draws)} draws, {len(pairs)} pipelines, {len(textures)} textures, {len(modules.modules)} DKSH modules')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('scene', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--uam', default='uam')
    args = parser.parse_args()
    export(args.scene, args.output, args.uam)


if __name__ == '__main__':
    main()
