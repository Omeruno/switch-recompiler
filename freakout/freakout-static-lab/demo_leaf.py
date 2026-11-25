"""Run an isolated leaf function from the verified Windows demo in both ISAs."""
import argparse
import random
import struct
from pathlib import Path

from keystone import KS_ARCH_ARM64, KS_MODE_LITTLE_ENDIAN
from lift_check import (
    BASE, RETURN, STACK, MASK, Cs, CS_ARCH_X86, CS_MODE_32,
    UC_ARCH_ARM64, UC_ARCH_X86, UC_MODE_32, UC_MODE_ARM,
    UC_ARM64_REG_PC, UC_ARM64_REG_W0, UC_ARM64_REG_X30,
    UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP,
    assemble, machine, signed,
)
from probe import inspect

DEMO_SHA256 = "69830e276dceeb1017be49a6193c6cfaa0905c6397b8aa9c2907e8707f4adce9"
FUNCTION_RVA = 0x2660D0
OBJECT = 0x33000
PATTERN = [
    ("mov", "ecx, dword ptr [esp + 4]"),
    ("mov", "eax, dword ptr [ecx + 8]"),
    ("sub", "eax, dword ptr [ecx + 0xc]"),
    ("sar", "eax, 4"),
    ("ret", ""),
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    args = parser.parse_args()
    metadata = inspect(args.executable)
    if metadata["sha256"] != DEMO_SHA256:
        parser.error("the function address is defined for the verified Windows demo only")
    section = next(section for section in metadata["sections"] if
                   section["rva"] <= FUNCTION_RVA < section["rva"] + section["raw_size"])
    offset = section["raw_offset"] + FUNCTION_RVA - section["rva"]
    source = args.executable.read_bytes()[offset:offset + 14]
    decoder = Cs(CS_ARCH_X86, CS_MODE_32)
    instructions = [(name, operands) for _, _, name, operands in decoder.disasm_lite(source, BASE)]
    if instructions != PATTERN:
        raise RuntimeError("the source no longer matches the meaning of the translation")

    target = assemble(KS_ARCH_ARM64, KS_MODE_LITTLE_ENDIAN, """
        ldr w1, [x0, #8]
        ldr w2, [x0, #12]
        sub w1, w1, w2
        asr w0, w1, #4
        ret
    """)
    x86 = machine(UC_ARCH_X86, UC_MODE_32, source)
    arm = machine(UC_ARCH_ARM64, UC_MODE_ARM, target)
    edges = (0, 1, 0x7FFFFFFF, 0x80000000, MASK - 1, MASK)
    rng = random.Random(FUNCTION_RVA)
    inputs = [(a, b) for a in edges for b in edges]
    inputs.extend((rng.getrandbits(32), rng.getrandbits(32)) for _ in range(10_000))

    for a, b in inputs:
        fields = struct.pack("<II", a, b)
        x86.mem_write(OBJECT + 8, fields)
        arm.mem_write(OBJECT + 8, fields)
        x86.mem_write(STACK, struct.pack("<II", RETURN, OBJECT))
        x86.reg_write(UC_X86_REG_ESP, STACK)
        arm.reg_write(UC_ARM64_REG_X30, RETURN)
        arm.reg_write(UC_ARM64_REG_W0, OBJECT)
        x86.emu_start(BASE, RETURN, count=16)
        arm.emu_start(BASE, RETURN, count=16)
        if x86.reg_read(UC_X86_REG_EIP) != RETURN or arm.reg_read(UC_ARM64_REG_PC) != RETURN:
            raise RuntimeError("a function did not reach its return address")
        actual = x86.reg_read(UC_X86_REG_EAX), arm.reg_read(UC_ARM64_REG_W0)
        expected = (signed((a - b) & MASK) >> 4) & MASK
        if actual != (expected, expected):
            raise RuntimeError(f"{a:#x}, {b:#x}: x86/ARM64={actual}, expected={expected}")

    print(f"Demo RVA {FUNCTION_RVA:#x}: {len(inputs):,} inputs matched on x86 and ARM64.")


if __name__ == "__main__":
    main()
