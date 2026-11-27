"""Compile the live rasterizer's built-in shaders and the demo's DX9 fragment programs."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from dx9_asm import assemble
from dx9_glsl import commands, register_type, translate


def passthrough():
    lines = ['#version 460', 'layout(location=0) in vec4 position;']
    for i in range(10):
        lines.append(f'layout(location={i + 1}) in vec4 value{i};')
        lines.append(f'layout(location={i}) out vec4 varying{i};')
    lines += ['void main() {', '    gl_Position = position;']
    lines += [f'    varying{i} = value{i};' for i in range(10)]
    return '\n'.join((*lines, '}'))


def fixed(textured):
    return '\n'.join(('#version 460', 'layout(location=0) in vec4 tint;',
                     'layout(location=2) in vec4 uv;', 'layout(location=0) out vec4 color;',
                     'layout(binding=0) uniform sampler2D image;' if textured else '',
                     'void main() { color = ' + ('texture(image, uv.xy) * tint;' if textured else 'tint;') + ' }'))


def compile_program(uam, temporary, stage, code):
    source, output = temporary / 'shader.glsl', temporary / 'shader.dksh'
    source.write_text(code)
    subprocess.run([uam, '-s', stage, '-o', str(output), str(source)], check=True)
    blob = output.read_bytes()
    if not blob.startswith(b'DKSH'):
        raise ValueError('uam produced an invalid shader file')
    return blob


def generate(assets, output, uam):
    shader_dir = assets / 'Data/Common/Shaders/DX9'
    shaders = [path for path in sorted(shader_dir.iterdir()) if path.suffix.lower() in ('.vsh', '.psh')]
    programs = [(-3, 'vert', passthrough()), (-2, 'frag', fixed(False)), (-1, 'frag', fixed(True))]
    for i, path in enumerate(shaders):
        if path.suffix.lower() != '.psh':
            continue
        shader = assemble(path.read_text(encoding='latin1'))
        if any(token & 65535 == 31 and register_type(words[1]) == 10 and
               (words[0] >> 27 & 15) != 2 for token, words in commands(shader)):
            continue
        programs.append((i, 'frag', translate(shader)))
    declarations, catalog = [], []
    with tempfile.TemporaryDirectory(prefix='freakout-gpu-') as scratch:
        for i, (source, stage, code) in enumerate(programs):
            blob = compile_program(uam, Path(scratch), stage, code)
            rows = [', '.join(f'0x{byte:02x}' for byte in blob[at:at + 16]) for at in range(0, len(blob), 16)]
            data = ',\n    '.join(rows)
            declarations.append(f'static const uint8_t gpu_code_{i}[] = {{\n    {data}\n}};')
            catalog.append(f'    {{{source}, gpu_code_{i}, sizeof gpu_code_{i}}},')
    output.write_text('\n'.join((*declarations, 'static const GpuCode gpu_programs[] = {', *catalog, '};', '')))
    print(f'Compiled {len(programs) - 3} DX9 fragment programs and three built-in programs.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('assets', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--uam', default='uam')
    args = parser.parse_args()
    generate(args.assets, args.output, args.uam)
