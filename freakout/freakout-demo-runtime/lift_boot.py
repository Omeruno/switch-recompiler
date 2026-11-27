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


def close_short_branches(lifter, read_va, sizes, base, end):
    def follow(target, origin, remaining, pending):
        if target in sizes:
            return True
        if target in pending:
            return bool(pending[target])
        if not base <= target < end or abs(target - origin) > 64:
            return False
        pending[target] = 0
        for instruction in lifter.md.disasm(read_va(target, min(32, end - target)), target):
            boundary = instruction.address + instruction.size
            mnemonic = instruction.mnemonic.split()[-1]
            if mnemonic in ('ret', 'retn'):
                pending[target] = boundary - target
                return True
            if mnemonic == 'call':
                if (remaining and instruction.op_str.startswith('0x') and
                        int(instruction.op_str, 16) in sizes and
                        follow(boundary, origin, remaining - 1, pending)):
                    pending[target] = boundary - target
                    return True
                break
            if mnemonic.startswith(('int', 'loop')) or mnemonic == 'ud2':
                break
            if mnemonic.startswith('j'):
                if not remaining or not instruction.op_str.startswith('0x'):
                    break
                edges = [int(instruction.op_str, 16)]
                if mnemonic != 'jmp':
                    edges.append(boundary)
                if all(follow(edge, origin, remaining - 1, pending) for edge in edges):
                    pending[target] = boundary - target
                    return True
                break
            if boundary in sizes:
                pending[target] = boundary - target
                return True
        return False

    for address, length in list(sizes.items()):
        inspected = list(lifter.md.disasm(read_va(address, length), address))
        if not inspected:
            continue
        last = inspected[-1]
        if not last.mnemonic.startswith('j') or not last.op_str.startswith('0x'):
            continue
        targets = [int(last.op_str, 16)]
        if last.mnemonic != 'jmp':
            targets.append(last.address + last.size)
        for target in targets:
            pending = {}
            if follow(target, last.address, 2, pending):
                sizes.update(pending)


