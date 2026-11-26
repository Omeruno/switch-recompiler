"""Generate native blocks, import bindings and DX9 shader bytecode for the demo."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import tempfile

HOME = Path(__file__).resolve().parent
sys.path.insert(0, str(HOME.parent))

from lift_boot import emit
from trace_boot import Boot, IMAGE_BASE, load_demo
from d3d_probe import ROOT_METHODS, DEVICE_METHODS, SURFACE_METHODS, TEXTURE_METHODS, BUFFER_METHODS
from input_probe import ROOT_METHODS as INPUT_ROOT, DEVICE_METHODS as INPUT_DEVICE
from video_probe import CONTROL, EVENTS, GRAPH, POSITION, WINDOW
from dx9_asm import assemble
from menu_audio_trace import MenuAudioTrace
from scene_demo import MenuInput, _DESCENT_KEYS
from unicorn.x86_const import UC_X86_REG_EIP

INTERFACES = (
    ('d3d_root', 'IDirect3D9', ROOT_METHODS),
    ('d3d_device', 'IDirect3DDevice9', DEVICE_METHODS),
    ('d3d_surface', 'IDirect3DSurface9', SURFACE_METHODS),
    ('d3d_texture', 'IDirect3DTexture9', TEXTURE_METHODS),
    ('d3d_vertex_buffer', 'IDirect3DVertexBuffer9', BUFFER_METHODS),
    ('d3d_index_buffer', 'IDirect3DIndexBuffer9', BUFFER_METHODS),
    ('di_root', 'IDirectInput8', INPUT_ROOT),
    ('di_device', 'IDirectInputDevice8', INPUT_DEVICE),
    ('video_graph', 'IGraphBuilder', GRAPH),
    ('video_events', 'IMediaEventEx', EVENTS),
    ('video_control', 'IMediaControl', CONTROL),
    ('video_position', 'IMediaPosition', POSITION),
    ('video_window', 'IVideoWindow', WINDOW),
)


def generate_header(boot, payload, out):
    rows = []
    for module in (boot.pe, boot.cpp):
        for group in module.DIRECTORY_ENTRY_IMPORT:
            for imp in group.imports:
                rows.append(f'    {{0x{imp.address:x}u, 0x{boot.get(imp.address):x}u}},')
    apis = [f'    {{{json.dumps(dll)}, {json.dumps(name)}}},' for dll, name in boot.apis.values()]
    data = '\n'.join((f'static const ImportBinding bindings[] = {{', *rows, '};',
                      'static const Import initial_imports[] = {', *apis, '};'))
    extras = []
    for key, dll, methods in INTERFACES:
        extras += [f'static const char *const {key}_names[] = {{',
                   *(f'    {json.dumps(item)},' for item in methods), '};',
                   f'const Methods {key} = {{"{dll}", {key}_names, sizeof {key}_names / sizeof *{key}_names}};']
    ctype = boot.crt.get_data(0x3a4d8, 512)
    lines = [f'static const uint8_t ctype_table[] = {{{", ".join(map(str, ctype))}}};']
    modules = (('FreakOut.exe', boot.pe), ('msvcp71.dll', boot.cpp), ('msvcr71.dll', boot.crt))
    for (name, module), raw in zip(modules, payload):
        digest = ', '.join(str(value) for value in hashlib.sha256(raw).digest())
        lines.append(f'    {{"{name}", 0x{module.OPTIONAL_HEADER.ImageBase:x}u, {len(raw)}u, {{{digest}}}}},')
    (out / 'game_data.inc').write_text(data + '\n' + lines[0] + '\nstatic const Module modules[] = {\n'
                                      + '\n'.join(lines[1:]) + '\n};\n', encoding='ascii')
    (out / 'exports.inc').write_text(
        f'static const uint32_t guest_qsort = 0x{boot.crt_guest["qsort"]:x}u;\n'
        f'static const uint32_t guest_setjmp3 = 0x{boot.crt_guest["_setjmp3"]:x}u;\n', encoding='ascii')
    (out / 'interfaces.inc').write_text('\n'.join(extras) + '\n', encoding='ascii')


def generate_shaders(assets, out):
    directory = assets / 'Data' / 'Common' / 'Shaders' / 'DX9'
    declarations, catalog = [], []
    for path in sorted(directory.iterdir()):
        if path.suffix.lower() not in ('.vsh', '.psh'):
            continue
        raw = path.read_bytes()
        label = f'shader_{len(catalog)}'
        shader = assemble(raw.decode('latin1'))
        tokens = ',\n    '.join(', '.join(f'0x{value:08x}u' for value in shader.tokens[i:i + 8])
                                for i in range(0, len(shader.tokens), 8))
        declarations.append(f'static const uint32_t {label}[] = {{\n    {tokens},\n}};')
        digest = ', '.join(str(value) for value in hashlib.sha256(raw).digest())
        catalog.append(f'    {{{json.dumps(path.relative_to(assets).as_posix())}, {label}, '
                       f'sizeof {label}, {len(raw)}, {{{digest}}}}},')
    if not catalog:
        raise ValueError(f'no D3D9 assembly shader sources found in {directory}')
    (out / 'shaders.inc').write_text('\n'.join((*declarations,
                                               'static const ShaderSource sources[] = {',
                                               *catalog, '};')) + '\n', encoding='ascii')


def trace_menu(boot, race):
    if race:
        boot.d3d.vtables = {}
        MenuInput(boot, MenuAudioTrace(boot), _DESCENT_KEYS)
    else:
        MenuInput(boot, MenuAudioTrace(boot))
    def retain_recent_events(probe, _present, _draws):
        for events in (probe.events, boot.audio.events):
            if len(events) > 30000:
                del events[:-4096]

    boot.d3d.frame_sink = retain_recent_events
    address = IMAGE_BASE + boot.pe.OPTIONAL_HEADER.AddressOfEntryPoint
    try:
        for _ in range(16 if race else 10):
            boot.status = 'instruction limit'
            boot.cpu.emu_start(address, 0, count=900_000_000)
            if boot.status != 'instruction limit':
                break
            address = boot.cpu.reg_read(UC_X86_REG_EIP)
    finally:
        boot.close_files()


def main():
    args = argparse.ArgumentParser(description=__doc__)
    args.add_argument('exe', type=Path)
    args.add_argument('pcrecomp', type=Path)
    args.add_argument('assets', type=Path)
    args.add_argument('--checkpoint', choices=('race', 'menu', 'draw', 'texture'), default='menu')
    args.add_argument('--out', type=Path, default=HOME / 'generated')
    options = args.parse_args()
    options.out.mkdir(parents=True, exist_ok=True)
    payload = load_demo(options.exe)
    draw = options.checkpoint == 'draw'
    race = options.checkpoint == 'race'
    menu = options.checkpoint == 'menu' or race
    assets = options.assets.resolve()
    with tempfile.TemporaryDirectory(prefix='freakout-trace-') as scratch:
        boot = Boot(*payload, stop_at_winmain=False, assets=assets, save_root=Path(scratch), probe_audio=True,
                    present_limit=1220 if race else 270 if menu else 256 if draw else 12,
                    compressed_upload_limit=0 if menu or draw else 3,
                    frame_after_draw=menu or draw, drawn_frame_limit=4096 if menu else 1,
                    draw_after_worker=menu,
                    guest_memory=0x20000000 if menu else 0x08000000,
                    trace_history=16384 if menu else None,
                    instruction_limit=900_000_000 if draw else 88_000_000)
        generate_header(boot, payload, options.out)
        generate_shaders(assets, options.out)
        if menu:
            trace_menu(boot, race)
        else:
            boot.run()
    call_count = sum(boot.api_calls.values())
    print(f'x86 reference: {boot.status}, {call_count} imports, {boot.file_bytes} bytes read',
          flush=True)
    stop = ('frame boundary:' if menu else 'drawn frame checkpoint:' if draw else 'texture checkpoint:')
    if not boot.status.startswith(stop):
        raise RuntimeError(f'x86 startup did not reach the checkpoint: {boot.status}')
    reference = {'status': boot.status, 'draws': boot.d3d.draw_count,
                 'threads': boot.thread.log, 'presents': boot.d3d.present_count, 'imports': call_count,
                 'api_counts': dict(boot.api_calls), 'bytes_read': boot.file_bytes,
                 'archive_reads': boot.file_reads, 'uploads': boot.d3d.uploads,
                 'd3d_api_counts': {key: value for key, value in boot.api_calls.items() if key.startswith('IDirect3D')},
                 'draw_events': [event for event in boot.d3d.events if event[0] in ('draw', 'geometry')],
                 'clear': [event for event in boot.d3d.events if event[0] == 'Clear']}
    (options.out / 'reference.json').write_text(json.dumps(reference, indent=2) + '\n')
    emit(boot, options.pcrecomp.resolve(), options.out, standalone=False, close_local_branches=menu,
         native_memory=0x0c000000 if menu else 0x08000000)


if __name__ == '__main__':
    main()
