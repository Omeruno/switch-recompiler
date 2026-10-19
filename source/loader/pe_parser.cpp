#include "pe_parser.h"

#include <fstream>
#include <cstring>

namespace pe {

const char* ToString(LoadResult result) {
    switch (result) {
        case LoadResult::Ok:                  return "Ok";
        case LoadResult::FileNotFound:        return "FileNotFound";
        case LoadResult::NotAValidDosHeader:  return "NotAValidDosHeader (нет сигнатуры MZ)";
        case LoadResult::NotAValidPeHeader:   return "NotAValidPeHeader (нет сигнатуры PE\\0\\0)";
        case LoadResult::Not32BitPe:          return "Not32BitPe (это PE32+ / x64, мы поддерживаем только x86-32)";
        case LoadResult::UnsupportedMachine:  return "UnsupportedMachine (machine type != 0x14c)";
    }
    return "Unknown";
}

LoadResult PEParser::LoadFromFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return LoadResult::FileNotFound;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    fileBuffer_.resize(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(fileBuffer_.data()), size)) {
        return LoadResult::FileNotFound;
    }

    // --- DOS-заголовок ---
    if (fileBuffer_.size() < sizeof(DosHeader)) {
        return LoadResult::NotAValidDosHeader;
    }
    std::memcpy(&dosHeader_, fileBuffer_.data(), sizeof(DosHeader));

    if (dosHeader_.e_magic != 0x5A4D) { // "MZ"
        return LoadResult::NotAValidDosHeader;
    }

    // --- NT-заголовок ("PE\0\0" + FileHeader) ---
    uint32_t ntHeaderOffset = static_cast<uint32_t>(dosHeader_.e_lfanew);
    if (ntHeaderOffset + 4 + sizeof(FileHeader) > fileBuffer_.size()) {
        return LoadResult::NotAValidPeHeader;
    }

    uint32_t peSignature = 0;
    std::memcpy(&peSignature, fileBuffer_.data() + ntHeaderOffset, 4);
    if (peSignature != 0x00004550) { // "PE\0\0" в little-endian как uint32
        return LoadResult::NotAValidPeHeader;
    }

    uint32_t fileHeaderOffset = ntHeaderOffset + 4;
    std::memcpy(&fileHeader_, fileBuffer_.data() + fileHeaderOffset, sizeof(FileHeader));

    if (fileHeader_.machine != 0x14c) {
        return LoadResult::UnsupportedMachine;
    }

    // --- Optional Header ---
    uint32_t optionalHeaderOffset = fileHeaderOffset + sizeof(FileHeader);
    if (optionalHeaderOffset + sizeof(OptionalHeader32) > fileBuffer_.size()) {
        return LoadResult::NotAValidPeHeader;
    }

    uint16_t magic = 0;
    std::memcpy(&magic, fileBuffer_.data() + optionalHeaderOffset, 2);
    if (magic == 0x20b) {
        return LoadResult::Not32BitPe; // PE32+ (x64) — не наш случай
    }
    if (magic != 0x10b) {
        return LoadResult::NotAValidPeHeader;
    }

    std::memcpy(&optionalHeader_, fileBuffer_.data() + optionalHeaderOffset, sizeof(OptionalHeader32));

    // --- Секции ---
    uint32_t sectionHeaderOffset = optionalHeaderOffset + fileHeader_.sizeOfOptionalHeader;

    sections_.clear();
    sections_.reserve(fileHeader_.numberOfSections);

    for (uint16_t i = 0; i < fileHeader_.numberOfSections; ++i) {
        uint32_t off = sectionHeaderOffset + i * sizeof(SectionHeader);
        if (off + sizeof(SectionHeader) > fileBuffer_.size()) break;

        SectionHeader raw{};
        std::memcpy(&raw, fileBuffer_.data() + off, sizeof(SectionHeader));

        Section s;
        // name[8] не гарантированно нуль-терминирована, режем аккуратно
        char nameBuf[9] = {0};
        std::memcpy(nameBuf, raw.name, 8);
        s.name = std::string(nameBuf);
        s.virtualAddress   = raw.virtualAddress;
        s.virtualSize      = raw.virtualSize;
        s.rawDataPointer   = raw.pointerToRawData;
        s.rawDataSize      = raw.sizeOfRawData;
        s.characteristics  = raw.characteristics;

        sections_.push_back(s);
    }

    ParseImportTable();

    return LoadResult::Ok;
}

int64_t PEParser::RvaToFileOffset(uint32_t rva) const {
    for (const auto& s : sections_) {
        // Секция покрывает диапазон [virtualAddress, virtualAddress + virtualSize)
        if (rva >= s.virtualAddress && rva < s.virtualAddress + s.virtualSize) {
            uint32_t deltaIntoSection = rva - s.virtualAddress;
            return static_cast<int64_t>(s.rawDataPointer) + deltaIntoSection;
        }
    }
    return -1;
}

