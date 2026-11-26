#include "game.h"
#include <string.h>
#include <strings.h>

#define IS(s) (!strcmp(name, (s)))
#define MODULE 0x2d180001u
#define BAD_FORMAT 0x80070057u

bool input_dispatch(Game *g, const Import *call)
{
    const char *name = call->name;
    uint32_t a = arg(g, 0), b = arg(g, 1);
    if ((IS("LoadLibraryA") || IS("GetModuleHandleA")) && a &&
        !strcasecmp((const char *)guest_pointer(a), "dinput8.dll"))
        return result(g, MODULE, 4);
    if (IS("FreeLibrary") && a == MODULE)
        return result(g, 1, 4);
    if (IS("GetProcAddress") && a == MODULE) {
        bool match = b > 0xffff && !strcmp((const char *)guest_pointer(b), "DirectInput8Create");
        return result(g, match ? add_import(g, "dinput8.dll", "DirectInput8Create") : 0, 8);
    }
    if (IS("DirectInput8Create")) {
        if (b != 0x800 || !arg(g, 3) || arg(g, 4))
            return result(g, BAD_FORMAT, 20);
        if (!g->input)
            g->input = make_interface(g, &di_root);
        if (!g->input)
            return false;
        ++g->input_refs;
        wr32(arg(g, 3), g->input);
        return result(g, 0, 20);
    }
    if (!strcmp(call->dll, di_root.dll) && a == g->input) {
        if (IS("AddRef"))
            return result(g, ++g->input_refs, 4);
        if (IS("Release") && g->input_refs)
            return result(g, --g->input_refs, 4);
        if (IS("EnumDevices"))
            return result(g, 0, 20); // Keyboard and mouse are created by GUID.
        if (IS("CreateDevice")) {
            static const uint8_t guid[] = {0x2b,0x1d,0x6f,0xa0,0xd5,0xcf,0x11,0xbf,0xc7,0x44,0x45,0x53,0x54,0,0};
            uint8_t type = rd8(b);
            if ((type != 0x61 && type != 0x60) || memcmp(guest_pointer(b + 1), guid, sizeof guid))
                return result(g, 0x80040154, 16);
            uint32_t *handle = type == 0x61 ? &g->keyboard : &g->mouse;
            if (!*handle)
                *handle = make_interface(g, &di_device);
            if (!*handle)
                return false;
            wr32(arg(g, 2), *handle);
            return result(g, 0, 16);
        }
    }
    if (!strcmp(call->dll, di_device.dll) && a && (a == g->keyboard || a == g->mouse)) {
        bool keyboard = a == g->keyboard;
        bool *acquired = keyboard ? &g->key_acquired : &g->mouse_acquired;
        uint32_t *size = keyboard ? &g->key_size : &g->mouse_size;
        if (IS("Acquire") || IS("Unacquire")) {
            *acquired = IS("Acquire");
            return result(g, 0, 4);
        }
        if (IS("SetDataFormat")) {
            *size = rd32(b + 12);
            return result(g, 0, 8);
        }
        if (IS("SetProperty") || IS("SetCooperativeLevel"))
            return result(g, 0, 12);
        if (IS("GetDeviceState") && *acquired && *size == b && b <= 256) {
            uint8_t *target = guest_pointer(arg(g, 2));
            memset(target, 0, b);
            if (keyboard)
                memcpy(target, g->keys, b);
            return result(g, 0, 12);
        }
        if (IS("Poll") && *acquired)
            return result(g, 0, 4);
    }
    return false;
}

void game_keys(Game *g, const uint8_t keys[256])
{
    static const struct { uint8_t scan, vk; } bindings[] = {
        {0x01, 0x1b}, {0x1c, 0x0d}, {0x39, 0x20},
        {0xc8, 0x26}, {0xcb, 0x25}, {0xcd, 0x27}, {0xd0, 0x28},
    };
    for (unsigned i = 0; g->window_count && i < sizeof bindings / sizeof *bindings; ++i) {
        uint32_t scan = bindings[i].scan, vk = bindings[i].vk;
        if (keys[scan] == g->virtual_keys[vk])
            continue;
        bool pressed = keys[scan] != 0;
        uint32_t flags = 1 | (scan & 127) << 16 | (scan & 128 ? 0x01000000 : 0);
        flags |= pressed ? 0 : 0xc0000000;
        FreakOutMessage message = {
            .hwnd = 0x200, .message = pressed ? 0x100 : 0x101,
            .wparam = vk, .lparam = flags, .time = g->idle_polls,
        };
        if (freakout_post_message(&g->messages, message))
            g->virtual_keys[vk] = keys[scan];
    }
    memcpy(g->keys, keys, sizeof g->keys);
}
