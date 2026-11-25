#include "game.h"
#include <string.h>
#include <strings.h>

#define S_OK 0
#define NOT_AVAILABLE 0x8876086au
#define INVALID_CALL 0x8876086cu
#define MODULE 0x2d390001u
#define IS(s) (!strcmp(name, (s)))

static void caps(uint32_t address, uint32_t adapter, uint32_t type)
{
    uint32_t data[76] = {
        [0] = type, [1] = adapter, [5] = 1, [7] = 0x10000,
        [22] = 4096, [23] = 4096, [24] = 256, [27] = 16, [36] = 8, [37] = 8, [38] = 8,
        [45] = 0xffff, [46] = 0xffff, [47] = 16, [48] = 256, [49] = 0xfffe0200, [50] = 256,
        [51] = 0xffff0200, [58] = 1, [60] = 1,
    };
    memcpy(guest_pointer(address), data, sizeof data);
}

static bool root(Game *g, const char *name)
{
    if (arg(g, 0) != g->d3d)
        return false;
    uint32_t adapter = arg(g, 1), param = arg(g, 2);
    if (IS("AddRef"))
        return result(g, ++g->d3d_refs, 4);
    if (IS("Release") && g->d3d_refs)
        return result(g, --g->d3d_refs, 4);
    if (IS("GetAdapterCount"))
        return result(g, 1, 4);
    if (IS("GetAdapterIdentifier")) {
        uint8_t *info = guest_pointer(arg(g, 3));
        memset(info, 0, 1100);
        memcpy(info, "freakout-switch", sizeof "freakout-switch");
        memcpy(info + 512, "Nintendo Switch", sizeof "Nintendo Switch");
        memcpy(info + 1024, "DISPLAY1", sizeof "DISPLAY1");
        return result(g, adapter ? INVALID_CALL : S_OK, 16);
    }
    if (IS("GetDeviceCaps")) {
        caps(arg(g, 3), adapter, param);
        return result(g, adapter || param != 1 ? NOT_AVAILABLE : S_OK, 16);
    }
    if (IS("GetAdapterModeCount"))
        return result(g, !adapter && (param == 21 || param == 22), 12);
    if (IS("EnumAdapterModes") || IS("GetAdapterDisplayMode")) {
        bool display = IS("GetAdapterDisplayMode");
        uint32_t mode[] = {1024, 768, 60, display ? 22 : param};
        if (adapter || (!display && ((param != 21 && param != 22) || arg(g, 3))))
            return result(g, INVALID_CALL, display ? 12 : 20);
        memcpy(guest_pointer(display ? param : arg(g, 4)), mode, sizeof mode);
        return result(g, S_OK, display ? 12 : 20);
    }
    if (IS("CheckDeviceType") || IS("CheckDeviceFormat") || IS("CheckDepthStencilMatch") ||
        IS("CheckDeviceFormatConversion")) {
        unsigned pop = IS("CheckDeviceFormat") ? 28 : IS("CheckDeviceFormatConversion") ? 20 : 24;
        return result(g, adapter || param != 1 ? NOT_AVAILABLE : S_OK, pop);
    }
    if (IS("CheckDeviceMultiSampleType")) {
        bool available = arg(g, 5) == 0;
        if (arg(g, 6))
            wr32(arg(g, 6), available);
        return result(g, available ? S_OK : NOT_AVAILABLE, 28);
    }
    if (IS("CreateDevice") && !adapter && !g->device) {
        uint32_t *p = (uint32_t *)guest_pointer(arg(g, 5));
        g->device = make_interface(g, &d3d_device);
        g->backbuffer = make_interface(g, &d3d_surface);
        g->depth_buffer = p[9] ? make_interface(g, &d3d_surface) : 0;
        if (!g->device || !g->backbuffer || (p[9] && !g->depth_buffer))
            return false;
        g->device_refs = 1;
        g->back = (Surface){.format = p[2], .usage = 1, .width = p[0], .height = p[1], .refs = 1};
        g->depth = (Surface){.format = p[10], .usage = 2, .width = p[0], .height = p[1], .refs = 1};
        wr32(arg(g, 6), g->device);
        fprintf(g->log, "CreateDevice %ux%u format=%u\n", p[0], p[1], p[2]);
        return result(g, S_OK, 28);
    }
    return false;
}

static Texture *find_texture(Game *g, uint32_t handle)
{
    for (unsigned i = 0; handle && i < MAX_TEXTURES; ++i)
        if (g->textures[i].handle == handle)
            return &g->textures[i];
    return NULL;
}

static Surface *find_surface(Game *g, uint32_t handle)
{
    if (!handle)
        return NULL;
    if (handle == g->backbuffer)
        return &g->back;
    if (handle == g->depth_buffer)
        return &g->depth;
    for (unsigned i = 0; i < MAX_TEXTURES; ++i) {
        Texture *t = &g->textures[i];
        for (unsigned mip = 0; t->handle && mip < t->levels; ++mip)
            if (t->surfaces[mip] == handle)
                return &t->mip[mip];
    }
    return NULL;
}

