"""Check PCRecomp-generated ARM64 code using the corresponding demo x86 code."""
import argparse
import hashlib
import random
import re
import struct
import subprocess
import sys
from pathlib import Path

import pefile
from unicorn import UC_ARCH_ARM64, UC_ARCH_X86, UC_MODE_32, UC_MODE_ARM, Uc
from unicorn.arm64_const import UC_ARM64_REG_PC, UC_ARM64_REG_SP, UC_ARM64_REG_X0, UC_ARM64_REG_X30
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_EFLAGS, UC_X86_REG_EIP, UC_X86_REG_ESP

DEMO_HASH = "69830e276dceeb1017be49a6193c6cfaa0905c6397b8aa9c2907e8707f4adce9"
FUNCTION_RVA = 0x2660D0
FUNCTION_SIZE = 14
FLAGS = 0xC5
CODE = 0x10000
RETURN = 0x400000
GUEST = 0x500000
ARM_STACK = 0xD0F000
STARTUP = ((0x1AF59E, 0x5AF615 - 0x5AF59E), (0x1AF84C, 59))


def generate(args, work, base, functions, name):
    bounds = work / "functions.txt"
    addresses = [base + rva for rva, _ in functions]
    entries = zip(addresses, (size for _, size in functions))
    bounds.write_text("".join(f"{va:#x} {size} selected_function\n" for va, size in entries),
                      encoding="ascii")
    lifter = args.pcrecomp / "tools/lift/lift32_cpu.py"
    source = work / "lifted.c"
    subprocess.run([sys.executable, str(lifter), str(args.demo), str(bounds), str(source),
                    *map(hex, addresses)], check=True)

    upstream = (args.pcrecomp / "runtime/recomp32_cpu/cpu.h").read_text(encoding="utf-8")
    # Isolated memory region for the test. The full-game allocator is a separate step.
    portable, changes = re.subn(r"\(uintptr_t\)a\b", r"(guest_memory + a)", upstream)
    if not changes:
        raise RuntimeError("upstream memory helpers changed")
    header = "#include <stdint.h>\nextern uint8_t *guest_memory;\n" + portable
    (work / "cpu.h").write_text(header, encoding="utf-8")

    wrapper = Path(__file__).with_name(f"arm64_{name}.c").read_text(encoding="utf-8")
    (work / "check.c").write_text(wrapper + '\n#include "lifted.c"\n', encoding="utf-8")
    elf = work / f"{name}-arm64.elf"
    subprocess.run([
        str(args.zig), "cc", "-target", "aarch64-linux-musl", "-O2", "-ffreestanding",
        "-lc", "-static", f"-Wl,-e,check_{name}", str(work / "check.c"), "-o", str(elf),
    ], check=True)
    return elf


def arm_machine(path, memory_size):
    binary = path.read_bytes()
    if binary[:6] != b"\x7fELF\x02\x01" or struct.unpack_from("<H", binary, 18)[0] != 183:
        raise RuntimeError("expected a little-endian ARM64 ELF")
    entry, phoff = struct.unpack_from("<QQ", binary, 24)
    phsize, phnum = struct.unpack_from("<HH", binary, 54)
    segments = [
        struct.unpack_from("<IIQQQQQQ", binary, phoff + index * phsize)
        for index in range(phnum)
    ]
    loads = [s for s in segments if s[0] == 1]
    lo = min(s[3] for s in loads) & ~0xFFF
    hi = (max(s[3] + s[6] for s in loads) + 0xFFF) & ~0xFFF
    vm = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    vm.mem_map(lo, hi - lo)
    for _, _, offset, address, _, size, _, _ in loads:
        if size:
            vm.mem_write(address, binary[offset:offset + size])
    vm.mem_map(RETURN, 0x1000)
    vm.mem_map(GUEST, memory_size)
    vm.mem_map(ARM_STACK - 0x1000, 0x1000)
    return vm, entry


