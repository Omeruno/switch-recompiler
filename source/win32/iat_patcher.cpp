#include "iat_patcher.h"
#include "crt_globals.h"
#include <cstring>

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
        if (std::strcmp(e.dllName, dllName) == 0 && std::strcmp(e.funcName, funcName) == 0) {
            return &e;
        }
    }
    return nullptr;
}

} // namespace

PatchResult PatchImportAddressTable(const pe::PEParser& parser,
                                     uint8_t* imageBuffer,
                                     size_t imageBufferSize,
                                     uint32_t crtGlobalsBase) {
    PatchResult result;

    for (const auto& mod : parser.GetImports()) {
        for (const auto& func : mod.functions) {
            result.totalImports++;

            uint32_t value = 0;
            bool implemented = false; // именно РЕАЛИЗОВАНО, а не просто "есть куда прыгнуть"

            if (func.name.empty()) {
                // Импорт по ординалу (это ВСЯ MFC42u.DLL — 616 из 616 её
                // импортов у paint.exe) — сначала ищем в статической таблице
                // (см. FindHostFunctionByOrdinal). Если там нет — заводим
                // запись в динамическом диагностическом пуле
                // (AllocateMfcStubSlot), чтобы вызов хотя бы не улетал в
                // никуда (адрес 0/сырой thunk), а был виден в логе.
                int idx = FindHostFunctionByOrdinal(mod.dllName.c_str(), func.ordinal);
                if (idx >= 0) {
                    value = IndexToHostAddress(idx);
                    implemented = true;
                } else {
                    value = AllocateMfcStubSlot(mod.dllName, func.ordinal);
                }
            } else if (const DataImportEntry* dataEntry = FindDataImport(mod.dllName.c_str(), func.name.c_str())) {
                value = crtGlobalsBase + dataEntry->crtOffset;
                implemented = true;
            } else {
                int idx = FindHostFunction(mod.dllName.c_str(), func.name.c_str());
                if (idx >= 0) {
                    value = IndexToHostAddress(idx);
                    implemented = true;
                } else {
                    // Страховочная заглушка (аудит этой сессии): раньше слот
                    // оставался 0, и вызов нереализованной функции был
                    // безымянным крашем "адрес 0". Теперь заглушка с
                    // корректной очисткой стека и именем функции в логе.
                    // Счётчик unimplemented по-прежнему честно растёт.
                    value = AllocateNamedStubSlot(mod.dllName, func.name);
                }
            }

            if (implemented) result.patched++;
            else result.unimplemented++;

            // func.iatRva — это RVA, т.е. смещение внутри уже замапленного
            // parser.MapImageToMemory() образа (см. pe_parser.h)
            if (static_cast<size_t>(func.iatRva) + 4 <= imageBufferSize) {
                std::memcpy(imageBuffer + func.iatRva, &value, 4);
            }
        }
    }

    return result;
}

} // namespace win32
