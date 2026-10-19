#include "virtual_registry.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace win32 {

VirtualRegistry g_registry;

namespace {

constexpr uint32_t FAKE_HKEY_BASE = 0x90000000u;

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

} // namespace

std::string ReadWideCStringNarrow(cpu::IGuestMemory& mem, uint32_t addr, size_t maxLen) {
    std::string result;
    if (addr == 0) return result;
    for (size_t i = 0; i < maxLen; i++) {
        uint16_t c = mem.Read16(addr + static_cast<uint32_t>(i) * 2);
        if (c == 0) break;
        result.push_back(static_cast<char>(c & 0xFF)); // грубое усечение — ок для ASCII
    }
    return result;
}

std::string VirtualRegistry::ResolveBasePath(uint32_t hKey) const {
    switch (hKey) {
        case HKEY_CLASSES_ROOT:  return "hkcr";
        case HKEY_CURRENT_USER:  return "hkcu";
        case HKEY_LOCAL_MACHINE: return "hklm";
        case HKEY_USERS:         return "hku";
        default:
            if (hKey >= FAKE_HKEY_BASE) {
                size_t idx = hKey - FAKE_HKEY_BASE;
                if (idx < openHandles_.size()) return openHandles_[idx];
            }
            return "unknown";
    }
}

std::string VirtualRegistry::JoinPath(const std::string& base, const std::string& subKey) const {
    std::string sub = ToLower(subKey);
    if (sub.empty()) return base;
    return base + "\\" + sub;
}

uint32_t VirtualRegistry::CreateOrOpenKey(uint32_t hKeyParent, const std::string& subKey, bool& existed) {
    std::string fullPath = JoinPath(ResolveBasePath(hKeyParent), subKey);

    existed = keys_.find(fullPath) != keys_.end();
    if (!existed) {
        keys_[fullPath]; // создаёт пустую запись значений для этого пути
    }

    uint32_t idx = static_cast<uint32_t>(openHandles_.size());
    openHandles_.push_back(fullPath);
    return FAKE_HKEY_BASE + idx;
}

uint32_t VirtualRegistry::OpenExistingKey(uint32_t hKeyParent, const std::string& subKey, bool& found) {
    std::string fullPath = JoinPath(ResolveBasePath(hKeyParent), subKey);

    found = keys_.find(fullPath) != keys_.end();
    if (!found) return 0;

    uint32_t idx = static_cast<uint32_t>(openHandles_.size());
    openHandles_.push_back(fullPath);
    return FAKE_HKEY_BASE + idx;
}

bool VirtualRegistry::GetValue(uint32_t hKey, const std::string& valueName, RegValue& out) const {
    std::string path = ResolveBasePath(hKey);
    auto keyIt = keys_.find(path);
    if (keyIt == keys_.end()) return false;
    auto valIt = keyIt->second.find(ToLower(valueName));
    if (valIt == keyIt->second.end()) return false;
    out = valIt->second;
    return true;
}

void VirtualRegistry::SetValue(uint32_t hKey, const std::string& valueName, uint32_t type,
                                const std::vector<uint8_t>& data) {
    std::string path = ResolveBasePath(hKey);
    keys_[path][ToLower(valueName)] = RegValue{type, data};
}

namespace {
char HexDigit(int v) { return v < 10 ? static_cast<char>('0' + v) : static_cast<char>('a' + v - 10); }
int HexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}
} // namespace

bool VirtualRegistry::SaveToFile(const std::string& path) const {
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) return false; // например, папка на SD ещё не создана — не критично, просто не сохранили

    for (const auto& keyEntry : keys_) {
        std::fprintf(f, "KEY %s\n", keyEntry.first.c_str());
        for (const auto& valEntry : keyEntry.second) {
            std::fprintf(f, "VAL %s %u ", valEntry.first.c_str(), valEntry.second.type);
            for (uint8_t b : valEntry.second.data) {
                std::fputc(HexDigit(b >> 4), f);
                std::fputc(HexDigit(b & 0xF), f);
            }
            std::fputc('\n', f);
        }
    }
    std::fclose(f);
    return true;
}

bool VirtualRegistry::LoadFromFile(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f) return false; // честно и ожидаемо на самом первом запуске — файла ещё нет

    keys_.clear();
    std::string currentKey;
    char line[1024];
    while (std::fgets(line, sizeof(line), f)) {
        size_t len = std::strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = 0;

        if (std::strncmp(line, "KEY ", 4) == 0) {
            currentKey = line + 4;
            keys_[currentKey]; // создаём пустую запись
        } else if (std::strncmp(line, "VAL ", 4) == 0 && !currentKey.empty()) {
            char name[256] = {0};
            unsigned int type = 0;
            int consumed = 0;
            if (std::sscanf(line + 4, "%255s %u%n", name, &type, &consumed) == 2) {
                const char* hexStart = line + 4 + consumed;
                while (*hexStart == ' ') hexStart++;
                std::vector<uint8_t> data;
                size_t hexLen = std::strlen(hexStart);
                for (size_t i = 0; i + 1 < hexLen; i += 2) {
                    uint8_t b = static_cast<uint8_t>((HexValue(hexStart[i]) << 4) | HexValue(hexStart[i+1]));
                    data.push_back(b);
                }
                keys_[currentKey][name] = RegValue{type, data};
            }
        }
    }
    std::fclose(f);
    return true;
}

} // namespace win32