static uint32_t block_bytes(uint32_t format)
{
    if (format == 0x31545844u)
        return 8;
    return (format & 0x00ffffffu) == 0x00545844u && (format >> 24) >= '2' && (format >> 24) <= '5' ? 16 : 0;
}

static uint32_t surface_pitch(const Surface *s)
{
    static const uint8_t pixel_bytes[] = {3, 4, 4, 2, 2, 2, 2, 0, 1};
    uint32_t block = block_bytes(s->format);
    if (block)
        return ((s->width + 3) / 4) * block;
    uint32_t bytes = s->format >= 20 && s->format <= 28 ? pixel_bytes[s->format - 20] : 0;
    return (s->width * bytes + 3) & ~3u;
}

static uint32_t surface_bytes(const Surface *s)
{
    uint32_t rows = block_bytes(s->format) ? (s->height + 3) / 4 : s->height;
    return rows * surface_pitch(s);
}

static bool lock(Game *g, Surface *s, uint32_t output, uint32_t rectangle, unsigned pop)
{
    uint32_t size = surface_bytes(s);
    if (rectangle || !size || s->locked)
        return result(g, INVALID_CALL, pop);
    if (!s->data)
        s->data = allocate(g, size);
    if (!s->data)
        return result(g, INVALID_CALL, pop);
    s->locked = true;
    wr32(output, surface_pitch(s));
    wr32(output + 4, s->data);
    return result(g, S_OK, pop);
}

static bool unlock(Game *g, Surface *s, unsigned level, unsigned pop)
{
    if (!s->locked)
        return result(g, INVALID_CALL, pop);
    s->locked = false;
    if (block_bytes(s->format)) {
        ++g->uploads;
        unsigned size = surface_bytes(s);
        fprintf(g->log, "DXT%c mip=%u size=%u sha256=", (int)(s->format >> 24), level, size);
        log_hash(g->log, guest_pointer(s->data), size);
        fputc('\n', g->log);
    }
    return result(g, S_OK, pop);
}

static void describe(const Surface *s, uint32_t output)
{
    const uint32_t description[] = {s->format, 1, s->usage, s->pool, 0, 0, s->width, s->height};
    memcpy(guest_pointer(output), description, sizeof description);
}

static bool surface(Game *g, const char *name)
{
    Surface *s = find_surface(g, arg(g, 0));
    if (!s)
        return false;
    if (IS("AddRef"))
        return result(g, ++s->refs, 4);
    if (IS("Release") && s->refs)
        return result(g, --s->refs, 4);
    if (IS("GetDesc")) {
        describe(s, arg(g, 1));
        return result(g, S_OK, 8);
    }
    if (IS("LockRect"))
        return lock(g, s, arg(g, 1), arg(g, 2), 16);
    if (IS("UnlockRect")) {
        uint32_t self = arg(g, 0);
        unsigned mip = 0;
        for (unsigned i = 0; i < MAX_TEXTURES; ++i)
            for (unsigned j = 0; j < g->textures[i].levels; ++j)
                if (self == g->textures[i].surfaces[j])
                    mip = j;
        return unlock(g, s, mip, 4);
    }
    return false;
}

static bool texture(Game *g, const char *name)
{
    Texture *t = find_texture(g, arg(g, 0));
    if (!t)
        return false;
    if (IS("AddRef"))
        return result(g, ++t->refs, 4);
    if (IS("Release") && t->refs)
        return result(g, --t->refs, 4);
    if (IS("GetType"))
        return result(g, 3, 4);
    if (IS("GetLevelCount"))
        return result(g, t->levels, 4);
    unsigned level = arg(g, 1);
    if (level >= t->levels)
        return false;
    Surface *s = &t->mip[level];
    if (IS("GetSurfaceLevel")) {
        uint32_t *handle = &t->surfaces[level];
        if (!*handle)
            *handle = make_interface(g, &d3d_surface);
        if (!*handle)
            return false;
        wr32(arg(g, 2), *handle);
        s->refs++;
        return result(g, S_OK, 12);
    }
    if (IS("GetLevelDesc")) {
        describe(s, arg(g, 2));
        return result(g, S_OK, 12);
    }
    if (IS("LockRect"))
        return lock(g, s, arg(g, 2), arg(g, 3), 20);
    if (IS("UnlockRect"))
        return unlock(g, s, level, 8);
    return false;
}

