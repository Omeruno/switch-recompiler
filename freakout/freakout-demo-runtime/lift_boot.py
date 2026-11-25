"""Lift the executed startup blocks from the demo and its CRT modules."""
import re
import struct
import sys
from pathlib import Path

from window_probe import WINDOW_RETURN
from thread_probe import THREAD_RETURN
from trace_boot import GAME_ENTRY, GATE, IMAGE_BASE, LOW_MEMORY, Boot

LOW_SIZE = LOW_MEMORY
RUNTIME_BASE = 0x7C340000
RUNTIME_SIZE = 0xE0000
NATIVE_LOW = 0x10000000
NATIVE_RUNTIME = 0x20000000
NATIVE_CPU = 0x30000000
NATIVE_CPU_PAGE = 0x1000
NATIVE_STACK = 0x31000000
RETURN = 0x40000000


def emit(boot, upstream, out, *, standalone=True):
    sys.path.insert(0, str(upstream / "tools/lift"))
    from lift32_cpu import Lifter
    routines = []
    skipped = []
    for module in (boot.pe, boot.crt, boot.cpp):
        base = module.OPTIONAL_HEADER.ImageBase
        end = base + module.OPTIONAL_HEADER.SizeOfImage
        def read_va(address, count):
            return module.get_data(address - base, count)
        lifter = Lifter("demo", module.OPTIONAL_HEADER.SizeOfImage, read_va)
        lifter.reloc_vas = set()
        for relocation in module.DIRECTORY_ENTRY_BASERELOC:
            lifter.reloc_vas.update(base + e.rva for e in relocation.entries if e.type == 3)
        for address, length in sorted(boot.block_sizes.items()):
            if not base <= address < end or (address == GAME_ENTRY and boot.stop_at_winmain):
                continue
            instructions = list(lifter.md.disasm(read_va(address, length), address))
            if sum(ins.size for ins in instructions) != length:
                raise RuntimeError(f"cannot disassemble block {address:#x}")
            lifter.func_start, lifter.func_end = address, address + length
            lifter.jumptables = {}
            code = [f"static void L_{address:08X}(CPU *c) {{"]
            for index, ins in enumerate(instructions):
                mnemonic = ins.mnemonic.split()[-1]
                last = index + 1 == len(instructions)
                if mnemonic in ("ret", "retn"):
                    code.append("    c->eip = rd32(c->esp);")
                lines = ["guest_cpuid(c);"] if mnemonic == "cpuid" else lifter.translate(ins, set())
                for line in lines:
                    if "unimplemented" in line.lower() or "unsupported" in line.lower() or "TODO" in line:
                        skipped.append((ins.address, ins.mnemonic, line))
                    code.append(f"    {line}")
                if not last and (mnemonic == "call" or mnemonic == "ret" or mnemonic.startswith("j")):
                    raise RuntimeError(f"control transfer inside block at {ins.address:#x}")
            last = instructions[-1].mnemonic.split()[-1]
            if last not in ("ret", "retn", "call", "jmp"):
                code.append(f"    c->eip = 0x{address + length:08x}u;")
            code.append("}")
            routines.append((address, "\n".join(code)))

    runtime = (upstream / "runtime/recomp32_cpu/cpu.h").read_text()
    runtime, count = re.subn(r"\(uintptr_t\)a\b", "guest_pointer(a)", runtime)
    if not count:
        raise RuntimeError("PCRecomp memory helpers changed")
    (out / "cpu.h").write_text("""#include <stdint.h>
uint8_t *guest_pointer(uint32_t address);
""" + runtime)
    entries = "\n".join(f"    {{ 0x{address:08x}u, L_{address:08X} }},"
                        for address, _ in sorted(routines))
    boundary = ""
    if not boot.stop_at_winmain:
        boundary = f"if (c->eip == 0x{GAME_ENTRY:08x}u && !seen_winmain++) return c->eip;"
    own_main = "int main(void) { return 0; }" if standalone else ""
    winmain_state = "static unsigned seen_winmain;" if boundary else ""
    (out / "blocks.c").write_text("""#include "cpu.h"
#include <stddef.h>
#include <stdlib.h>

_Static_assert(offsetof(CPU, st) == 64 && offsetof(CPU, fpu_top) == 128, "CPU x87 layout changed");
_Static_assert(sizeof(CPU) <= 0x1000, "CPU state exceeds its guest page");
""" + own_main + """

static uint8_t *low_mem;
static uint8_t *runtime_mem;
""" + winmain_state + """
uint32_t g_image_delta;

uint8_t *guest_pointer(uint32_t address)
{
    if (address < 0x08000000u)
        return low_mem + address;
    if (address >= 0x7C340000u && address < 0x7C420000u)
        return runtime_mem + (address - 0x7C340000u);
    __builtin_trap();
}
static void dispatch(CPU *c, uint32_t target) { c->eip = target; }
#define dispatch_jmp dispatch

/* CPUID profile used by the reference Unicorn x86 run. */
static void guest_cpuid(CPU *c)
{
    switch (c->eax) {
    case 0:
        c->eax = 0xDu; c->ebx = 0x756E6547u;
        c->ecx = 0x6C65746Eu; c->edx = 0x49656E69u;
        break;
    case 1:
        c->eax = 0x306C4u; c->ebx = 0x800u;
        c->ecx = 0xCED82203u; c->edx = 0x78BFBFDu;
        break;
    case 0x80000000u:
        c->eax = 0x80000008u; c->ebx = 0x756E6547u;
        c->ecx = 0x6C65746Eu; c->edx = 0x49656E69u;
        break;
    case 0x80000001u:
        c->eax = 0x306C4u; c->ebx = 0;
        c->ecx = 0x21u; c->edx = 0x28100800u;
        break;
    default: __builtin_trap();
    }
}

""" + "\n".join(body for _, body in routines) + """
struct Block { uint32_t address; void (*run)(CPU *); };
static const struct Block blocks[] = {
""" + entries + """
};

static const struct Block *lookup(uint32_t address)
{
    size_t lo = 0, hi = sizeof blocks / sizeof blocks[0];
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (blocks[mid].address == address)
            return &blocks[mid];
        if (blocks[mid].address < address)
            lo = mid + 1;
        else
            hi = mid;
    }
    return NULL;
}

uint32_t advance(uint8_t *guest, uint8_t *runtime, CPU *c)
{
    low_mem = guest;
    runtime_mem = runtime;
    for (unsigned count = 0; count < 2048; count++) {
""" + boundary + """
        const struct Block *block = lookup(c->eip);
        if (!block)
            return c->eip;
        block->run(c);
    }
    return 0;
}

int has_block(uint32_t address)
{
    return lookup(address) != NULL;
}
""")
    print(f"{len(routines)} startup blocks generated; {len(skipped)} visibly unsupported instructions.")
    for address, mnemonic, line in skipped[:20]:
        print(f"{address:#x}: {mnemonic} {line}")


