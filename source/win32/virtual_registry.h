#pragma once
// virtual_registry.h
//
// Host-owned values and session-owned HKEY grants. See D1_REGISTRY_KEYS.md.
// Native save/load is the existing best-effort snapshot provider, not a
// transactional/power-loss-safe Windows hive or registry-flush implementation.

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

struct RegistryHandle {std::string path;uint32_t access=0;};
class VirtualRegistry {
public:
    // Values belong to the host registry; transient HKEYs belong to a guest
    // session. Exchange only handles so application settings survive relaunch.
    void ExchangeOpenHandles(std::vector<RegistryHandle>& handles) {openHandles_.swap(handles);}

    static constexpr uint32_t AllAccess=0x000f003f;
    static constexpr size_t MaxHandles=65536,MaxPath=4096;
    bool ValidKey(uint32_t) const;
    bool CanAccess(uint32_t,uint32_t) const;
    bool PathFits(uint32_t,const std::string&) const;
    bool CloseKey(uint32_t);
    // Открывает ключ, создавая его при отсутствии. existed=true, если ключ
    // уже был (для правильного REG_CREATED_NEW_KEY/REG_OPENED_EXISTING_KEY)
    uint32_t CreateOrOpenKey(uint32_t hKeyParent, const std::string& subKey, bool& existed,uint32_t access=AllAccess);

    // Открывает ТОЛЬКО существующий ключ. found=false, если такого ключа
    // нет — вызывающий код (RegOpenKeyExA) должен вернуть ERROR_FILE_NOT_FOUND
    uint32_t OpenExistingKey(uint32_t hKeyParent, const std::string& subKey, bool& found,uint32_t access=AllAccess);

    bool GetValue(uint32_t hKey, const std::string& valueName, RegValue& out) const;
    bool SetValue(uint32_t hKey, const std::string& valueName, uint32_t type, const std::vector<uint8_t>& data);

    // Existing native snapshot provider. No atomic replacement, fsync/error
    // propagation or complete name/large-value round-trip guarantee. D2 must
    // replace this separately; successful RegSetValueEx is in-memory acceptance.
    bool SaveToFile(const std::string& path) const;
    bool LoadFromFile(const std::string& path);

private:
    std::string ResolveBasePath(uint32_t hKey) const;
    std::string JoinPath(const std::string& base, const std::string& subKey) const;

    // path (уже нормализованный, lowercase) -> (имя значения -> данные)
    std::unordered_map<std::string, std::unordered_map<std::string, RegValue>> keys_;
    std::vector<RegistryHandle> openHandles_; // индекс -> полный путь ключа
};

extern VirtualRegistry g_registry;

// Путь к файлу реестра на SD-карте — общий, чтобы main.cpp (загрузка при
// старте) и host_functions.cpp (сохранение при каждой записи) использовали
// один и тот же путь.
constexpr const char* kRegistryFilePath = "sdmc:/switch-recompiler/registry.dat";
constexpr const char* kRegistryFolderPath = "sdmc:/switch-recompiler";

// Legacy USER class/title helper only; checked registry APIs do not use it.
std::string ReadWideCStringNarrow(cpu::IGuestMemory&,uint32_t,size_t maxLen=256);

} // namespace win32