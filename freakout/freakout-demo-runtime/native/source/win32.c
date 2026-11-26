#include "game.h"
#include <string.h>
#include <strings.h>

bool win32_dispatch(Game *g, const Import *call)
{
    const char *name = call->name;
    #define IS(s) (!strcmp(name, (s)))
    uint32_t a = arg(g, 0);
    if (IS("GetModuleHandleA") && !a)
        return result(g, PE_BASE, 4);
    if (IS("GetCurrentProcessId"))
        return result(g, 1, 0);
    uint64_t ticks = g->presents < 136 ? 1 : 1 + (uint64_t)(g->presents - 136) * 16667;
    if (IS("GetTickCount"))
        return result(g, g->presents < 136 ? 1 : (uint32_t)(ticks / 1000), 0);
    if (IS("GetSystemTimeAsFileTime") || IS("QueryPerformanceCounter") || IS("QueryPerformanceFrequency")) {
        wr64(a, IS("QueryPerformanceFrequency") ? 1000000 : IS("QueryPerformanceCounter") ? ticks : 1);
        return result(g, 1, 4);
    }
    if (IS("GetSystemTime") || IS("GetLocalTime")) {
        static const uint16_t time[] = {2007, 1, 4, 11, 12, 0, 0, 0};
        memcpy(guest_pointer(a), time, sizeof time);
        return result(g, 0, 4);
    }
    if (IS("GetStartupInfoA")) {
        memset(guest_pointer(a), 0, 68);
        wr32(a, 68);
        return result(g, 0, 4);
    }
    if (IS("GetVersionExA")) {
        uint32_t size = rd32(a);
        if (size != 148 && size != 156)
            return result(g, 0, 4);
        memset(guest_pointer(a + 4), 0, size - 4);
        wr32(a + 4, 5);
        wr32(a + 8, 1);
        wr32(a + 12, 2600);
        wr32(a + 16, 2);
        return result(g, 1, 4);
    }
    if (IS("InterlockedExchange") || IS("InterlockedIncrement") || IS("InterlockedDecrement")) {
        uint32_t previous = rd32(a);
        uint32_t next = IS("InterlockedExchange") ? arg(g, 1) : previous + (IS("InterlockedIncrement") ? 1 : -1);
        wr32(a, next);
        return result(g, IS("InterlockedExchange") ? previous : next, IS("InterlockedExchange") ? 8 : 4);
    }
    if (IS("ShowCursor")) {
        g->cursor_count += a ? 1 : -1;
        return result(g, g->cursor_count, 4);
    }
    if (IS("LoadIconA") || IS("LoadCursorA") || IS("GetStockObject"))
        return result(g, IS("LoadIconA") ? 0x101 : 0x102, IS("GetStockObject") ? 4 : 8);
    if (IS("RegisterClassA")) {
        g->window_proc = rd32(a + 4);
        return result(g, 1, 4);
    }
    if (IS("AdjustWindowRect"))
        return result(g, 1, 12);
    if (IS("CreateWindowExA")) {
        if (!g->window_count)
            fprintf(g->log, "CreateWindowExA %s %ux%u\n", guest_pointer(arg(g, 2)), arg(g, 6), arg(g, 7));
        return result(g, 0x200 + g->window_count++, 48);
    }
    if (IS("ShowWindow") && a >= 0x200 && a - 0x200 < g->window_count)
        return result(g, 0, 8);
    if ((IS("UpdateWindow") || IS("SetCapture")) && a >= 0x200 && a - 0x200 < g->window_count)
        return result(g, IS("UpdateWindow"), 4);
    if (IS("PeekMessageA")) {
        FreakOutMessage message;
        bool present = freakout_peek_message(&g->messages, arg(g, 1), arg(g, 2), arg(g, 3),
                                            arg(g, 4) & 1, &message);
        if (present)
            memcpy(guest_pointer(a), &message, sizeof message);
        else
            ++g->idle_polls;
        return result(g, present, 20);
    }
    if (IS("TranslateMessage"))
        return result(g, 0, 4);
    if (IS("DispatchMessageA")) {
        uint32_t hwnd = rd32(a);
        if (!g->window_proc || hwnd < 0x200 || hwnd - 0x200 >= g->window_count)
            return result(g, 0, 4);
        if (g->window_depth == sizeof g->window_callbacks / sizeof *g->window_callbacks)
            return false;
        unsigned depth = g->window_depth++;
        uint32_t stack = g->cpu.esp;
        g->window_callbacks[depth].stack = stack;
        g->window_callbacks[depth].ret = rd32(stack);
        wr32(stack - 20, WINDOW_RETURN);
        memcpy(guest_pointer(stack - 16), guest_pointer(a), 16);
        g->cpu.esp = stack - 20;
        g->cpu.eip = g->window_proc;
        return true;
    }
    if (IS("DefWindowProcA"))
        return result(g, 0, 16);
    if (IS("GetKeyboardState")) {
        memcpy(guest_pointer(a), g->virtual_keys, sizeof g->virtual_keys);
        return result(g, 1, 4);
    }
    if (IS("GetAsyncKeyState") || IS("GetKeyState"))
        return result(g, a < 256 && g->virtual_keys[a] ? 0x8000 : 0, 4);
    if (IS("_FSOUND_SetPriority@8"))
        return result(g, 0, 8); // No channel exists until the audio backend initializes.
    return false;
    #undef IS
}