def arm_machine(path):
    from unicorn import UC_ARCH_ARM64, UC_MODE_ARM, Uc
    image = path.read_bytes()
    if image[:6] != b"\x7fELF\x02\x01" or struct.unpack_from("<H", image, 18)[0] != 183:
        raise ValueError("expected ARM64 ELF")
    entry, phoff = struct.unpack_from("<QQ", image, 24)
    size, count = struct.unpack_from("<HH", image, 54)
    segments = [struct.unpack_from("<IIQQQQQQ", image, phoff + i * size) for i in range(count)]
    loads = [s for s in segments if s[0] == 1]
    lo = min(s[3] for s in loads) & ~0xFFF
    hi = (max(s[3] + s[6] for s in loads) + 0xFFF) & ~0xFFF
    cpu = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    cpu.mem_map(lo, hi - lo)
    for _, _, position, address, _, length, _, _ in loads:
        if length:
            cpu.mem_write(address, image[position:position + length])
    return cpu, entry


class Guest:
    """Expose the same guest registers and memory to the startup import model."""
    def __init__(self, native):
        from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_EIP, UC_X86_REG_ESP
        self.native = native
        self.offsets = {UC_X86_REG_EAX: 0, UC_X86_REG_ESP: 16, UC_X86_REG_EBP: 20, UC_X86_REG_EIP: 32}
        self.stopped = False

    def address(self, address):
        if 0 <= address < LOW_SIZE:
            return NATIVE_LOW + address
        if RUNTIME_BASE <= address < RUNTIME_BASE + RUNTIME_SIZE:
            return NATIVE_RUNTIME + address - RUNTIME_BASE
        raise ValueError(f"guest address outside mapped modules: {address:#x}")

    def mem_write(self, address, data):
        self.native.mem_write(self.address(address), data)

    def mem_read(self, address, size):
        return self.native.mem_read(self.address(address), size)

    def reg_write(self, reg, value):
        self.native.mem_write(NATIVE_CPU + self.offsets[reg], struct.pack("<I", value & 0xFFFFFFFF))

    def reg_read(self, reg):
        return struct.unpack("<I", self.native.mem_read(NATIVE_CPU + self.offsets[reg], 4))[0]

    def context_save(self):
        return bytes(self.native.mem_read(NATIVE_CPU, NATIVE_CPU_PAGE))

    def context_restore(self, context):
        self.native.mem_write(NATIVE_CPU, context)

    def push_guest_float(self, value):
        top_address = NATIVE_CPU + 128
        top = (struct.unpack("<I", self.native.mem_read(top_address, 4))[0] - 1) & 7
        self.native.mem_write(NATIVE_CPU + 64 + top * 8, struct.pack("<d", value))
        self.native.mem_write(top_address, struct.pack("<I", top))

    def pop_guest_float(self):
        top_address = NATIVE_CPU + 128
        top = struct.unpack("<I", self.native.mem_read(top_address, 4))[0]
        value = struct.unpack("<d", self.native.mem_read(NATIVE_CPU + 64 + top * 8, 8))[0]
        self.native.mem_write(top_address, struct.pack("<I", (top + 1) & 7))
        return value

    def emu_stop(self):
        self.stopped = True


