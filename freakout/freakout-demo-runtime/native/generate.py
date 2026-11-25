"""Generate portable startup blocks and the demo's PE import map."""
import argparse
import hashlib
import json
from pathlib import Path
import sys

HOME = Path(__file__).resolve().parent
sys.path.insert(0, str(HOME.parent))

from lift_boot import emit
from trace_boot import Boot, load_demo
from d3d_probe import ROOT_METHODS, DEVICE_METHODS, SURFACE_METHODS, TEXTURE_METHODS
from input_probe import ROOT_METHODS as INPUT_ROOT, DEVICE_METHODS as INPUT_DEVICE
from video_probe import CONTROL, EVENTS, GRAPH, POSITION, WINDOW

INTERFACES = (
    ('d3d_root', 'IDirect3D9', ROOT_METHODS),
    ('d3d_device', 'IDirect3DDevice9', DEVICE_METHODS),
    ('d3d_surface', 'IDirect3DSurface9', SURFACE_METHODS),
    ('d3d_texture', 'IDirect3DTexture9', TEXTURE_METHODS),
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
        f'static const uint32_t guest_qsort = 0x{boot.crt_guest["qsort"]:x}u;\n', encoding='ascii')
    (out / 'interfaces.inc').write_text('\n'.join(extras) + '\n', encoding='ascii')


def main():
    args = argparse.ArgumentParser(description=__doc__)
    args.add_argument('exe', type=Path)
    args.add_argument('pcrecomp', type=Path)
    args.add_argument('assets', type=Path)
    args.add_argument('--out', type=Path, default=HOME / 'generated')
    options = args.parse_args()
    options.out.mkdir(parents=True, exist_ok=True)
    payload = load_demo(options.exe)
    boot = Boot(*payload, stop_at_winmain=False, assets=options.assets.resolve(), probe_audio=True,
                present_limit=12, compressed_upload_limit=3, instruction_limit=88_000_000)
    generate_header(boot, payload, options.out)
    result = boot.run()
    print(f'x86 reference: {result["status"]}, {len(boot.api_sequence)} imports, {boot.file_bytes} bytes read',
          flush=True)
    if len(boot.d3d.uploads) != 3:
        raise RuntimeError(f'x86 startup did not upload the texture: {result["status"]}')
    reference = {'presents': boot.d3d.present_count, 'imports': len(boot.api_sequence),
                 'api_counts': dict(boot.api_calls), 'bytes_read': boot.file_bytes,
                 'archive_reads': boot.file_reads, 'uploads': boot.d3d.uploads,
                 'clear': [event for event in boot.d3d.events if event[0] == 'Clear']}
    (options.out / 'reference.json').write_text(json.dumps(reference, indent=2) + '\n')
    emit(boot, options.pcrecomp.resolve(), options.out, standalone=False)


if __name__ == '__main__':
    main()