def add_race_blocks(lifter, read_va, sizes):
    selector, table = 0x492875, 0x493214
    if selector not in sizes:
        return
    if read_va(selector, 7) != b"\xff\x24\x85" + struct.pack("<I", table):
        raise RuntimeError("unrecognized FreakOut race tip dispatch")
    tips = struct.unpack("<10I", read_va(table, 40))
    if any(not selector + 7 <= address < table for address in tips):
        raise RuntimeError("invalid FreakOut race tip table")

    # The tips are selected with rand(), so a reference descent need not visit each case.
    pending = list(tips)
    ranges = [(selector + 7, table), (0x415060, 0x415080)]
    # This HUD update can take either path depending on the timer and the race score.
    if 0x493d45 in sizes:
        pending.append(0x493d69)
        ranges.append((0x493d69, 0x493dd3))
    add_verified_blocks(lifter, read_va, sizes, pending, ranges)
    # The first race HUD update also varies with the countdown state.
    selector, table = 0x43adf6, 0x43b3c0
    if selector in sizes:
        if read_va(selector, 7) != b"\xff\x24\x85" + struct.pack("<I", table):
            raise RuntimeError("unrecognized FreakOut race HUD dispatch")
        targets = struct.unpack("<13I", read_va(table, 52))
        if any(not selector + 7 <= address < table for address in targets):
            raise RuntimeError("invalid FreakOut race HUD table")
        add_verified_blocks(lifter, read_va, sizes, list(targets), [(selector + 7, table)])
    # The HUD text builder chooses a different glyph path for the live timer.
    if 0x4c5060 in sizes:
        ranges = [(0x4c4410, 0x4c45cf), (0x4c5060, 0x4c5366), (0x4c60d5, 0x4c6139)]
        collect_verified_edges(lifter, read_va, sizes, [0x4c50bd], ranges, {})
    # Live race messages select text by numeric ID using a byte-to-index table.
    if 0x574b4e in sizes:
        selector, table, index = 0x574b55, 0x57752c, 0x5776c4
        if read_va(selector, 7) != b'\xff\x24\x85' + struct.pack('<I', table):
            raise RuntimeError("unexpected FreakOut race message dispatch")
        count = (index - table) // 4
        if max(read_va(index, 224)) >= count:
            raise RuntimeError("invalid FreakOut race message index")
        targets = struct.unpack(f"<{count}I", read_va(table, count * 4))
        if any(not selector + 7 <= address < table for address in targets):
            raise RuntimeError("invalid FreakOut race message table")
        add_verified_blocks(lifter, read_va, sizes, list(targets), [(selector + 7, table)])
    # Course geometry uses the other floating-point branch after the run starts.
    if 0x5101b5 in sizes:
        collect_verified_edges(lifter, read_va, sizes, [0x5101bb], [(0x510110, 0x5102b3)], {})
    if 0x5377d2 in sizes:
        ranges = [(0x535c90, 0x535d7b), (0x5377d2, 0x5379a6)]
        collect_verified_edges(lifter, read_va, sizes, [0x5377f5], ranges, {})
    if 0x43c116 in sizes:
        handlers = (0x436810, 0x43a1a0, 0x436c70, 0x436850)
        if struct.unpack('<4I', read_va(0x6a8dc8, 16)) != handlers:
            raise RuntimeError("unexpected FreakOut live input handlers")
        selector, table = 0x49cf09, 0x49db70
        if read_va(selector, 7) != b'\xff\x24\x8d' + struct.pack('<I', table):
            raise RuntimeError("unexpected FreakOut live input event table")
        targets = struct.unpack('<12I', read_va(table, 48))
        if any(not selector + 7 <= address < table for address in targets):
            raise RuntimeError("invalid FreakOut live input event table")
        ranges = [(0x410460, 0x410486), (0x436850, 0x436874), (0x436c70, 0x436cbc), (0x43a1a0, 0x43a34b),
                  (0x43c116, 0x43c16f), (0x490260, 0x490293), (0x49cec0, table),
                  (0x49dba0, 0x49de02), (0x578500, 0x57852e)]
        collect_verified_edges(lifter, read_va, sizes, list(handlers), ranges, {selector: targets})
    # Steering at nonzero input uses the original signed correction and clamp.
    if 0x4a815a in sizes:
        add_verified_blocks(lifter, read_va, sizes, [0x4a8163], [(0x4a8163, 0x4a81ed)])
    if 0x4a6a50 in sizes:
        collect_verified_edges(lifter, read_va, sizes, [0x4a6a75], [(0x4a6a50, 0x4a6ba9)], {})


def add_input_blocks(lifter, read_va, sizes):
    if 0x440ab0 not in sizes:
        return
    first, last = 0x4409a0, 0x440bb5
    # Include the keyboard and action tables even if the trace uses only Enter.
    tables = (
        (0x4409d4, 0x440a94, 6, 0x85),
        (0x440af4, 0x440bb8, 6, 0x85),
        (0x440b40, 0x440bf0, 8, 0x8d),
    )
    dispatches = {}
    for selector, table, count, sib in tables:
        if read_va(selector, 7) != bytes((0xff, 0x24, sib)) + struct.pack("<I", table):
            raise RuntimeError(f"unrecognized FreakOut input dispatch at {selector:#x}")
        targets = struct.unpack(f"<{count}I", read_va(table, 4 * count))
        if any(not first <= address < last or 0x440a92 <= address < 0x440ab0 for address in targets):
            raise RuntimeError(f"invalid FreakOut input table at {table:#x}")
        dispatches[selector] = targets
    ranges = [(first, 0x440a92), (0x440ab0, last)]
    pending = [address for targets in dispatches.values() for address in targets]
    # Repeated input on the loading screen can return through this separate epilogue.
    if 0x48a490 in sizes:
        pending.append(0x48a57b)
        ranges.append((0x48a57b, 0x48a590))
    # A button can request a UI sound before the original bank is open.
    if 0x5263d5 in sizes:
        pending.append(0x526434)
        ranges.append((0x526434, 0x52643f))
    collect_verified_edges(lifter, read_va, sizes, pending, ranges, dispatches)