def compare_arm(executable, cpp, crt, elf, expected, *, assets=None, into_game=False, probe_renderer=False, probe_d3d=False, probe_frame=False, probe_audio=False, present_limit=1, archive_read_limit=0, compressed_upload_limit=0, buffer_upload_limit=0, frame_after_draw=False, drawn_frame_limit=1, draw_after_worker=False):
    from unicorn.arm64_const import (UC_ARM64_REG_PC, UC_ARM64_REG_SP, UC_ARM64_REG_X0,
                                    UC_ARM64_REG_X1, UC_ARM64_REG_X2, UC_ARM64_REG_X30)
    from unicorn.x86_const import UC_X86_REG_EIP, UC_X86_REG_ESP
    arm, entry = arm_machine(elf)
    arm.mem_map(NATIVE_LOW, LOW_SIZE)
    arm.mem_map(NATIVE_RUNTIME, RUNTIME_SIZE)
    arm.mem_map(NATIVE_CPU, NATIVE_CPU_PAGE)
    arm.mem_map(NATIVE_STACK - 0x10000, 0x10000)
    arm.mem_map(RETURN, 0x1000)
    boot = Boot(executable, cpp, crt, stop_at_winmain=not into_game, assets=assets,
                probe_renderer=probe_renderer, probe_d3d=probe_d3d, probe_frame=probe_frame, probe_audio=probe_audio, present_limit=present_limit, archive_read_limit=archive_read_limit, compressed_upload_limit=compressed_upload_limit, buffer_upload_limit=buffer_upload_limit, frame_after_draw=frame_after_draw, drawn_frame_limit=drawn_frame_limit, draw_after_worker=draw_after_worker)
    arm.mem_write(NATIVE_LOW, bytes(boot.cpu.mem_read(0, LOW_SIZE)))
    for module in (boot.crt, boot.cpp):
        base = module.OPTIONAL_HEADER.ImageBase
        size = module.OPTIONAL_HEADER.SizeOfImage
        arm.mem_write(NATIVE_RUNTIME + base - RUNTIME_BASE,
                      bytes(boot.cpu.mem_read(base, size)))
    guest = Guest(arm)
    guest.reg_write(UC_X86_REG_EIP, IMAGE_BASE + boot.pe.OPTIONAL_HEADER.AddressOfEntryPoint)
    guest.reg_write(UC_X86_REG_ESP, boot.cpu.reg_read(UC_X86_REG_ESP))
    boot.cpu = guest
    boot.thread.initial_context = guest.context_save()
    def verify():
        if (boot.status != expected["status"] or boot.winmain != expected["winmain_arguments"]
                or boot.debug_messages != expected["debug"]
                or boot.api_calls != expected["imports"] or boot.file_events != expected["files"]
                or boot.api_sequence != expected["import_sequence"]
                or boot.thread.log != expected["threads"]
                or boot.file_bytes != expected["file_bytes"] or boot.file_reads != expected["archive_reads"] or boot.sort_events != expected["sorts"] or boot.window_events != expected["windows"]
                or (boot.d3d.events if boot.d3d else []) != expected["d3d"]
                or (boot.d3d.uploads if boot.d3d else []) != expected["uploads"]
                or (boot.audio.events if boot.audio else []) != expected["audio"]
                or (boot.input.events if boot.input else []) != expected["input"]
                or (boot.video.events if boot.video else []) != expected["video"]
                or ((boot.message_loop.events, boot.message_loop.idle_polls) if boot.message_loop else None)
                != expected["messages"]):
            raise RuntimeError(f"x86 and ARM64 diverged at {boot.status}: {boot.winmain}")
        print(f"ARM64 reproduced the x86 path through {sum(boot.api_calls.values())} import calls.")
        print(f"Read {boot.file_bytes} bytes through guest fopen/fread.")
        print(f"Last checkpoint: {boot.status}")

    # The native dispatcher yields every 2048 blocks even without a host call.
    # An import count cannot bound long stretches of CPU-only game code.
    for step in range(len(expected["import_sequence"]) + 65536):
        arm.reg_write(UC_ARM64_REG_SP, NATIVE_STACK)
        arm.reg_write(UC_ARM64_REG_X0, NATIVE_LOW)
        arm.reg_write(UC_ARM64_REG_X1, NATIVE_RUNTIME)
        arm.reg_write(UC_ARM64_REG_X2, NATIVE_CPU)
        arm.reg_write(UC_ARM64_REG_X30, RETURN)
        arm.emu_start(entry, RETURN, count=40_000_000)
        if arm.reg_read(UC_ARM64_REG_PC) != RETURN:
            state = struct.unpack("<9I", arm.mem_read(NATIVE_CPU, 36))
            raise RuntimeError(f"ARM64 stopped at host PC {arm.reg_read(UC_ARM64_REG_PC):#x}, "
                               f"import step {step}, guest registers {[hex(v) for v in state]}")
        address = arm.reg_read(UC_ARM64_REG_X0)
        if address == 0:
            continue
        if address == GAME_ENTRY:
            boot.game_entry(guest, address, 0, None)
            if guest.stopped:
                verify()
                return
            continue
        callback = address in (GATE, WINDOW_RETURN, THREAD_RETURN)
        if not callback and address not in boot.apis:
            raise RuntimeError(f"ARM64 reached untranslated x86 address {address:#x} at step {step}")
        boot.host_call(guest, address, 0, None)
        if not callback:
            index = len(boot.api_sequence) - 1
            if index >= len(expected["import_sequence"]):
                raise RuntimeError(f"ARM64 made an unexpected extra import: {boot.api_sequence[-1]}")
            if boot.api_sequence[-1] != expected["import_sequence"][index] or boot.api_frames[-1] != expected["import_frames"][index]:
                raise RuntimeError(f"import #{index}: ARM64 {boot.api_sequence[-1]} {boot.api_frames[-1]}, "
                                   f"x86 {expected['import_sequence'][index]} {expected['import_frames'][index]}")
        if guest.stopped:
            verify()
            return
    ip = guest.reg_read(UC_X86_REG_EIP)
    raise RuntimeError(f"ARM64 dispatcher limit reached at x86 PC {ip:#x} "
                       f"after {len(boot.api_sequence)} imports")