def compare(x86_code, arm, entry):
    x86 = Uc(UC_ARCH_X86, UC_MODE_32)
    x86.mem_map(CODE, 0x1000)
    x86.mem_map(RETURN, 0x1000)
    x86.mem_map(GUEST, 0x1000)
    x86.mem_write(CODE, x86_code)
    edges = (0, 1, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFE, 0xFFFFFFFF)
    rng = random.Random(FUNCTION_RVA)
    inputs = [(a, b) for a in edges for b in edges]
    inputs.extend((rng.getrandbits(32), rng.getrandbits(32)) for _ in range(10_000))
    for a, b in inputs:
        fields = struct.pack("<II", a, b)
        x86.mem_write(GUEST + 0x208, fields)
        arm.mem_write(GUEST + 0x208, fields)
        x86.mem_write(GUEST + 0x100, struct.pack("<II", RETURN, GUEST + 0x200))
        arm.mem_write(GUEST + 0x100, struct.pack("<II", RETURN, 0x200))
        x86.reg_write(UC_X86_REG_ESP, GUEST + 0x100)
        x86.emu_start(CODE, RETURN, count=64)
        arm.reg_write(UC_ARM64_REG_SP, ARM_STACK)
        arm.reg_write(UC_ARM64_REG_X0, GUEST)
        arm.reg_write(UC_ARM64_REG_X30, RETURN)
        arm.emu_start(entry, RETURN, count=1000)
        if x86.reg_read(UC_X86_REG_EIP) != RETURN or arm.reg_read(UC_ARM64_REG_PC) != RETURN:
            raise RuntimeError("leaf did not return")
        expected = x86.reg_read(UC_X86_REG_EAX) | ((x86.reg_read(UC_X86_REG_EFLAGS) & FLAGS) << 32)
        actual = arm.reg_read(UC_ARM64_REG_X0)
        if actual != expected:
            raise RuntimeError(f"{a:#x}, {b:#x}: x86={expected:#x} ARM64={actual:#x}")
    print(f"{len(inputs):,} cases matched: EAX and CF/PF/ZF/SF.")


def compare_startup(pe, arm, entry):
    base = pe.OPTIONAL_HEADER.ImageBase
    module_marker, crt_marker = 0x100000, 0x100010
    x86 = Uc(UC_ARCH_X86, UC_MODE_32)
    x86.mem_map(0, 0x800000)
    for rva, size in STARTUP:
        x86.mem_write(base + rva, pe.get_data(rva, size))

    headers = pe.__data__[:pe.OPTIONAL_HEADER.SizeOfHeaders]
    x86.mem_write(base, headers)
    arm.mem_write(GUEST + base, headers)
    x86.mem_write(0, struct.pack("<I", 0xFFFFFFFF))
    x86.mem_write(0x6A5090, struct.pack("<I", module_marker))
    x86.mem_write(0x6A5384, struct.pack("<I", crt_marker))
    x86.reg_write(UC_X86_REG_ESP, 0x90000)

    def snapshot(start, marker, wanted):
        x86.emu_start(start, marker, count=1000)
        if x86.reg_read(UC_X86_REG_EIP) != marker:
            raise RuntimeError(f"x86 startup stopped before marker {marker:#x}")
        stack = x86.reg_read(UC_X86_REG_ESP)
        return_address, argument = struct.unpack("<II", x86.mem_read(stack, 8))
        if (return_address, argument) != wanted:
            raise RuntimeError(f"unexpected x86 import arguments for marker {marker:#x}")
        return (marker, return_address, argument, stack, x86.reg_read(UC_X86_REG_EBP),
                struct.unpack("<I", x86.mem_read(0, 4))[0])

    module = snapshot(base + STARTUP[0][0], module_marker, (0x5AF5B8, 0))
    x86.reg_write(UC_X86_REG_EAX, base)
    x86.reg_write(UC_X86_REG_ESP, module[3] + 8)
    crt = snapshot(module[1], crt_marker, (0x5AF615, 2))

    arm.reg_write(UC_ARM64_REG_SP, ARM_STACK)
    arm.reg_write(UC_ARM64_REG_X0, GUEST)
    arm.reg_write(UC_ARM64_REG_X30, RETURN)
    arm.emu_start(entry, RETURN, count=1000)
    if arm.reg_read(UC_ARM64_REG_PC) != RETURN:
        raise RuntimeError("ARM64 startup did not complete the probe")
    actual = struct.unpack("<12I", arm.mem_read(GUEST + 0x800, 48))
    if actual != module + crt:
        raise RuntimeError(f"startup diverged: x86={module + crt}, ARM64={actual}")
    print("PE entry returned the module base and reached __set_app_type(2), "
          "matching x86 arguments, stack and SEH state.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("demo", type=Path)
    parser.add_argument("pcrecomp", type=Path)
    parser.add_argument("zig", type=Path)
    parser.add_argument("--work", type=Path, default=Path("build"))
    args = parser.parse_args()
    if hashlib.sha256(args.demo.read_bytes()).hexdigest() != DEMO_HASH:
        parser.error("expected the verified Windows demo executable")
    args.work.mkdir(parents=True, exist_ok=True)
    pe = pefile.PE(str(args.demo), fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase
    elf = generate(args, args.work, base, ((FUNCTION_RVA, FUNCTION_SIZE),), "leaf")
    arm, entry = arm_machine(elf, 0x1000)
    compare(pe.get_data(FUNCTION_RVA, FUNCTION_SIZE), arm, entry)
    elf = generate(args, args.work, base, STARTUP, "startup")
    arm, entry = arm_machine(elf, 0x800000)
    compare_startup(pe, arm, entry)


if __name__ == "__main__":
    main()
