"""Read PE metadata from a local installation without loading its binaries."""
import argparse
import hashlib
import json
import struct
from pathlib import Path


DIRECTORIES = {0: "exports", 1: "imports", 3: "exceptions", 5: "relocations",
               9: "tls", 13: "delay_imports", 14: "clr"}
MACHINES = {0x14C: "x86", 0x8664: "x86-64", 0xAA64: "ARM64"}


def inspect(path):
    data = path.read_bytes()

    def unpack(fmt, offset):
        return struct.unpack_from("<" + fmt, data, offset)

    if data[:2] != b"MZ":
        raise ValueError("missing MZ signature")
    pe_offset, = unpack("I", 0x3C)
    if data[pe_offset:pe_offset + 4] != b"PE\0\0":
        raise ValueError("missing PE signature")
    machine, section_count, _, _, _, optional_size, flags = unpack("HHIIIHH", pe_offset + 4)
    optional = pe_offset + 24
    magic, = unpack("H", optional)
    if magic not in (0x10B, 0x20B):
        raise ValueError(f"unsupported PE magic {magic:#x}")
    wide = magic == 0x20B
    ptr = "Q" if wide else "I"
    ptr_size = struct.calcsize(ptr)
    directory_offset = 112 if wide else 96
    if optional_size < directory_offset or optional + optional_size > len(data):
        raise ValueError("truncated optional header")
    entry, = unpack("I", optional + 16)
    base, = unpack(ptr, optional + (24 if wide else 28))
    count, = unpack("I", optional + directory_offset - 4)
    count = min(count, (optional_size - directory_offset) // 8)
    directories = {
        name: dict(zip(("rva", "size"), unpack("II", optional + directory_offset + index * 8)))
        for index, name in DIRECTORIES.items() if index < count
    }

    sections = []
    for index in range(section_count):
        header = optional + optional_size + index * 40
        name, virtual_size, rva, raw_size, raw_offset, _, _, _, _, characteristics = unpack(
            "8sIIIIIIHHI", header
        )
        sections.append({
            "name": name.rstrip(b"\0").decode("ascii", "replace"),
            "rva": rva, "virtual_size": virtual_size, "raw_offset": raw_offset,
            "raw_size": raw_size, "executable": bool(characteristics & 0x20000000),
        })

    header_size, = unpack("I", optional + 60)

    def offset(rva, size):
        if 0 <= rva < header_size and rva + size <= min(header_size, len(data)):
            return rva
        for section in sections:
            delta = rva - section["rva"]
            raw = section["raw_offset"] + delta
            if 0 <= delta and delta + size <= section["raw_size"] and raw + size <= len(data):
                return raw
        raise ValueError(f"unmapped RVA {rva:#x}")

    def string(rva):
        start = offset(rva, 1)
        end = data.find(b"\0", start, start + 4096)
        if end < 0:
            raise ValueError(f"unterminated string at RVA {rva:#x}")
        return data[start:end].decode("ascii", "replace")

    def symbols(table):
        result = []
        ordinal_bit = 1 << (ptr_size * 8 - 1)
        while True:
            value, = unpack(ptr, offset(table, ptr_size))
            if value == 0:
                return result
            result.append(f"#{value & 0xFFFF}" if value & ordinal_bit else string(value + 2))
            table += ptr_size

    def imports(key, delayed=False):
        directory = directories.get(key, {})
        start = directory.get("rva", 0)
        size = directory.get("size", 0)
        if not start or not size:
            return []
        fmt = "8I" if delayed else "5I"
        stride = struct.calcsize(fmt)
        result = []
        # The terminator may sit just past the counted descriptors.
        for relative in range(0, size + 1, stride):
            fields = unpack(fmt, offset(start + relative, stride))
            if not any(fields):
                return result
            if relative + stride > size:
                break
            if delayed:
                attrs, name, _, iat, table, *_ = fields
                if not attrs & 1:
                    name -= base
                    table = (table or iat) - base
                else:
                    table = table or iat
            else:
                table, _, _, name, iat = fields
                table = table or iat
            result.append({"dll": string(name), "symbols": symbols(table)})
        raise ValueError(f"missing terminator in {key}")

    return {
        "sha256": hashlib.sha256(data).hexdigest(), "file_size": len(data),
        "format": "PE32+" if wide else "PE32",
        "machine": MACHINES.get(machine, hex(machine)), "image_base": base,
        "entry_rva": entry, "relocations_stripped": bool(flags & 1),
        "sections": sections, "directories": directories,
        "imports": imports("imports"), "delay_imports": imports("delay_imports", delayed=True),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("installation", type=Path, help="game directory or path to FreakOut.exe")
    parser.add_argument("--out", type=Path, default=Path("freakout-pe.json"))
    args = parser.parse_args()
    requested = args.installation
    root = requested if requested.is_dir() else requested.parent
    if not root.is_dir():
        parser.error(f"folder does not exist: {root}")
    modules = sorted(root.rglob("*"), key=lambda path: (str(path).casefold(), str(path)))
    modules = [path for path in modules if path.is_file() and path.suffix.lower() in {".exe", ".dll"}]
    if requested.is_dir():
        candidates = [path for path in modules if path.name.lower() == "freakout.exe"]
        if len(candidates) != 1:
            parser.error(f"expected one FreakOut.exe, found {len(candidates)}. Pass the exact EXE path.")
        executable = candidates[0]
    else:
        executable = requested
        if executable not in modules or executable.suffix.lower() != ".exe":
            parser.error(f"EXE not found: {executable}")

    report = {"entry_module": executable.relative_to(root).as_posix(), "modules": {}, "errors": {}}
    for module in modules:
        name = module.relative_to(root).as_posix()
        try:
            report["modules"][name] = inspect(module)
        except (OSError, ValueError, struct.error) as exc:
            report["errors"][name] = str(exc)

    if report["entry_module"] not in report["modules"]:
        parser.error(report["errors"][report["entry_module"]])
    if args.out.exists() and any(args.out.samefile(module) for module in modules):
        parser.error("report must not overwrite an EXE or DLL")
    args.out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"{args.out}: {len(report['modules'])} PE modules, {len(report['errors'])} unreadable")


if __name__ == "__main__":
    main()