def main():
    import argparse
    import subprocess
    from trace_boot import load_demo
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exe", type=Path)
    parser.add_argument("pcrecomp", type=Path)
    parser.add_argument("zig", type=Path)
    parser.add_argument("--output", type=Path, default=Path("build"))
    parser.add_argument("--into-game", action="store_true")
    parser.add_argument("--assets", type=Path)
    parser.add_argument("--probe-renderer", action="store_true")
    parser.add_argument("--probe-d3d", action="store_true")
    parser.add_argument("--probe-frame", action="store_true")
    parser.add_argument("--probe-audio", action="store_true")
    parser.add_argument("--frame-after-draw", action="store_true")
    parser.add_argument("--drawn-frame-limit", type=int, choices=range(1, 17), default=1)
    parser.add_argument("--buffer-upload-limit", type=int, default=0)
    parser.add_argument("--compressed-upload-limit", type=int, default=0)
    parser.add_argument("--instruction-limit", type=int, default=20_000_000)
    parser.add_argument("--archive-read-limit", type=int, default=0)
    parser.add_argument("--present-limit", type=int, default=1)
    parser.add_argument("--after-worker", action="store_true")
    args = parser.parse_args()
    if not 1 <= args.present_limit <= 4096:
        parser.error("--present-limit must be between 1 and 4096")
    payload = load_demo(args.exe)
    assets = args.assets.resolve() if args.assets else None
    into_game = args.into_game or args.probe_renderer or args.probe_d3d or args.probe_frame or args.probe_audio
    original = Boot(*payload, stop_at_winmain=not into_game, assets=assets,
                    probe_renderer=args.probe_renderer, probe_d3d=args.probe_d3d, probe_frame=args.probe_frame, probe_audio=args.probe_audio, present_limit=args.present_limit, archive_read_limit=args.archive_read_limit, compressed_upload_limit=args.compressed_upload_limit, buffer_upload_limit=args.buffer_upload_limit, frame_after_draw=args.frame_after_draw, drawn_frame_limit=args.drawn_frame_limit, draw_after_worker=args.after_worker)
    original.instruction_limit = args.instruction_limit
    expected = original.run()
    if expected["winmain_arguments"] is None:
        raise RuntimeError(expected["status"])
    args.output.mkdir(parents=True, exist_ok=True)
    emit(original, args.pcrecomp, args.output)
    elf = args.output / "boot-arm64.elf"
    subprocess.run([str(args.zig), "cc", "-target", "aarch64-linux-musl", "-O1", "-fno-inline", "-Wno-parentheses-equality",
                    "-lc", "-lm", "-static", "-Wl,-e,advance",
                    str(args.output / "blocks.c"), "-o", str(elf)], check=True)
    compare_arm(*payload, elf, expected, assets=assets, into_game=into_game,
                probe_renderer=args.probe_renderer, probe_d3d=args.probe_d3d, probe_frame=args.probe_frame, probe_audio=args.probe_audio, present_limit=args.present_limit, archive_read_limit=args.archive_read_limit, compressed_upload_limit=args.compressed_upload_limit, buffer_upload_limit=args.buffer_upload_limit, frame_after_draw=args.frame_after_draw, drawn_frame_limit=args.drawn_frame_limit, draw_after_worker=args.after_worker)


if __name__ == "__main__":
    main()
