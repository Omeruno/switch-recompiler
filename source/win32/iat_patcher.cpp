#include "iat_patcher.h"
#include "crt_globals.h"
#include <cstring>
#include <strings.h>
#include <vector>

namespace win32 {

namespace {

// Известные импорты-ДАННЫЕ (не функции!) — компилятор обращается к ним как
// `mov eax, [__imp__acmdln]`, а не через call. IAT-слот должен указывать на
// настоящую ячейку памяти, а не на наш "host-адрес" (который годится только
// для перехвата call).
struct DataImportEntry {
    const char* dllName;
    const char* funcName;
    uint32_t crtOffset; // смещение внутри CRT-globals области
};

const DataImportEntry g_dataImports[] = {
    { "msvcrt.dll", "_adjust_fdiv", CRT_OFFSET_ADJUST_FDIV },
    { "msvcrt.dll", "_acmdln",      CRT_OFFSET_CMDLINE_PTR },
    { "msvcrt.dll", "_wcmdln",      CRT_OFFSET_WCMDLINE_PTR }, // широкий аналог _acmdln —
                                                                 // нужен MFC-приложениям (paint.exe),
                                                                 // найдено экспериментально: без этой
                                                                 // записи IAT-слот оставался 0, и
                                                                 // guest-код падал на разыменовании
                                                                 // нулевого указателя сразу после
                                                                 // __wgetmainargs/CRT-старта.
};

const DataImportEntry* FindDataImport(const char* dllName, const char* funcName) {
    for (const auto& e : g_dataImports) {
        if (strcasecmp(e.dllName, dllName) == 0 && std::strcmp(e.funcName, funcName) == 0) {
            return &e;
        }
    }
    return nullptr;
}

} // namespace

namespace {
struct DynamicDataImport {
    const char* dll;
    const char* name;
    uint32_t (*provider)(cpu::IGuestMemory&);
};
std::vector<DynamicDataImport>& DynamicData() {
    static std::vector<DynamicDataImport> v;
    return v;
}
cpu::IGuestMemory* g_bindMemory = nullptr;
} // namespace

void RegisterDataImport(const char* dllName, const char* name, uint32_t (*provider)(cpu::IGuestMemory&)) {
    DynamicData().push_back({dllName, name, provider});
}

uint32_t FindDataImportAddress(cpu::IGuestMemory& mem, const std::string& dllName, const std::string& name) {
    for (const auto& d : DynamicData())
        if (strcasecmp(d.dll, dllName.c_str()) == 0 && name == d.name) return d.provider(mem);
    return 0;
}

void SetImportBindingMemory(cpu::IGuestMemory* mem) { g_bindMemory = mem; }
cpu::IGuestMemory* ImportBindingMemory() {return g_bindMemory;}

uint32_t ResolveHostImport(const std::string& dllName, const pe::ImportedFunction& func,
                           uint32_t crtGlobalsBase, bool& implemented) {
    implemented = false;
    if (!func.name.empty() && g_bindMemory) {
        uint32_t data = FindDataImportAddress(*g_bindMemory, dllName, func.name);
        if (data) { implemented = true; return data; }
    }
    if (func.name.empty()) {
        // Импорт по ординалу (это ВСЯ MFC42u.DLL — 616 из 616 её
        // импортов у paint.exe) — сначала ищем в таблице реализаций.
        // Если там нет — заводим запись в диагностическом пуле, чтобы
        // вызов был виден в логе, а не улетал в никуда.
        int idx = FindHostFunctionByOrdinal(dllName.c_str(), func.ordinal);
        if (idx >= 0) { implemented = true; return IndexToHostAddress(idx); }
        return AllocateMfcStubSlot(dllName, func.ordinal);
    }
    if (const DataImportEntry* dataEntry = FindDataImport(dllName.c_str(), func.name.c_str())) {
        implemented = true;
        return crtGlobalsBase + dataEntry->crtOffset;
    }
    int idx = FindHostFunction(dllName.c_str(), func.name.c_str());
    if (idx >= 0) { implemented = true; return IndexToHostAddress(idx); }
    // Страховочная заглушка с корректной очисткой стека и именем в логе.
    return AllocateNamedStubSlot(dllName, func.name);
}

PatchResult PatchImportAddressTable(const pe::PEParser& parser,
                                     uint8_t* imageBuffer,
                                     size_t imageBufferSize,
                                     uint32_t crtGlobalsBase) {
    PatchResult result;
    for (const auto& mod : parser.GetImports()) {
        for (const auto& func : mod.functions) {
            result.totalImports++;
            bool implemented = false;
            uint32_t value = ResolveHostImport(mod.dllName, func, crtGlobalsBase, implemented);
            if (implemented) result.patched++;
            else result.unimplemented++;
            // func.iatRva — RVA внутри уже разложенного образа
            if (static_cast<size_t>(func.iatRva) + 4 <= imageBufferSize) {
                std::memcpy(imageBuffer + func.iatRva, &value, 4);
            }
        }
    }
    return result;
}

} // namespace win32