std::string PEParser::ReadCStringAtFileOffset(uint32_t fileOffset) const {
    std::string result;
    if (fileOffset >= fileBuffer_.size()) return result;

    size_t i = fileOffset;
    while (i < fileBuffer_.size() && fileBuffer_[i] != 0) {
        result.push_back(static_cast<char>(fileBuffer_[i]));
        ++i;
    }
    return result;
}

void PEParser::ParseImportTable() {
    imports_.clear();

    // DataDirectory[1] — Import Table
    if (optionalHeader_.numberOfRvaAndSizes < 2) return;
    const DataDirectory& importDir = optionalHeader_.dataDirectory[1];
    if (importDir.virtualAddress == 0 || importDir.size == 0) return;

    int64_t descriptorFileOffset = RvaToFileOffset(importDir.virtualAddress);
    if (descriptorFileOffset < 0) return;

    size_t idx = 0;
    while (true) {
        uint32_t off = static_cast<uint32_t>(descriptorFileOffset) + idx * sizeof(ImportDescriptor);
        if (off + sizeof(ImportDescriptor) > fileBuffer_.size()) break;

        ImportDescriptor desc{};
        std::memcpy(&desc, fileBuffer_.data() + off, sizeof(ImportDescriptor));

        // Массив дескрипторов заканчивается нулевой записью
        if (desc.name == 0 && desc.originalFirstThunk == 0 && desc.firstThunk == 0) {
            break;
        }

        ImportedModule module;

        int64_t nameOffset = RvaToFileOffset(desc.name);
        if (nameOffset >= 0) {
            module.dllName = ReadCStringAtFileOffset(static_cast<uint32_t>(nameOffset));
        }

        // Читаем thunk-таблицу. Предпочитаем originalFirstThunk (ILT),
        // но некоторые старые линкеры кладут 0 туда — тогда fallback на firstThunk (IAT).
        uint32_t thunkRva = desc.originalFirstThunk != 0 ? desc.originalFirstThunk : desc.firstThunk;
        int64_t thunkFileOffset = RvaToFileOffset(thunkRva);

        if (thunkFileOffset >= 0) {
            size_t thunkIdx = 0;
            while (true) {
                uint32_t thunkOff = static_cast<uint32_t>(thunkFileOffset) + thunkIdx * sizeof(uint32_t);
                if (thunkOff + sizeof(uint32_t) > fileBuffer_.size()) break;

                uint32_t thunkValue = 0;
                std::memcpy(&thunkValue, fileBuffer_.data() + thunkOff, sizeof(uint32_t));
                if (thunkValue == 0) break; // конец списка функций этой DLL

                ImportedFunction func;
                // Старший бит = 1 значит импорт по ординалу, а не по имени
                constexpr uint32_t ORDINAL_FLAG = 0x80000000;
                if (thunkValue & ORDINAL_FLAG) {
                    func.name = "";
                    func.ordinal = static_cast<uint16_t>(thunkValue & 0xFFFF);
                } else {
                    // thunkValue — это RVA на IMAGE_IMPORT_BY_NAME:
                    // структура {uint16_t Hint; char Name[];}
                    int64_t byNameOffset = RvaToFileOffset(thunkValue);
                    if (byNameOffset >= 0) {
                        // Пропускаем 2 байта Hint, дальше сама строка имени
                        func.name = ReadCStringAtFileOffset(static_cast<uint32_t>(byNameOffset) + 2);
                    }
                    func.ordinal = 0;
                }
                func.iatRva = desc.firstThunk + static_cast<uint32_t>(thunkIdx * sizeof(uint32_t));

                module.functions.push_back(func);
                ++thunkIdx;
            }
        }

        imports_.push_back(module);
        ++idx;
    }
}

void PEParser::MapImageToMemory(uint8_t* dest, size_t destSize) const {
    if (destSize < optionalHeader_.sizeOfImage) return;
    std::memset(dest, 0, destSize);

    // Заголовки (всё до первой секции) обычно тоже мапятся как есть
    uint32_t headersSize = optionalHeader_.sizeOfHeaders;
    if (headersSize <= fileBuffer_.size() && headersSize <= destSize) {
        std::memcpy(dest, fileBuffer_.data(), headersSize);
    }

    for (const auto& s : sections_) {
        if (s.rawDataSize == 0) continue; // например .bss-подобные секции без данных на диске
        if (s.rawDataPointer + s.rawDataSize > fileBuffer_.size()) continue;
        if (s.virtualAddress + s.rawDataSize > destSize) continue;

        std::memcpy(dest + s.virtualAddress,
                    fileBuffer_.data() + s.rawDataPointer,
                    s.rawDataSize);
    }
}

} // namespace pe
