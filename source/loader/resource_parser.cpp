#include "resource_parser.h"
#include <cstring>
#include <cctype>

namespace pe {

namespace {

#pragma pack(push, 1)
struct ResDirHeader {
    uint32_t characteristics;
    uint32_t timeDateStamp;
    uint16_t majorVersion;
    uint16_t minorVersion;
    uint16_t numberOfNamedEntries;
    uint16_t numberOfIdEntries;
};

struct ResDirEntry {
    uint32_t id;                 // нас интересуют только ID-энтри (не именованные)
    uint32_t offsetToDataOrDir;  // старший бит: 1 = ещё поддиректория, 0 = ResDataEntry
};

struct ResDataEntry {
    uint32_t offsetToData; // это уже АБСОЛЮТНЫЙ RVA (в отличие от офсетов в ResDirEntry!)
    uint32_t size;
    uint32_t codePage;
    uint32_t reserved;
};
#pragma pack(pop)

constexpr uint32_t SUBDIR_FLAG = 0x80000000u;

bool ReadStruct(const uint8_t* image, size_t imageSize, uint32_t rva, void* out, size_t size) {
    if (static_cast<size_t>(rva) + size > imageSize) return false;
    std::memcpy(out, image + rva, size);
    return true;
}

// Ищет ID-энтри с данным id внутри директории по абсолютному RVA dirAbsRva.
// Возвращает "сырой" offsetToDataOrDir (со старшим битом как есть), либо -1.
int64_t FindEntryInDirectory(const uint8_t* image, size_t imageSize,
                              uint32_t dirAbsRva, uint32_t wantedId) {
    ResDirHeader header;
    if (!ReadStruct(image, imageSize, dirAbsRva, &header, sizeof(header))) return -1;

    uint32_t entryArrayRva = dirAbsRva + sizeof(ResDirHeader);
    uint32_t totalEntries = static_cast<uint32_t>(header.numberOfNamedEntries) + header.numberOfIdEntries;

    // Именованные энтри идут первыми в массиве — пропускаем их, ищем
    // только среди ID-энтри (для типа/языка/блока строк нам нужны именно они)
    for (uint32_t i = header.numberOfNamedEntries; i < totalEntries; i++) {
        ResDirEntry entry;
        uint32_t entryRva = entryArrayRva + i * sizeof(ResDirEntry);
        if (!ReadStruct(image, imageSize, entryRva, &entry, sizeof(entry))) return -1;
        if (entry.id == wantedId) {
            return static_cast<int64_t>(entry.offsetToDataOrDir);
        }
    }
    return -1;
}

// Как FindEntryInDirectory, но ищет среди ИМЕНОВАННЫХ записей (первые
// numberOfNamedEntries в массиве) по строковому имени, а не числовому ID.
// Имя ресурса хранится как: [uint16 длина][UTF-16 символы, без null] —
// сравниваем побайтово, без учёта регистра (латиница, этого достаточно).
int64_t FindNamedEntryInDirectory(const uint8_t* image, size_t imageSize,
                                   uint32_t dirAbsRva, uint32_t resourceDirRva,
                                   const char* wantedName) {
    ResDirHeader header;
    if (!ReadStruct(image, imageSize, dirAbsRva, &header, sizeof(header))) return -1;

    uint32_t entryArrayRva = dirAbsRva + sizeof(ResDirHeader);
    size_t wantedLen = std::strlen(wantedName);

    for (uint32_t i = 0; i < header.numberOfNamedEntries; i++) {
        ResDirEntry entry;
        uint32_t entryRva = entryArrayRva + i * sizeof(ResDirEntry);
        if (!ReadStruct(image, imageSize, entryRva, &entry, sizeof(entry))) return -1;

        // entry.id тут на самом деле — RVA строки имени (со сброшенным старшим
        // битом "это имя, не число", который для именованных записей всегда 1)
        uint32_t nameRva = resourceDirRva + (entry.id & 0x7FFFFFFFu);
        uint16_t nameLen;
        if (!ReadStruct(image, imageSize, nameRva, &nameLen, sizeof(nameLen))) continue;
        if (nameLen != wantedLen) continue;

        bool match = true;
        for (uint16_t c = 0; c < nameLen; c++) {
            uint16_t ch;
            if (!ReadStruct(image, imageSize, nameRva + 2 + c * 2, &ch, sizeof(ch))) { match = false; break; }
            char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(ch & 0xFF)));
            char wantedLower = static_cast<char>(std::tolower(static_cast<unsigned char>(wantedName[c])));
            if (lower != wantedLower) { match = false; break; }
        }
        if (match) return static_cast<int64_t>(entry.offsetToDataOrDir);
    }
    return -1;
}

} // namespace

