#pragma once
// pe_parser.h
//
// Минимальный, но настоящий парсер PE32-файлов (32-бит Windows .exe).
// Разбирает: DOS-заголовок, NT-заголовки, секции, таблицу импортов.
//
// Специально НЕ зависит от <windows.h> и НЕ зависит от libnx —
// это чистый portable C++, чтобы можно было тестировать логику
// на обычном ПК (host), а потом использовать тот же файл на Switch.

#include <cstdint>
#include <string>
#include <vector>
#include <map>

namespace pe {

// ---------------------------------------------------------------------------
// Структуры формата PE (packed — важно, реальный формат не выровнен по C++)
// ---------------------------------------------------------------------------

#pragma pack(push, 1)

// DOS-заголовок — первые байты любого .exe, начинаются с "MZ"
struct DosHeader {
    uint16_t e_magic;      // "MZ" == 0x5A4D
    uint16_t e_cblp;
    uint16_t e_cp;
    uint16_t e_crlc;
    uint16_t e_cparhdr;
    uint16_t e_minalloc;
    uint16_t e_maxalloc;
    uint16_t e_ss;
    uint16_t e_sp;
    uint16_t e_csum;
    uint16_t e_ip;
    uint16_t e_cs;
    uint16_t e_lfarlc;
    uint16_t e_ovno;
    uint16_t e_res[4];
    uint16_t e_oemid;
    uint16_t e_oeminfo;
    uint16_t e_res2[10];
    int32_t  e_lfanew;     // смещение до NT-заголовка (PE\0\0)
};

// COFF File Header (сразу после сигнатуры "PE\0\0")
struct FileHeader {
    uint16_t machine;              // 0x14c = x86 32-bit, 0x8664 = x64
    uint16_t numberOfSections;
    uint32_t timeDateStamp;
    uint32_t pointerToSymbolTable;
    uint32_t numberOfSymbols;
    uint16_t sizeOfOptionalHeader;
    uint16_t characteristics;
};

struct DataDirectory {
    uint32_t virtualAddress;
    uint32_t size;
};

// Optional Header для PE32 (не PE32+/x64 — у нас 32-битный winmine.exe)
struct OptionalHeader32 {
    uint16_t magic;                 // 0x10b = PE32, 0x20b = PE32+
    uint8_t  majorLinkerVersion;
    uint8_t  minorLinkerVersion;
    uint32_t sizeOfCode;
    uint32_t sizeOfInitializedData;
    uint32_t sizeOfUninitializedData;
    uint32_t addressOfEntryPoint;    // RVA точки входа
    uint32_t baseOfCode;
    uint32_t baseOfData;             // только в PE32, отсутствует в PE32+
    uint32_t imageBase;
    uint32_t sectionAlignment;
    uint32_t fileAlignment;
    uint16_t majorOSVersion;
    uint16_t minorOSVersion;
    uint16_t majorImageVersion;
    uint16_t minorImageVersion;
    uint16_t majorSubsystemVersion;
    uint16_t minorSubsystemVersion;
    uint32_t win32VersionValue;
    uint32_t sizeOfImage;
    uint32_t sizeOfHeaders;
    uint32_t checkSum;
    uint16_t subsystem;
    uint16_t dllCharacteristics;
    uint32_t sizeOfStackReserve;
    uint32_t sizeOfStackCommit;
    uint32_t sizeOfHeapReserve;
    uint32_t sizeOfHeapCommit;
    uint32_t loaderFlags;
    uint32_t numberOfRvaAndSizes;
    DataDirectory dataDirectory[16]; // Export, Import, Resource, ... (индекс 1 = Import, 2 = Resource)
};

struct SectionHeader {
    char     name[8];                // не всегда нуль-терминирована!
    uint32_t virtualSize;
    uint32_t virtualAddress;
    uint32_t sizeOfRawData;
    uint32_t pointerToRawData;
    uint32_t pointerToRelocations;
    uint32_t pointerToLinenumbers;
    uint16_t numberOfRelocations;
    uint16_t numberOfLinenumbers;
    uint32_t characteristics;
};

// Import Directory Table — один элемент на каждую импортируемую DLL
struct ImportDescriptor {
    uint32_t originalFirstThunk; // RVA на Import Lookup Table (имена/ординалы)
    uint32_t timeDateStamp;
    uint32_t forwarderChain;
    uint32_t name;                // RVA на строку с именем DLL
    uint32_t firstThunk;         // RVA на Import Address Table (сюда лоадер пишет реальные адреса)
};

#pragma pack(pop)

// ---------------------------------------------------------------------------
// Удобные "человеческие" структуры — то, с чем реально работает наш код
// ---------------------------------------------------------------------------

struct Section {
    std::string name;
    uint32_t virtualAddress;
    uint32_t virtualSize;
    uint32_t rawDataPointer;
    uint32_t rawDataSize;
    uint32_t characteristics;
};

struct ImportedFunction {
    std::string name;      // "" если импорт по ординалу
    uint16_t ordinal;      // валиден, только если name пуст
    uint32_t iatRva;       // RVA слота в Import Address Table — сюда будем
                            // писать адрес нашей функции-заглушки при загрузке
};

struct ImportedModule {
    std::string dllName;
    std::vector<ImportedFunction> functions;
};

enum class LoadResult {
    Ok,
    FileNotFound,
    NotAValidDosHeader,     // нет сигнатуры "MZ"
    NotAValidPeHeader,      // нет сигнатуры "PE\0\0"
    Not32BitPe,             // это PE32+ (x64), а мы пока поддерживаем только x86
    UnsupportedMachine      // machine type не x86 (0x14c)
};

const char* ToString(LoadResult result);

// ---------------------------------------------------------------------------
// Основной класс
// ---------------------------------------------------------------------------

class PEParser {
public:
    // Загружает файл целиком в память и парсит все заголовки + секции + импорты.
    LoadResult LoadFromFile(const std::string& path);