static bool create_texture(Game *g, uint32_t width, uint32_t height)
{
    uint32_t levels = arg(g, 3);
    if (!width || !height || width > 4096 || height > 4096)
        return result(g, INVALID_CALL, 36);
    if (!levels) {
        uint32_t longest = width > height ? width : height;
        levels = 32 - __builtin_clz(longest);
    }
    if (levels > MAX_MIPS)
        return result(g, INVALID_CALL, 36);
    for (unsigned i = 0; i < MAX_TEXTURES; ++i) {
        Texture *t = &g->textures[i];
        if (t->handle)
            continue;
        t->handle = make_interface(g, &d3d_texture);
        if (!t->handle)
            return false;
        t->refs = 1;
        t->levels = levels;
        uint32_t usage = arg(g, 4), format = arg(g, 5), pool = arg(g, 6);
        for (unsigned m = 0; m < levels; ++m) {
            t->mip[m] = (Surface){.format = format, .usage = usage, .pool = pool,
                                  .width = width, .height = height, .refs = 1};
            if (width > 1)
                width >>= 1;
            if (height > 1)
                height >>= 1;
        }
        wr32(arg(g, 7), t->handle);
        return result(g, S_OK, 36);
    }
    return false;
}

static bool device(Game *g, const char *name)
{
    if (arg(g, 0) != g->device || !g->device)
        return false;
    uint32_t a = arg(g, 1), b = arg(g, 2);
    if (IS("AddRef"))
        return result(g, ++g->device_refs, 4);
    if (IS("Release") && g->device_refs)
        return result(g, --g->device_refs, 4);
    if (IS("TestCooperativeLevel"))
        return result(g, S_OK, 4);
    if (IS("GetDeviceCaps")) {
        caps(a, 0, 1);
        return result(g, S_OK, 8);
    }
    if (IS("GetRenderTarget") && !a) {
        g->back.refs++;
        wr32(b, g->backbuffer);
        return result(g, S_OK, 12);
    }
    if (IS("GetDepthStencilSurface") && g->depth_buffer) {
        g->depth.refs++;
        wr32(a, g->depth_buffer);
        return result(g, S_OK, 8);
    }
    if (IS("CreateTexture"))
        return create_texture(g, a, b);
    if (IS("SetRenderState") && a < 256) {
        g->raster[a] = b;
        return result(g, S_OK, 12);
    }
    if (IS("SetSamplerState") && a < 16 && b < 16) {
        g->samplers[a][b] = arg(g, 3);
        return result(g, S_OK, 16);
    }
    if (IS("SetTextureStageState") && a < 8 && b < 64) {
        g->stages[a][b] = arg(g, 3);
        return result(g, S_OK, 16);
    }
    if (IS("SetTransform") && a < 32) {
        memcpy(g->transforms + 16 * a, guest_pointer(b), 64);
        return result(g, S_OK, 12);
    }
    if (IS("SetViewport")) {
        memcpy(g->viewport, guest_pointer(a), sizeof g->viewport);
        return result(g, S_OK, 8);
    }
    if (IS("Clear") && a <= 1) {
        if (a) {
            uint32_t *rect = (uint32_t *)guest_pointer(b);
            if (rect[0] || rect[1] || rect[2] != g->back.width || rect[3] != g->back.height)
                return false;
        }
        if (arg(g, 3) & 1)
            g->clear = arg(g, 4);
        g->clears++;
        return result(g, S_OK, 28);
    }
    if (IS("Present") && !a && !b && !arg(g, 3) && !arg(g, 4)) {
        ++g->presents;
        fprintf(g->log, "Present #%u, Clear calls %u, ARGB %08x\n", g->presents, g->clears, g->clear);
        return result(g, S_OK, 20);
    }
    return false;
}

bool d3d_dispatch(Game *g, const Import *call)
{
    const char *name = call->name;
    if (IS("LoadLibraryA") && !strcasecmp((const char *)guest_pointer(arg(g, 0)), "d3d9.dll"))
        return result(g, MODULE, 4);
    if (IS("GetProcAddress") && arg(g, 0) == MODULE) {
        uint32_t function = arg(g, 1);
        bool match = function > 0xffff && !strcmp((const char *)guest_pointer(function), "Direct3DCreate9");
        return result(g, match ? add_import(g, "d3d9.dll", "Direct3DCreate9") : 0, 8);
    }
    if (IS("Direct3DCreate9")) {
        if (arg(g, 0) != 32)
            return result(g, 0, 4);
        if (!g->d3d)
            g->d3d = make_interface(g, &d3d_root);
        if (g->d3d)
            g->d3d_refs++;
        return result(g, g->d3d, 4);
    }
    if (!strcmp(call->dll, d3d_root.dll))
        return root(g, name);
    if (!strcmp(call->dll, d3d_device.dll))
        return device(g, name);
    if (!strcmp(call->dll, d3d_surface.dll))
        return surface(g, name);
    if (!strcmp(call->dll, d3d_texture.dll))
        return texture(g, name);
    return false;
}
