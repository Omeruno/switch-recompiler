#include "crt_globals.h"
#include <cstring>

namespace win32 {

uint32_t g_crtGlobalsBase = 0;

void InitCrtGlobals(cpu::IGuestMemory& mem, uint32_t base, const char* commandLine) {
    g_crtGlobalsBase = base;

    mem.Write32(base + CRT_OFFSET_ADJUST_FDIV, 0);
    mem.Write32(base + CRT_OFFSET_FMODE, 0x8000); // _O_TEXT
    mem.Write32(base + CRT_OFFSET_COMMODE, 0);

    size_t len = std::strlen(commandLine);
    if (len > 63) len = 63;
    for (size_t i = 0; i < len; i++) {
        mem.Write8(base + CRT_OFFSET_CMDLINE_STR + static_cast<uint32_t>(i),
                   static_cast<uint8_t>(commandLine[i]));
    }
    mem.Write8(base + CRT_OFFSET_CMDLINE_STR + static_cast<uint32_t>(len), 0);

    // _acmdln сам по себе — это char*, значение которого лежит в CMDLINE_PTR
    mem.Write32(base + CRT_OFFSET_CMDLINE_PTR, base + CRT_OFFSET_CMDLINE_STR);

    // argv = { "имя_программы", NULL }
    mem.Write32(base + CRT_OFFSET_ARGV + 0, base + CRT_OFFSET_CMDLINE_STR);
    mem.Write32(base + CRT_OFFSET_ARGV + 4, 0);

    // envp = { NULL } — пустое окружение, этого достаточно для большинства программ
    mem.Write32(base + CRT_OFFSET_ENV, 0);

    // Широкая (UTF-16) копия той же командной строки — для __wgetmainargs/
    // GetCommandLineW. Командная строка у нас всегда чистый ASCII (имя
    // программы), поэтому побайтовое расширение char->wchar_t честно и
    // достаточно (не нужен настоящий MultiByteToWideChar).
    size_t wlen = len; // до 63 символов, как и в узкой версии
    for (size_t i = 0; i < wlen; i++) {
        mem.Write16(base + CRT_OFFSET_WCMDLINE_STR + static_cast<uint32_t>(i) * 2,
                    static_cast<uint16_t>(static_cast<uint8_t>(commandLine[i])));
    }
    mem.Write16(base + CRT_OFFSET_WCMDLINE_STR + static_cast<uint32_t>(wlen) * 2, 0);

    // wargv = { "имя_программы" (в UTF-16), NULL }
    mem.Write32(base + CRT_OFFSET_WARGV + 0, base + CRT_OFFSET_WCMDLINE_STR);
    mem.Write32(base + CRT_OFFSET_WARGV + 4, 0);

    // wenvp = { NULL }
    mem.Write32(base + CRT_OFFSET_WENV, 0);

    // _wcmdln сам по себе — это wchar_t*, значение которого лежит в WCMDLINE_PTR
    // (широкий аналог _acmdln/CMDLINE_PTR выше)
    mem.Write32(base + CRT_OFFSET_WCMDLINE_PTR, base + CRT_OFFSET_WCMDLINE_STR);
}

} // namespace win32