    uint32_t GetEntryPointRva() const { return optionalHeader_.addressOfEntryPoint; }
    uint32_t GetImageBase() const { return optionalHeader_.imageBase; }
    uint32_t GetSizeOfImage() const { return optionalHeader_.sizeOfImage; }

    // DataDirectory[2] = Resource Table (см. PE spec, IMAGE_DIRECTORY_ENTRY_RESOURCE).
    // 0, если у образа вообще нет ресурсов (.rsrc отсутствует).
    uint32_t GetResourceDirectoryRva() const {
        if (optionalHeader_.numberOfRvaAndSizes < 3) return 0;
        return optionalHeader_.dataDirectory[2].virtualAddress;
    }

    // Каталог данных по индексу (0 экспорт, 1 импорт, 2 ресурсы, 5 релокации,
    // 9 TLS, 12 IAT). {0, 0}, если каталога нет.
    DataDirectory GetDataDirectory(int index) const {
        if (index < 0 || static_cast<uint32_t>(index) >= optionalHeader_.numberOfRvaAndSizes || index >= 16) return {0, 0};
        return optionalHeader_.dataDirectory[index];
    }
    uint16_t GetCharacteristics() const { return fileHeader_.characteristics; }
    uint16_t GetDllCharacteristics() const { return optionalHeader_.dllCharacteristics; }
    bool IsDll() const { return (fileHeader_.characteristics & 0x2000) != 0; }
    uint32_t GetSizeOfStackReserve() const { return optionalHeader_.sizeOfStackReserve; }
    uint32_t GetSizeOfStackCommit() const { return optionalHeader_.sizeOfStackCommit; }
    uint32_t GetSizeOfHeapReserve() const { return optionalHeader_.sizeOfHeapReserve; }
    uint16_t GetSubsystem() const { return optionalHeader_.subsystem; }

    const std::vector<Section>& GetSections() const { return sections_; }
    const std::vector<ImportedModule>& GetImports() const { return imports_; }

    // Переводит RVA (относительный виртуальный адрес, как в заголовках PE)
    // в реальное смещение внутри файла на диске (raw file offset).
    // Проверяет весь диапазон: заголовки или raw-байты секции, включая VirtualSize=0.
    // Виртуальный хвост/BSS без файловых данных возвращает -1.
    int64_t RvaToFileOffset(uint32_t rva, size_t bytes=1) const;

    // Копирует весь образ в подготовленный буфер `dest` так,
    // как это должно выглядеть в памяти процесса при исполнении
    // (т.е. раскладывает секции по их virtualAddress, а не по файловому смещению).
    // `dest` должен быть заранее выделен размером не меньше GetSizeOfImage().
    void MapImageToMemory(uint8_t* dest, size_t destSize) const;

private:
    std::vector<uint8_t> fileBuffer_;   // сырые байты файла как есть на диске
    DosHeader dosHeader_{};
    FileHeader fileHeader_{};
    OptionalHeader32 optionalHeader_{};
    std::vector<Section> sections_;
    std::vector<ImportedModule> imports_;

    // Читает ограниченную нуль-терминированную строку по файловым RVA
    bool ReadCStringAtRva(uint32_t rva, std::string& out) const;

    bool ParseImportTable();
};

} // namespace pe
