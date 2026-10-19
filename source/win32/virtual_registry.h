#pragma once
// virtual_registry.h
//
// Простой виртуальный реестр Windows — key-value хранилище в памяти.
// Реализует ровно тот контракт, который нужен ADVAPI32-функциям
// (RegCreateKeyExW/RegOpenKeyExA/RegQueryValueEx*/RegSetValueExW/RegCloseKey).
//
// ЧЕСТНОЕ ОГРАНИЧЕНИЕ текущей версии: значения НЕ сохраняются между
// запусками игры (живут только пока запущен наш homebrew). Это сознательный
// выбор для первой версии, а не недоделка — полноценное сохранение на SD
// (JSON-файл через файловую систему Switch) можно добавить позже отдельным
// шагом, не меняя саму логику работы с реестром.

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include "../cpu/guest_memory.h"

namespace win32 {

constexpr uint32_t HKEY_CLASSES_ROOT  = 0x80000000u;
constexpr uint32_t HKEY_CURRENT_USER  = 0x80000001u;
constexpr uint32_t HKEY_LOCAL_MACHINE = 0x80000002u;
constexpr uint32_t HKEY_USERS         = 0x80000003u;

// Коды ошибок Win32, которые реально проверяет типичный игровой код —
// поэтому важно возвращать именно их, а не произвольные числа
constexpr uint32_t ERROR_SUCCESS_        = 0;
constexpr uint32_t ERROR_FILE_NOT_FOUND_ = 2;
constexpr uint32_t ERROR_MORE_DATA_      = 234;

struct RegValue {
    uint32_t type = 0; // REG_SZ=1, REG_BINARY=3, REG_DWORD=4, и т.д.
    std::vector<uint8_t> data;
};

class VirtualRegistry {
public:
    // Открывает ключ, создавая его при отсутствии. existed=true, если ключ
    // уже был (для правильного REG_CREATED_NEW_KEY/REG_OPENED_EXISTING_KEY)
    uint32_t CreateOrOpenKey(uint32_t hKeyParent, const std::string& subKey, bool& existed);

    // Открывает ТОЛЬКО существующий ключ. found=false, если такого ключа
    // нет — вызывающий код (RegOpenKeyExA) должен вернуть ERROR_FILE_NOT_FOUND
    uint32_t OpenExistingKey(uint32_t hKeyParent, const std::string& subKey, bool& found);

    bool GetValue(uint32_t hKey, const std::string& valueName, RegValue& out) const;
    void SetValue(uint32_t hKey, const std::string& valueName, uint32_t type, const std::vector<uint8_t>& data);

    // Сохранение/загрузка на SD-карту — простой текстовый формат, ключи и
    // значения построчно. Вызывается автоматически: LoadFromFile — один раз
    // при старте (main.cpp), SaveToFile — после каждого RegSetValueEx (реестр
    // маленький, лишний вызов записи на SD не заметен, зато данные никогда
    // не теряются, даже если консоль выключат посреди игры).
    bool SaveToFile(const std::string& path) const;
    bool LoadFromFile(const std::string& path);

private:
    std::string ResolveBasePath(uint32_t hKey) const;
    std::string JoinPath(const std::string& base, const std::string& subKey) const;

    // path (уже нормализованный, lowercase) -> (имя значения -> данные)
    std::unordered_map<std::string, std::unordered_map<std::string, RegValue>> keys_;
    std::vector<std::string> openHandles_; // индекс -> полный путь ключа
};

extern VirtualRegistry g_registry;

// Путь к файлу реестра на SD-карте — общий, чтобы main.cpp (загрузка при
// старте) и host_functions.cpp (сохранение при каждой записи) использовали
// один и тот же путь.
constexpr const char* kRegistryFilePath = "sdmc:/switch-recompiler/registry.dat";
constexpr const char* kRegistryFolderPath = "sdmc:/switch-recompiler";

// Читает wide (UTF-16) C-строку из гостевой памяти, грубо приводя к ASCII —
// этого достаточно для путей реестра и имён значений (они всегда латиница)
std::string ReadWideCStringNarrow(cpu::IGuestMemory& mem, uint32_t addr, size_t maxLen = 256);

} // namespace win32