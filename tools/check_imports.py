import sys
import pefile

# Использование: python3 tools/check_imports.py path/to/file.exe|.dll
pe = pefile.PE(sys.argv[1] if len(sys.argv) > 1 else "winmine.exe")

print("=== Импорты (DLL и функции) ===")
for entry in pe.DIRECTORY_ENTRY_IMPORT:
    print(f"\n{entry.dll.decode()}:")
    for imp in entry.imports:
        name = imp.name.decode() if imp.name else f"ordinal_{imp.ordinal}"
        print(f"   {name}")

print("\n=== Entry Point ===")
print(hex(pe.OPTIONAL_HEADER.AddressOfEntryPoint))

print("\n=== Секции ===")
for section in pe.sections:
    print(f"{section.Name.decode().strip():10} "
          f"VirtAddr: {hex(section.VirtualAddress):10} "
          f"VirtSize: {hex(section.Misc_VirtualSize):10} "
          f"RawSize: {hex(section.SizeOfRawData)}")

print("\n=== Machine type ===")
print(hex(pe.FILE_HEADER.Machine), "(0x14c = x86 32-bit, 0x8664 = x64)")