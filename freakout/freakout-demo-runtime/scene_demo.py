"""Run the Windows demo through its menus and capture one 3D Present on Linux."""
import argparse
import json
from pathlib import Path
import struct

from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP

from menu_audio_trace import MenuAudioTrace
from scene_capture import SceneCapture
from software_skinning import SoftwareSkinning
from trace_boot import Boot, IMAGE_BASE, load_demo


# Triggers use the number of completed Presents, including the loading frames.
_KEYS = {
    136: (0x28, 0xD0, True),
    137: (0x28, 0xD0, False),
    138: (0x26, 0xC8, True),
    139: (0x26, 0xC8, False),
    140: (0x0D, 0x1C, True),
    141: (0x0D, 0x1C, False),
    145: (0x0D, 0x1C, True),
    149: (0x0D, 0x1C, False),
    261: (0x28, 0xD0, True),
    262: (0x28, 0xD0, False),
    263: (0x0D, 0x1C, True),
    265: (0x0D, 0x1C, False),
}

_SINGLE_RACE_KEYS = {
    **_KEYS,
    397: (0x0D, 0x1C, True),
    400: (0x0D, 0x1C, False),
    422: (0x0D, 0x1C, True),
    425: (0x0D, 0x1C, False),
    446: (0x0D, 0x1C, True),
    449: (0x0D, 0x1C, False),
    471: (0x0D, 0x1C, True),
    474: (0x0D, 0x1C, False),
}

_DESCENT_KEYS = {
    **_SINGLE_RACE_KEYS,
    785: (0x0D, 0x1C, True),
    788: (0x0D, 0x1C, False),
    1050: (0x0D, 0x1C, True),
    1053: (0x0D, 0x1C, False),
    1171: (0x0D, 0x1C, True),
    1175: (0x0D, 0x1C, False),
}


