"""Generate the tested PCRecomp startup functions from the publisher's PC demo."""
import argparse
import hashlib
import re
import subprocess
import sys
from pathlib import Path

DEMO_HASH = "69830e276dceeb1017be49a6193c6cfaa0905c6397b8aa9c2907e8707f4adce9"
ROUTINES = ((0x5AF59E, 119), (0x5AF84C, 59), (0x6660D0, 14))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exe", type=Path)
    parser.add_argument("pcrecomp", type=Path)
    args = parser.parse_args()
    if hashlib.sha256(args.exe.read_bytes()).hexdigest() != DEMO_HASH:
        parser.error("expected the original Windows demo FreakOut.exe")
    destination = Path(__file__).resolve().parent / "generated"
    destination.mkdir(exist_ok=True)
    bounds = destination / "functions.txt"
    bounds.write_text("".join(f"{address:#x} {size} selected\n" for address, size in ROUTINES),
                      encoding="ascii")
    lifter = args.pcrecomp / "tools/lift/lift32_cpu.py"
    subprocess.run([sys.executable, str(lifter), str(args.exe), str(bounds),
                    str(destination / "lifted.c"), *(hex(a) for a, _ in ROUTINES)], check=True)
    source = destination / "lifted.c"
    lifted = source.read_text(encoding="utf-8")
    include = '#include "cpu.h"'
    if lifted.count(include) != 1:
        raise RuntimeError("upstream generated include changed")
    source.write_text(lifted.replace(include, include + "\nvoid dispatch(CPU *, uint32_t);"),
                      encoding="utf-8")
    header = (args.pcrecomp / "runtime/recomp32_cpu/cpu.h").read_text(encoding="utf-8")
    portable, changes = re.subn(r"\(uintptr_t\)a\b", "(guest_memory + a)", header)
    if not changes:
        raise RuntimeError("upstream memory helpers changed")
    (destination / "cpu.h").write_text(
        "#include <stdint.h>\nextern uint8_t *guest_memory;\n" + portable, encoding="utf-8"
    )


if __name__ == "__main__":
    main()
