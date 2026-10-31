#include "pe_parser.h"

#include <fstream>
#include <cstring>
#include <algorithm>
#include <cstddef>

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
    fileBuffer_.clear();sections_.clear();imports_.clear();
    dosHeader_={};fileHeader_={};optionalHeader_={};
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return LoadResult::FileNotFound;
    }

    std::streamsize size = file.tellg();
    if(size<0)return LoadResult::FileNotFound;
    if(uint64_t(size)>UINT32_MAX)return LoadResult::NotAValidPeHeader;
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
    if (uint64_t(ntHeaderOffset) + 4 + sizeof(FileHeader) > fileBuffer_.size()) {
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
    constexpr size_t prefix=offsetof(OptionalHeader32,dataDirectory);
    if (fileHeader_.sizeOfOptionalHeader<2 || uint64_t(optionalHeaderOffset)+fileHeader_.sizeOfOptionalHeader>fileBuffer_.size()) {
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

    if(fileHeader_.sizeOfOptionalHeader<prefix)return LoadResult::NotAValidPeHeader;
    std::memcpy(&optionalHeader_, fileBuffer_.data() + optionalHeaderOffset,
                std::min(size_t(fileHeader_.sizeOfOptionalHeader),sizeof(OptionalHeader32)));
    const auto directories=std::min(optionalHeader_.numberOfRvaAndSizes,16u);
    if(prefix+uint64_t(directories)*sizeof(DataDirectory)>fileHeader_.sizeOfOptionalHeader)return LoadResult::NotAValidPeHeader;

    // --- Секции ---
    uint32_t sectionHeaderOffset = optionalHeaderOffset + fileHeader_.sizeOfOptionalHeader;

    if(uint64_t(sectionHeaderOffset)+uint64_t(fileHeader_.numberOfSections)*sizeof(SectionHeader)>fileBuffer_.size())return LoadResult::NotAValidPeHeader;
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

    if(!ParseImportTable()){imports_.clear();return LoadResult::NotAValidPeHeader;}
    return LoadResult::Ok;
}

int64_t PEParser::RvaToFileOffset(uint32_t rva,size_t bytes) const {
    if(!bytes || rva>=optionalHeader_.sizeOfImage || bytes>uint64_t(optionalHeader_.sizeOfImage)-rva)return -1;
    if(rva<optionalHeader_.sizeOfHeaders){
        if(bytes>uint64_t(optionalHeader_.sizeOfHeaders)-rva || uint64_t(rva)+bytes>fileBuffer_.size())return -1;
        return rva;
    }
    for(const auto& section:sections_){
        if(rva<section.virtualAddress)continue;
        const uint64_t delta=uint64_t(rva)-section.virtualAddress;
        const uint64_t extent=std::max(section.virtualSize,section.rawDataSize);
        if(delta>=extent || bytes>extent-delta)continue;
        // Old linkers can set VirtualSize=0. Raw bytes still back the section,
        // but a virtual-only tail or uninitialized zero-pointer section has no file bytes.
        if((!section.rawDataPointer && (section.characteristics&0x80)) || delta>=section.rawDataSize || bytes>uint64_t(section.rawDataSize)-delta)return -1;
        const uint64_t offset=uint64_t(section.rawDataPointer)+delta;
        if(offset>fileBuffer_.size() || bytes>fileBuffer_.size()-offset)return -1;
        return int64_t(offset);
    }
    return -1;
}

bool PEParser::ReadCStringAtRva(uint32_t rva,std::string& out) const {
    out.clear();
    for(unsigned n=0;n<4096;++n){
        const uint64_t at=uint64_t(rva)+n;if(at>UINT32_MAX)return false;
        const auto offset=RvaToFileOffset(uint32_t(at));if(offset<0)return false;
        const auto ch=fileBuffer_[size_t(offset)];if(!ch)return true;out.push_back(char(ch));
    }
    return false;
}

bool PEParser::ParseImportTable() {
    imports_.clear();
    if(optionalHeader_.numberOfRvaAndSizes<2)return true;
    const auto dir=optionalHeader_.dataDirectory[1];
    if(!dir.virtualAddress && !dir.size)return true;
    if(!dir.virtualAddress || dir.size<sizeof(ImportDescriptor) || uint64_t(dir.virtualAddress)+dir.size>optionalHeader_.sizeOfImage)return false;
    size_t symbols=0,nameBytes=0;
    for(size_t index=0;index<=1024;++index){
        const uint64_t delta=index*sizeof(ImportDescriptor),rva=uint64_t(dir.virtualAddress)+delta;
        if(delta+sizeof(ImportDescriptor)>dir.size || rva>UINT32_MAX)return false;
        const auto offset=RvaToFileOffset(uint32_t(rva),sizeof(ImportDescriptor));if(offset<0)return false;
        ImportDescriptor desc{};std::memcpy(&desc,fileBuffer_.data()+offset,sizeof(desc));
        if(!desc.originalFirstThunk && !desc.timeDateStamp && !desc.forwarderChain && !desc.name && !desc.firstThunk)return true;
        if(index==1024 || !desc.name || !desc.firstThunk)return false;
        ImportedModule module;if(!ReadCStringAtRva(desc.name,module.dllName) || module.dllName.empty())return false;
        nameBytes+=module.dllName.size();if(nameBytes>1024*1024)return false;
        const auto thunk=desc.originalFirstThunk?desc.originalFirstThunk:desc.firstThunk;
        for(size_t item=0;;++item){
            const uint64_t at=uint64_t(thunk)+item*4,iat=uint64_t(desc.firstThunk)+item*4;
            if(at>UINT32_MAX || iat+4>optionalHeader_.sizeOfImage)return false;
            const auto off=RvaToFileOffset(uint32_t(at),4);if(off<0)return false;
            uint32_t value=0;std::memcpy(&value,fileBuffer_.data()+off,4);if(!value)break;
            if(symbols++>=65536)return false;
            ImportedFunction function;function.iatRva=uint32_t(iat);
            if(value&0x80000000u){if(value&0x7fff0000u)return false;function.ordinal=uint16_t(value);}
            else {
                if(value>UINT32_MAX-2 || RvaToFileOffset(value,2)<0 ||
                   !ReadCStringAtRva(value+2,function.name) || function.name.empty())return false;
            }
            nameBytes+=function.name.size();if(nameBytes>1024*1024)return false;
            module.functions.push_back(std::move(function));
        }
        imports_.push_back(std::move(module));
    }
    return false;
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
        if (s.rawDataSize == 0 || (!s.rawDataPointer && (s.characteristics&0x80))) continue; // например .bss-подобные секции без данных на диске
        if (uint64_t(s.rawDataPointer) + s.rawDataSize > fileBuffer_.size()) continue;
        if (uint64_t(s.virtualAddress) + s.rawDataSize > std::min(uint64_t(destSize),uint64_t(optionalHeader_.sizeOfImage))) continue;

        std::memcpy(dest + s.virtualAddress,
                    fileBuffer_.data() + s.rawDataPointer,
                    s.rawDataSize);
    }
}

} // namespace pe