def add_frontend_blocks(lifter, read_va, sizes):
    # The language-choice handler and loading-screen input have alternate paths
    # that an English confirmation and a neutral loading trace do not execute.
    if 0x471450 in sizes:
        ranges = [(0x471503, 0x471541), (0x4c1950, 0x4c19f8)]
        collect_verified_edges(lifter, read_va, sizes, [0x471503], ranges, {})
    if 0x472210 not in sizes:
        return
    selector, table = 0x472230, 0x4722e8
    if read_va(selector, 7) != b"\xff\x24\x85" + struct.pack("<I", table):
        raise RuntimeError("unrecognized FreakOut loading input dispatch")
    targets = struct.unpack("<8I", read_va(table, 32))
    if any(not selector + 7 <= address < table for address in targets):
        raise RuntimeError("invalid FreakOut loading input table")
    ranges = [(0x472210, 0x4722e6), (0x472310, 0x472541),
              (0x4d2820, 0x4d2852), (0x4d2870, 0x4d2877)]
    collect_verified_edges(lifter, read_va, sizes, [0x472310, *targets], ranges, {selector: targets})


def add_profile_blocks(lifter, read_va, sizes):
    if 0x4d68d0 not in sizes:
        return
    # Keyboard focus can return to the profile name before the user confirms Done.
    if read_va(0x5af3da, 6) != b"\xff\x25\x48\x54\x6a\x00":
        raise RuntimeError("unrecognized FreakOut C++ exception import")
    sizes.setdefault(0x5af3da, 6)
    ranges = [(0x4019b0, 0x401aa9), (0x401d90, 0x401e16), (0x4872cf, 0x4873aa),
              (0x4c5630, 0x4c56eb), (0x4c5750, 0x4c5773), (0x4d3510, 0x4d36fd), (0x4d4ce0, 0x4d5108),
              (0x4d5110, 0x4d515e), (0x4d5160, 0x4d5495), (0x4d68e5, 0x4d6924)]
    collect_verified_edges(lifter, read_va, sizes, [0x4872cf, 0x4d5110, 0x4d68e5], ranges, {})


def collect_verified_edges(lifter, read_va, sizes, pending, ranges, dispatches):
    for address, length in sizes.items():
        if not any(start <= address < end for start, end in ranges):
            continue
        instructions = list(lifter.md.disasm(read_va(address, length), address))
        if instructions:
            pending.extend(branch_targets(instructions[-1], dispatches))
    add_verified_blocks(lifter, read_va, sizes, pending, ranges, dispatches)


def branch_targets(ins, dispatches):
    mnemonic = ins.mnemonic.split()[-1]
    end = ins.address + ins.size
    if mnemonic in ('ret', 'retn'):
        return ()
    if mnemonic.startswith(('call', 'j')):
        if ins.op_str.startswith('0x'):
            targets = (int(ins.op_str, 16),)
        elif mnemonic == 'call':
            targets = ()
        elif ins.address in dispatches:
            return dispatches[ins.address]
        else:
            raise RuntimeError(f"unexpected indirect FreakOut branch at {ins.address:#x}")
        return targets if mnemonic == 'jmp' else (*targets, end)
    return (end,)


def add_verified_blocks(lifter, read_va, sizes, pending, ranges, dispatches=None):
    dispatches = dispatches or {}
    while pending:
        address = pending.pop()
        if address in sizes:
            continue
        limit = next((end for start, end in ranges if start <= address < end), None)
        if limit is None:
            raise RuntimeError(f"untraced FreakOut target at {address:#x}")
        for ins in lifter.md.disasm(read_va(address, limit - address), address):
            mnemonic = ins.mnemonic.split()[-1]
            end = ins.address + ins.size
            if mnemonic.startswith(('call', 'j')):
                pending.extend(branch_targets(ins, dispatches))
            elif mnemonic not in ('ret', 'retn'):
                if mnemonic.startswith(('int', 'loop')) or mnemonic == 'ud2':
                    raise RuntimeError(f"invalid FreakOut instruction at {ins.address:#x}")
                if end not in sizes:
                    continue
            sizes[address] = end - address
            break
        else:
            raise RuntimeError(f"unterminated FreakOut block at {address:#x}")


