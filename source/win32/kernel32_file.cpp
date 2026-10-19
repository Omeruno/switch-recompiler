// kernel32_file.cpp — KERNEL32: ввод-вывод через хендлы.
// Пока только консольные хендлы (stdout/stderr); файлы — следующим шагом.

#include <string>

#include "hle_util.h"

namespace win32 {

bool HleConsoleWrite(uint32_t handle, const char* data, size_t len); // kernel32_module.cpp

namespace {

using namespace hle;

// BOOL WriteFile(HANDLE, LPCVOID buf, DWORD n, LPDWORD written, LPOVERLAPPED)
HostCallOutcome K32_WriteFile(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t h = Arg(c, m, 0), buf = Arg(c, m, 1), n = Arg(c, m, 2), written = Arg(c, m, 3);
    std::string s(n, '\0');
    for (uint32_t i = 0; i < n; ++i) s[i] = static_cast<char>(m.Read8(buf + i));
    if (HleConsoleWrite(h, s.data(), s.size())) {
        if (written) m.Write32(written, n);
        return Ret(1);
    }
    if (written) m.Write32(written, 0);
    SetLastError(m, ERROR_INVALID_HANDLE);
    return Ret(0);
}

const HostFunctionEntry kEntries[] = {
    {"KERNEL32.dll", "WriteFile", 20, CallConv::Stdcall, &K32_WriteFile},
};
const bool kRegistered = RegisterHostFunctions(kEntries, sizeof(kEntries) / sizeof(kEntries[0]));

} // namespace
} // namespace win32
