"""Translate and check a small, explicitly supported x86 cdecl function."""
import random
import struct

from capstone import CS_ARCH_X86, CS_MODE_32, Cs
from capstone.x86_const import X86_OP_IMM, X86_OP_MEM, X86_OP_REG
from keystone import KS_ARCH_ARM64, KS_ARCH_X86, KS_MODE_32, KS_MODE_LITTLE_ENDIAN, Ks
from unicorn import (
    UC_ARCH_ARM64, UC_ARCH_X86, UC_MODE_32, UC_MODE_ARM,
    UC_PROT_ALL, Uc,
)
from unicorn.arm64_const import UC_ARM64_REG_PC, UC_ARM64_REG_W0, UC_ARM64_REG_W1, UC_ARM64_REG_X30
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP

BASE = 0x10000
RETURN = 0x20000
STACK = 0x31000
MASK = 0xFFFFFFFF
REGISTERS = {"eax": "w0", "ecx": "w2", "edx": "w3"}
CONDITIONS = {"jae": "hs", "jb": "lo", "je": "eq", "jne": "ne", "jge": "ge", "jl": "lt"}

SAMPLE = """
    mov eax, dword ptr [esp + 4]
    mov ecx, dword ptr [esp + 8]
    cmp eax, ecx
    jae keep
    add eax, ecx
    ret
keep:
    sub eax, ecx
    ret
"""


def assemble(arch, mode, source):
    encoding, _ = Ks(arch, mode).asm(source)
    return bytes(encoding)


def lift(code):
    decoder = Cs(CS_ARCH_X86, CS_MODE_32)
    decoder.detail = True
    instructions = list(decoder.disasm(code, BASE))
    if sum(instruction.size for instruction in instructions) != len(code):
        raise ValueError("the function contains undecodable bytes")

    if not instructions or instructions[-1].mnemonic != "ret":
        raise ValueError("expected a closed function ending in ret")
    addresses = {instruction.address for instruction in instructions}
    targets = {
        instruction.operands[0].imm
        for instruction in instructions
        if instruction.mnemonic in CONDITIONS and len(instruction.operands) == 1
        and instruction.operands[0].type == X86_OP_IMM
    }
    lines = ["mov w4, w0", "mov w5, w1"]  # Preserve the two incoming arguments.
    for index, instruction in enumerate(instructions):
        name = instruction.mnemonic
        operands = instruction.operands

        def register(operand):
            if operand.type != X86_OP_REG or instruction.reg_name(operand.reg) not in REGISTERS:
                raise ValueError(f"unsupported register at {instruction.address:#x}")
            return REGISTERS[instruction.reg_name(operand.reg)]

        lines.append(f"L{instruction.address:x}:")
        if name == "ret" and not operands:
            lines.append("ret")
        elif name == "mov" and len(operands) == 2:
            destination = register(operands[0])
            source = operands[1]
            if source.type == X86_OP_MEM:
                memory = source.mem
                if (instruction.reg_name(memory.base) != "esp" or memory.index or memory.segment
                        or memory.disp not in (4, 8) or source.size != 4):
                    raise ValueError("only reads of the two 32-bit cdecl arguments are supported")
                value = {4: "w4", 8: "w5"}[memory.disp]
            else:
                value = register(source)
            lines.append(f"mov {destination}, {value}")
        elif name in ("cmp", "add", "sub") and len(operands) == 2:
            left, right = map(register, operands)
            if name == "cmp":
                lines.append(f"cmp {left}, {right}")
            else:
                if index + 1 == len(instructions) or instructions[index + 1].mnemonic != "ret":
                    raise ValueError("arithmetic flags are supported only when unused before ret")
                lines.append(f"{name} {left}, {left}, {right}")
        elif name in CONDITIONS and len(operands) == 1 and operands[0].type == X86_OP_IMM:
            if index == 0 or instructions[index - 1].mnemonic != "cmp" or instruction.address in targets:
                raise ValueError("conditional branch must be reached through its preceding cmp")
            target = operands[0].imm
            if target not in addresses:
                raise ValueError(f"branch leaves the function at {instruction.address:#x}")
            lines.append(f"b.{CONDITIONS[name]} L{target:x}")
        else:
            raise ValueError(f"unsupported instruction: {name} {instruction.op_str}")
    return "\n".join(lines)


def machine(arch, mode, code):
    emulator = Uc(arch, mode)
    emulator.mem_map(BASE, 0x30000, UC_PROT_ALL)
    emulator.mem_write(BASE, code)
    return emulator


def execute_pair(x86, arm, lhs, rhs):
    x86.mem_write(STACK, struct.pack("<III", RETURN, lhs, rhs))
    x86.reg_write(UC_X86_REG_ESP, STACK)
    x86.emu_start(BASE, RETURN, timeout=100_000, count=64)
    arm.reg_write(UC_ARM64_REG_X30, RETURN)
    arm.reg_write(UC_ARM64_REG_W0, lhs)
    arm.reg_write(UC_ARM64_REG_W1, rhs)
    arm.emu_start(BASE, RETURN, timeout=100_000, count=64)
    if x86.reg_read(UC_X86_REG_EIP) != RETURN or arm.reg_read(UC_ARM64_REG_PC) != RETURN:
        raise RuntimeError("a function did not reach its return address")
    return x86.reg_read(UC_X86_REG_EAX), arm.reg_read(UC_ARM64_REG_W0)


def check_program(source, inputs, expected):
    x86_code = assemble(KS_ARCH_X86, KS_MODE_32, source)
    arm_assembly = lift(x86_code)
    arm_code = assemble(KS_ARCH_ARM64, KS_MODE_LITTLE_ENDIAN, arm_assembly)
    x86 = machine(UC_ARCH_X86, UC_MODE_32, x86_code)
    arm = machine(UC_ARCH_ARM64, UC_MODE_ARM, arm_code)
    for a, b in inputs:
        answer = expected(a, b)
        actual = execute_pair(x86, arm, a, b)
        if actual != (answer, answer):
            raise RuntimeError(f"{a:#x}, {b:#x}: x86/ARM64={actual}, expected={answer}")
    print(arm_assembly)
    print(f"{len(inputs):,} inputs matched on x86 and ARM64 ({len(x86_code)} → {len(arm_code)} bytes)")


def signed(value):
    return value if value < 2**31 else value - 2**32


def main():
    edges = (0, 1, 0x7FFFFFFF, 0x80000000, MASK - 1, MASK)
    rng = random.Random(304540)
    inputs = [(a, b) for a in edges for b in edges]
    inputs.extend((rng.getrandbits(32), rng.getrandbits(32)) for _ in range(10_000))
    check_program(SAMPLE, inputs, lambda a, b: (a - b if a >= b else a + b) & MASK)
    check_program(SAMPLE.replace("jae", "jge"), inputs,
                  lambda a, b: (a - b if signed(a) >= signed(b) else a + b) & MASK)


if __name__ == "__main__":
    main()