def emit(boot, upstream, out, *, standalone=True, close_local_branches=False, native_memory=LOW_MEMORY):
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
        sizes = {address: length for address, length in boot.block_sizes.items() if base <= address < end}
        if close_local_branches:
            close_short_branches(lifter, read_va, sizes, base, end)
            if module is boot.pe:
                add_input_blocks(lifter, read_va, sizes)
                add_frontend_blocks(lifter, read_va, sizes)
                add_profile_blocks(lifter, read_va, sizes)
                add_race_blocks(lifter, read_va, sizes)
        for address, length in sorted(sizes.items()):
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
    (out / "cpu.h").write_text(f"""#include <stdint.h>
#define FO_GUEST_MEMORY 0x{native_memory:08x}u
#define FO_SKIN_DEFORM 0x006430f0u
#define FO_SKIN_MATRICES 0x005c777du
uint8_t *guest_pointer(uint32_t address);
""" + runtime)
    entries = "\n".join(f"    {{ 0x{address:08x}u, L_{address:08X} }},"
                        for address, _ in sorted(routines))
    game_addresses = [address for address, _ in routines if address < RUNTIME_BASE]
    crt_addresses = [address for address, _ in routines if address >= RUNTIME_BASE]
    game_start = min(game_addresses, default=IMAGE_BASE)
    crt_start = min(crt_addresses, default=RUNTIME_BASE)
    game_span = max(game_addresses, default=game_start) - game_start + 1
    crt_span = max(crt_addresses, default=crt_start) - crt_start + 1
    index_type = "uint16_t" if len(routines) <= 65535 else "uint32_t"
    lookup_data = f"""
#define GAME_CODE_START 0x{game_start:x}u
#define RUNTIME_CODE_START 0x{crt_start:x}u
static {index_type} game_index[{game_span}], runtime_index[{crt_span}];
"""
    boundary = ""
    if not boot.stop_at_winmain:
        boundary = f"if (c->eip == 0x{GAME_ENTRY:08x}u && !seen_winmain++) return c->eip;"
    if not standalone:
        boundary += "\n        if (count && (c->eip == FO_SKIN_DEFORM || c->eip == FO_SKIN_MATRICES)) return c->eip;"
    own_main = "int main(void) { return 0; }" if standalone else ""
    winmain_state = "static unsigned seen_winmain;" if not boot.stop_at_winmain else ""
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
    if (address < FO_GUEST_MEMORY)
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
""" + lookup_data + """
static int lookup_ready;

static void init_lookup(void)
{
    for (size_t i = 0; i < sizeof blocks / sizeof blocks[0]; ++i) {
        uint32_t offset = blocks[i].address - GAME_CODE_START;
        if (offset < sizeof game_index / sizeof game_index[0])
            game_index[offset] = i + 1;
        else
            runtime_index[blocks[i].address - RUNTIME_CODE_START] = i + 1;
    }
    lookup_ready = 1;
}

static inline const struct Block *lookup(uint32_t address)
{
    uint32_t game_offset = address - GAME_CODE_START;
    uint32_t runtime_offset = address - RUNTIME_CODE_START;
    size_t index = 0;
    if (game_offset < sizeof game_index / sizeof game_index[0])
        index = game_index[game_offset];
    else if (runtime_offset < sizeof runtime_index / sizeof runtime_index[0])
        index = runtime_index[runtime_offset];
    return index ? &blocks[index - 1] : NULL;
}

uint32_t advance(uint8_t *guest, uint8_t *runtime, CPU *c)
{
    if (!lookup_ready)
        init_lookup();
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
    if (!lookup_ready)
        init_lookup();
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