bool FindResourceGeneric(const uint8_t* image, size_t imageSize, uint32_t resourceDirRva,
                          uint16_t typeId, uint16_t nameId,
                          uint32_t& outDataRva, uint32_t& outSize) {
    if (resourceDirRva == 0) return false;

    // Level 1: тип ресурса
    int64_t typeOffset = FindEntryInDirectory(image, imageSize, resourceDirRva, typeId);
    if (typeOffset < 0 || !(static_cast<uint32_t>(typeOffset) & SUBDIR_FLAG)) return false;
    uint32_t typeDirRva = resourceDirRva + (static_cast<uint32_t>(typeOffset) & ~SUBDIR_FLAG);

    // Level 2: конкретный ресурс по числовому ID
    int64_t nameOffset = FindEntryInDirectory(image, imageSize, typeDirRva, nameId);
    if (nameOffset < 0 || !(static_cast<uint32_t>(nameOffset) & SUBDIR_FLAG)) return false;
    uint32_t nameDirRva = resourceDirRva + (static_cast<uint32_t>(nameOffset) & ~SUBDIR_FLAG);

    // Level 3: язык — берём первую доступную запись
    ResDirHeader langHeader;
    if (!ReadStruct(image, imageSize, nameDirRva, &langHeader, sizeof(langHeader))) return false;
    uint32_t langTotal = static_cast<uint32_t>(langHeader.numberOfNamedEntries) + langHeader.numberOfIdEntries;
    if (langTotal == 0) return false;

    ResDirEntry langEntry;
    uint32_t langEntryRva = nameDirRva + sizeof(ResDirHeader);
    if (!ReadStruct(image, imageSize, langEntryRva, &langEntry, sizeof(langEntry))) return false;
    if (langEntry.offsetToDataOrDir & SUBDIR_FLAG) return false; // тут уже должны быть данные

    uint32_t dataEntryRva = resourceDirRva + (langEntry.offsetToDataOrDir & ~SUBDIR_FLAG);
    ResDataEntry dataEntry;
    if (!ReadStruct(image, imageSize, dataEntryRva, &dataEntry, sizeof(dataEntry))) return false;

    outDataRva = dataEntry.offsetToData; // уже абсолютный RVA
    outSize = dataEntry.size;
    return true;
}

bool FindResourceByTypeName(const uint8_t* image, size_t imageSize, uint32_t resourceDirRva,
                             const char* typeName, uint16_t nameId,
                             uint32_t& outDataRva, uint32_t& outSize) {
    if (resourceDirRva == 0) return false;

    // Level 1: тип ресурса — по СТРОКОВОМУ имени (не числовому ID)
    int64_t typeOffset = FindNamedEntryInDirectory(image, imageSize, resourceDirRva, resourceDirRva, typeName);
    if (typeOffset < 0 || !(static_cast<uint32_t>(typeOffset) & SUBDIR_FLAG)) return false;
    uint32_t typeDirRva = resourceDirRva + (static_cast<uint32_t>(typeOffset) & ~SUBDIR_FLAG);

    // Level 2 и 3 — как в обычном FindResourceGeneric (числовой ID, потом язык)
    int64_t nameOffset = FindEntryInDirectory(image, imageSize, typeDirRva, nameId);
    if (nameOffset < 0 || !(static_cast<uint32_t>(nameOffset) & SUBDIR_FLAG)) return false;
    uint32_t nameDirRva = resourceDirRva + (static_cast<uint32_t>(nameOffset) & ~SUBDIR_FLAG);

    ResDirHeader langHeader;
    if (!ReadStruct(image, imageSize, nameDirRva, &langHeader, sizeof(langHeader))) return false;
    uint32_t langTotal = static_cast<uint32_t>(langHeader.numberOfNamedEntries) + langHeader.numberOfIdEntries;
    if (langTotal == 0) return false;

    ResDirEntry langEntry;
    uint32_t langEntryRva = nameDirRva + sizeof(ResDirHeader);
    if (!ReadStruct(image, imageSize, langEntryRva, &langEntry, sizeof(langEntry))) return false;
    if (langEntry.offsetToDataOrDir & SUBDIR_FLAG) return false;

    uint32_t dataEntryRva = resourceDirRva + (langEntry.offsetToDataOrDir & ~SUBDIR_FLAG);
    ResDataEntry dataEntry;
    if (!ReadStruct(image, imageSize, dataEntryRva, &dataEntry, sizeof(dataEntry))) return false;

    outDataRva = dataEntry.offsetToData;
    outSize = dataEntry.size;
    return true;
}

std::vector<uint16_t> FindStringResource(const uint8_t* image, size_t imageSize,
                                          uint32_t resourceDirRva, uint16_t stringId) {
    std::vector<uint16_t> empty;

    // Windows группирует строки блоками по 16 штук: блок номер (id/16)+1,
    // внутри блока — под-индекс id%16
    uint16_t blockId = static_cast<uint16_t>(stringId / 16 + 1);
    uint16_t subIndex = static_cast<uint16_t>(stringId % 16);

    uint32_t dataRva = 0, dataSize = 0;
    if (!FindResourceGeneric(image, imageSize, resourceDirRva, RT_STRING, blockId, dataRva, dataSize)) {
        return empty;
    }

    // Сам блок — 16 Pascal-строк подряд (uint16 длина + столько же UTF-16 символов,
    // без null-терминатора, следующая строка сразу за предыдущей)
    uint32_t cursor = dataRva;
    for (uint16_t i = 0; i < 16; i++) {
        uint16_t len;
        if (!ReadStruct(image, imageSize, cursor, &len, sizeof(len))) return empty;
        cursor += sizeof(uint16_t);

        if (i == subIndex) {
            if (len == 0) return empty; // строка с таким ID пуста/отсутствует
            std::vector<uint16_t> result(len);
            if (!ReadStruct(image, imageSize, cursor, result.data(), len * sizeof(uint16_t))) return empty;
            return result;
        }
        cursor += len * sizeof(uint16_t);
    }
    return empty;
}

} // namespace pe