class MenuInput:
    def __init__(self, boot, audio, actions=_KEYS):
        self.boot = boot
        self.audio = audio
        self.actions = actions
        self.held = {}
        self.dispatch_original = boot.dispatch_host_call
        self.device_original = boot.input.device_call
        self.device_d3d = boot.d3d.handle_device
        boot.dispatch_host_call = self.dispatch
        boot.input.device_call = self.device
        boot.d3d.handle_device = self.graphics

    def graphics(self, name, arg, finish):
        self.device_d3d(name, arg, finish)
        if name != 'Present':
            return
        action = self.actions.get(self.boot.d3d.present_count)
        if action is None:
            return
        vk, scan, pressed = action
        if pressed:
            self.held[vk] = scan
        else:
            self.held.pop(vk, None)
        extended = 0x01000000 if scan & 128 else 0
        lparam = 1 | ((scan & 127) << 16) | extended | (0 if pressed else 0xC0000000)
        hwnd = next(iter(self.boot.window_handles))
        self.boot.message_loop.post(hwnd, 0x100 if pressed else 0x101, vk, lparam)

    def device(self, name, arg, finish):
        device = self.boot.input.devices[arg(0)]
        if name == 'GetDeviceState' and device['kind'] == 'keyboard' and device['acquired'] and arg(1) == 256:
            state = bytearray(256)
            for scan in self.held.values():
                state[scan] = 128
            self.boot.cpu.mem_write(arg(2), bytes(state))
            finish(0, 12)
            return True
        return self.device_original(name, arg, finish)

    def dispatch(self, cpu, address):
        dll, name = self.boot.apis.get(address, ('', ''))
        timed = self.boot.d3d.present_count >= 136 and name in ('QueryPerformanceCounter', 'GetTickCount')
        keyboard = name in ('GetKeyboardState', 'GetAsyncKeyState', 'GetKeyState')
        fmod = dll.lower() == 'fmod.dll' and name.startswith('_FSOUND_')
        if not (timed or keyboard or fmod):
            self.dispatch_original(cpu, address)
            return
        stack = cpu.reg_read(UC_X86_REG_ESP)
        return_address = self.boot.get(stack)

        def arg(i):
            return self.boot.get(stack + 4 + i * 4)

        def finish(result, pop):
            self.boot.api_sequence.append((dll, name))
            self.boot.api_calls[f'{dll}!{name}'] += 1
            self.boot.api_frames.append((return_address, stack, 0))
            cpu.reg_write(UC_X86_REG_EAX, result)
            cpu.reg_write(UC_X86_REG_ESP, stack + 4 + pop)
            cpu.reg_write(UC_X86_REG_EIP, return_address)

        if fmod:
            if not self.audio.dispatch(name, arg, finish):
                self.dispatch_original(cpu, address)
        elif name == 'GetKeyboardState':
            state = bytearray(256)
            for key in self.held:
                state[key] = 128
            cpu.mem_write(arg(0), bytes(state))
            finish(1, 4)
        elif keyboard:
            finish(0x8000 if arg(0) in self.held else 0, 4)
        else:
            microseconds = 1 + (self.boot.d3d.present_count - 136) * 16667
            if name == 'QueryPerformanceCounter':
                cpu.mem_write(arg(0), struct.pack('<Q', microseconds))
                finish(1, 4)
            else:
                finish(microseconds // 1000, 0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('exe', type=Path)
    parser.add_argument('assets', type=Path, help='root of a local copy of the Windows demo data')
    parser.add_argument('out', type=Path)
    parser.add_argument('--present', type=int, help='frame number (menu 390, Single Race 594, descent 1220)')
    parser.add_argument('--single-race', action='store_true', help='select Frozen Creek from Single Race')
    parser.add_argument('--descent', action='store_true', help='skip the intro and accept the Frozen Creek tutorial')
    parser.add_argument('--save-dir', type=Path, help='reuse a directory containing the original game saves')
    parser.add_argument('--waves', action='store_true', help='export samples requested by the guest to WAV')
    args = parser.parse_args()
    single_race = args.single_race or args.descent
    if args.present is None:
        args.present = 1220 if args.descent else 594 if single_race else 390
    if not 139 <= args.present <= 4096:
        parser.error('--present must be between 139 and 4096')
    output = args.out.resolve()
    output.mkdir(parents=True, exist_ok=True)
    save_root = args.save_dir.resolve() if args.save_dir else output / 'save'
    if not args.save_dir and save_root.exists() and any(save_root.iterdir()):
        parser.error('use a fresh output directory for the scripted first launch')
    profile = save_root / 'data' / 'user' / 'profile.pro'
    actions = _DESCENT_KEYS if args.descent else _SINGLE_RACE_KEYS if single_race else _KEYS
    if profile.is_file():
        actions = {frame: key for frame, key in actions.items() if frame not in (261, 262, 263, 265)}
    boot = Boot(*load_demo(args.exe), assets=args.assets.resolve(), save_root=save_root, stop_at_winmain=False,
                probe_audio=True, present_limit=args.present, frame_after_draw=True,
                draw_after_worker=True, drawn_frame_limit=4096, trace_blocks=False, trace_history=16384,
                guest_memory=0x20000000 if single_race else 0x08000000)
    capture = SceneCapture(boot.d3d, args.present, output / 'scene', SoftwareSkinning(boot.d3d))
    audio = MenuAudioTrace(boot, output / 'wav' if args.waves else None)
    MenuInput(boot, audio, actions)
    start = IMAGE_BASE + boot.pe.OPTIONAL_HEADER.AddressOfEntryPoint
    try:
        for _ in range(16):
            boot.status = 'instruction limit'
            boot.cpu.emu_start(start, 0, count=900_000_000)
            start = boot.cpu.reg_read(UC_X86_REG_EIP)
            if boot.status != 'instruction limit':
                break
            print(f'At Present #{boot.d3d.present_count}, continuing', flush=True)
    finally:
        boot.close_files()
    report = {
        'status': boot.status, 'present': boot.d3d.present_count,
        'heap_top_bytes': boot.heap.top, 'profile_bytes': 0,
        'software_skinned_draws': capture.software_skinned_draws,
        'files': [event for event in boot.file_events if 'profile.pro' in str(event).lower()],
        'level_script': [event for event in boot.file_events
                         if 'levelscript.lua' in str(event).lower()],
        'archive_reads': {name: len(reads) for name, reads in boot.file_reads.items()},
        'surface_copies': sum(event[0] == 'd3dx surface copy' for event in boot.d3d.events),
        'audio': boot.audio.events,
        'shaders': [event for event in boot.d3d.events if event[0] == 'assembled shader'],
    }
    if profile.is_file():
        report['profile_bytes'] = profile.stat().st_size
    (output / 'summary.json').write_text(json.dumps(report, indent=2))
    if boot.d3d.present_count != args.present or not (output / 'scene' / 'scene.json').is_file():
        raise SystemExit(boot.status)
    print(f'Captured Present #{args.present}, profile {report["profile_bytes"]} bytes')


if __name__ == '__main__':
    main()
