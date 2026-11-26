#include "game.h"
#include <stdlib.h>
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
        g->color_target = g->backbuffer;
        g->depth_target = g->depth_buffer;
        g->raster[D3D_COLOR_WRITE_ENABLE] = 15;
        ++g->back.refs;
        if (g->depth_buffer)
            ++g->depth.refs;
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

Surface *d3d_surface_lookup(Game *g, uint32_t handle, Texture **owner)
{
    if (!handle)
        return NULL;
    if (handle == g->backbuffer)
        return &g->back;
    if (handle == g->depth_buffer)
        return &g->depth;
    for (unsigned i = 0; i < MAX_SURFACES; ++i)
        if (g->surfaces[i].handle == handle)
            return &g->surfaces[i].image;
    for (unsigned i = 0; i < MAX_TEXTURES; ++i) {
        Texture *t = &g->textures[i];
        for (unsigned mip = 0; t->handle && mip < t->levels; ++mip)
            if (t->surfaces[mip] == handle) {
                if (owner)
                    *owner = t;
                return &t->mip[mip];
            }
    }
    return NULL;
}

static uint32_t drop_texture(Game *g, Texture *t)
{
    uint32_t refs = --t->refs;
    if (!refs) {
        for (unsigned level = 0; level < t->levels; ++level) {
            free(t->mip[level].decoded);
            free(t->mip[level].depth_samples);
            release(g, t->mip[level].data);
            release(g, t->surfaces[level]);
        }
        release(g, t->handle);
        *t = (Texture){0};
    }
    return refs;
}

static uint32_t release_surface(Game *g, uint32_t handle, Surface *surface, Texture *owner)
{
    uint32_t refs = --surface->refs;
    if (owner) {
        drop_texture(g, owner);
    } else if (!refs && surface != &g->back && surface != &g->depth) {
        for (unsigned i = 0; i < MAX_SURFACES; ++i) {
            if (g->surfaces[i].handle == handle) {
                free(surface->decoded);
                free(surface->depth_samples);
                release(g, surface->data);
                release(g, handle);
                g->surfaces[i].handle = 0;
                *surface = (Surface){0};
                break;
            }
        }
    }
    return refs;
}

static bool create_surface(Game *g, bool depth)
{
    unsigned width = arg(g, 1), height = arg(g, 2), format = arg(g, 3);
    uint32_t output = arg(g, 7);
    bool format_valid = depth ? format >= 70 && format <= 83 : format == 21 || format == 22;
    if (!width || !height || width > 4096 || height > 4096 || !format_valid ||
        arg(g, 4) || arg(g, 5) || !output || !game_bytes(g, output, 4))
        return result(g, INVALID_CALL, 36);
    for (unsigned i = 0; i < MAX_SURFACES; ++i) {
        if (g->surfaces[i].handle)
            continue;
        uint32_t handle = make_interface(g, &d3d_surface);
        if (!handle)
            break;
        g->surfaces[i].handle = handle;
        g->surfaces[i].image = (Surface){.format = format, .usage = depth ? 2 : 1,
                                         .width = width, .height = height, .refs = 1};
        wr32(output, handle);
        return result(g, S_OK, 36);
    }
    return result(g, 0x8007000eu, 36);
}

static bool set_target(Game *g, uint32_t handle, bool depth)
{
    uint32_t *binding = depth ? &g->depth_target : &g->color_target;
    Texture *new_owner = NULL, *old_owner = NULL;
    Surface *next = d3d_surface_lookup(g, handle, &new_owner);
    Surface *previous = d3d_surface_lookup(g, *binding, &old_owner);
    unsigned pop = depth ? 8 : 12;
    if ((!handle && !depth) || (handle && (!next || !(next->usage & (depth ? 2 : 1)))))
        return result(g, INVALID_CALL, pop);
    if (next)
        ++next->refs;
    if (new_owner)
        ++new_owner->refs;
    if (previous)
        release_surface(g, *binding, previous, old_owner);
    *binding = handle;
    if (!depth) {
        const uint32_t viewport[6] = {0, 0, next->width, next->height, 0, 0x3f800000u};
        memcpy(g->viewport, viewport, sizeof viewport);
    }
    return result(g, S_OK, pop);
}

static bool get_target(Game *g, uint32_t handle, uint32_t output, unsigned pop)
{
    Texture *owner = NULL;
    Surface *target = d3d_surface_lookup(g, handle, &owner);
    if (!output || !game_bytes(g, output, 4))
        return result(g, INVALID_CALL, pop);
    wr32(output, handle);
    if (!target)
        return result(g, 0x88760866u, pop);
    ++target->refs;
    if (owner)
        ++owner->refs;
    return result(g, S_OK, pop);
}

static uint32_t block_bytes(uint32_t format)
{
    if (format == 0x31545844u)
        return 8;
    return (format & 0x00ffffffu) == 0x00545844u && (format >> 24) >= '2' && (format >> 24) <= '5' ? 16 : 0;
}

uint32_t surface_pitch(const Surface *s)
{
    static const uint8_t pixel_bytes[] = {3, 4, 4, 2, 2, 2, 2, 0, 1};
    uint32_t block = block_bytes(s->format);
    if (block)
        return ((s->width + 3) / 4) * block;
    uint32_t bytes = s->format >= 20 && s->format <= 28 ? pixel_bytes[s->format - 20] : 0;
    return (s->width * bytes + 3) & ~3u;
}

uint32_t surface_bytes(const Surface *s)
{
    uint32_t rows = block_bytes(s->format) ? (s->height + 3) / 4 : s->height;
    return rows * surface_pitch(s);
}

static bool lock(Game *g, Surface *s, uint32_t output, uint32_t rectangle, unsigned pop)
{
    uint32_t size = surface_bytes(s);
    if (rectangle || !size || s->locked || !output || !game_bytes(g, output, 8)) {
        fprintf(g->log, "DX9 lock rejected: %ux%u format=%08x rectangle=%08x locked=%d\n",
                s->width, s->height, s->format, rectangle, s->locked);
        return result(g, INVALID_CALL, pop);
    }
    if (!s->data)
        s->data = resource_allocate(g, size);
    if (!s->data) {
        fprintf(g->log, "DX9 texture allocation failed: %u bytes, resource heap %u/%u bytes\n",
                size, g->resources.top - RESOURCE_HEAP_START, g->resources.limit - RESOURCE_HEAP_START);
        return result(g, INVALID_CALL, pop);
    }
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
    free(s->decoded);
    s->decoded = NULL;
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
    Texture *parent = NULL;
    Surface *s = d3d_surface_lookup(g, arg(g, 0), &parent);
    if (!s)
        return false;
    if (IS("AddRef")) {
        if (parent)
            ++parent->refs;
        return result(g, ++s->refs, 4);
    }
    if (IS("Release") && s->refs) {
        return result(g, release_surface(g, arg(g, 0), s, parent), 4);
    }
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
        return result(g, drop_texture(g, t), 4);
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
        t->refs++;
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

static Buffer *find_buffer(Game *g, uint32_t handle)
{
    for (unsigned i = 0; handle && i < MAX_BUFFERS; ++i)
        if (g->buffers[i].handle == handle)
            return &g->buffers[i];
    return NULL;
}

static uint32_t drop_buffer(Game *g, Buffer *buffer)
{
    uint32_t refs = --buffer->refs;
    if (!refs) {
        release(g, buffer->data);
        release(g, buffer->handle);
        *buffer = (Buffer){0};
    }
    return refs;
}

static bool bind_buffer(Game *g, uint32_t *binding, uint32_t handle, uint32_t kind)
{
    Buffer *next = find_buffer(g, handle);
    if (handle && (!next || next->kind != kind))
        return false;
    Buffer *previous = find_buffer(g, *binding);
    if (next)
        ++next->refs;
    if (previous)
        drop_buffer(g, previous);
    *binding = handle;
    return true;
}

static bool create_buffer(Game *g, bool vertex)
{
    uint32_t bytes = arg(g, 1), output = arg(g, 5);
    if (!bytes || !output)
        return result(g, INVALID_CALL, 28);
    for (unsigned i = 0; i < MAX_BUFFERS; ++i) {
        Buffer *b = &g->buffers[i];
        if (b->handle)
            continue;
        uint32_t data = resource_allocate(g, bytes);
        if (!data)
            return result(g, 0x8007000eu, 28);
        uint32_t handle = make_interface(g, vertex ? &d3d_vertex_buffer : &d3d_index_buffer);
        if (!handle) {
            release(g, data);
            return result(g, 0x8007000eu, 28);
        }
        *b = (Buffer){.handle = handle, .refs = 1, .bytes = bytes, .data = data,
                      .usage = arg(g, 2), .format = arg(g, 3), .pool = arg(g, 4), .kind = vertex ? 6 : 7};
        wr32(output, handle);
        return result(g, S_OK, 28);
    }
    return result(g, 0x8007000eu, 28);
}

static bool buffer(Game *g, const char *name)
{
    Buffer *b = find_buffer(g, arg(g, 0));
    if (!b)
        return false;
    if (IS("AddRef"))
        return result(g, ++b->refs, 4);
    if (IS("Release") && b->refs)
        return result(g, drop_buffer(g, b), 4);
    if (IS("GetType"))
        return result(g, b->kind, 4);
    if (IS("GetDesc")) {
        uint32_t desc[] = {b->kind == 6 ? 100 : b->format, b->kind, b->usage, b->pool, b->bytes, b->format};
        memcpy(guest_pointer(arg(g, 1)), desc, (b->kind == 6 ? 6 : 5) * sizeof(uint32_t));
        return result(g, S_OK, 8);
    }
    if (IS("Lock")) {
        uint32_t offset = arg(g, 1), size = arg(g, 2), output = arg(g, 3);
        if (b->locked || !output || offset > b->bytes || size > b->bytes - offset)
            return result(g, INVALID_CALL, 20);
        b->locked = true;
        b->lock_start = offset;
        b->lock_size = size ? size : b->bytes - offset;
        wr32(output, b->data + offset);
        return result(g, S_OK, 20);
    }
    if (IS("Unlock")) {
        if (!b->locked)
            return result(g, INVALID_CALL, 4);
        b->locked = false;
        return result(g, S_OK, 4);
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
    if (IS("CreateDepthStencilSurface") || IS("CreateRenderTarget"))
        return create_surface(g, IS("CreateDepthStencilSurface"));
    if (IS("GetRenderTarget"))
        return !a ? get_target(g, g->color_target, b, 12) : result(g, INVALID_CALL, 12);
    if (IS("GetDepthStencilSurface"))
        return get_target(g, g->depth_target, a, 8);
    if (IS("SetRenderTarget"))
        return !a ? set_target(g, b, false) : result(g, INVALID_CALL, 12);
    if (IS("SetDepthStencilSurface"))
        return set_target(g, a, true);
    if (IS("CreateVertexBuffer") || IS("CreateIndexBuffer"))
        return create_buffer(g, IS("CreateVertexBuffer"));
    if (IS("BeginScene") || IS("EndScene")) {
        if (g->scene == IS("BeginScene"))
            return result(g, INVALID_CALL, 4);
        g->scene = IS("BeginScene");
        return result(g, S_OK, 4);
    }
    if (IS("SetMaterial")) {
        memcpy(g->material, guest_pointer(a), sizeof g->material);
        return result(g, S_OK, 8);
    }
    if (IS("SetLight") || IS("LightEnable")) {
        size_t slot = 0, length = sizeof g->lights / sizeof *g->lights;
        while (slot < length && g->lights[slot].defined && g->lights[slot].index != a)
            ++slot;
        const void *data = IS("SetLight") && b ? game_bytes(g, b, sizeof g->lights[0].values) : NULL;
        if (slot == length || (IS("SetLight") && !data))
            return result(g, INVALID_CALL, 12);
        g->lights[slot].index = a;
        g->lights[slot].defined = true;
        if (data)
            memcpy(g->lights[slot].values, data, sizeof g->lights[slot].values);
        else
            g->lights[slot].enabled = b != 0;
        return result(g, S_OK, 12);
    }
    if (IS("SetTexture") && a < 16) {
        Texture *next = find_texture(g, b);
        if (b && !next)
            return result(g, INVALID_CALL, 12);
        Texture *previous = find_texture(g, g->texture_slots[a]);
        if (next)
            ++next->refs;
        if (previous)
            drop_texture(g, previous);
        g->texture_slots[a] = b;
        return result(g, S_OK, 12);
    }
    if (IS("SetStreamSource") && a == 0 && bind_buffer(g, &g->stream, b, 6)) {
        g->stream_offset = arg(g, 3);
        g->stride = arg(g, 4);
        return result(g, S_OK, 20);
    }
    if (IS("SetFVF")) {
        program_clear_declaration(g);
        g->fvf = a;
        return result(g, S_OK, 8);
    }
    if (IS("SetIndices") && bind_buffer(g, &g->indices, a, 7)) {
        return result(g, S_OK, 8);
    }
    if ((IS("DrawIndexedPrimitive") || IS("DrawPrimitive")) && g->scene) {
        bool indexed = IS("DrawIndexedPrimitive");
        Buffer *vb = find_buffer(g, g->stream), *ib = indexed ? find_buffer(g, g->indices) : NULL;
        uint64_t start = native_now_ns();
        bool rendered = vb && (!indexed || ib) && render_geometry(g, vb, ib);
        g->render_ns += native_now_ns() - start;
        if (!rendered) {
            fprintf(g->log, "DX9 draw rejected: type=%u vertices=%u triangles=%u FVF=%x stride=%u "
                    "texture=%08x vertexShader=%08x pixelShader=%08x depth=%u/%u blend=%u/%u/%u "
                    "colorMask=%x stencil=%u\n",
                    a, indexed ? arg(g, 4) : 0, arg(g, indexed ? 6 : 3), g->fvf, g->stride,
                    g->texture_slots[0], g->vertex_shader, g->pixel_shader,
                    g->raster[7], g->raster[23], g->raster[27], g->raster[19], g->raster[20],
                    g->raster[D3D_COLOR_WRITE_ENABLE], g->raster[52]);
            return false;
        }
        ++g->draws;
        ++g->frame_draws;
        return result(g, S_OK, indexed ? 28 : 16);
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
    if (IS("SetTransform") && a < 257) {
        memcpy(g->transforms[a], guest_pointer(b), 64);
        return result(g, S_OK, 12);
    }
    if (IS("SetViewport")) {
        memcpy(g->viewport, guest_pointer(a), sizeof g->viewport);
        return result(g, S_OK, 8);
    }
    if (IS("Clear") && a <= 1) {
        Surface *target = d3d_surface_lookup(g, g->color_target, NULL);
        if (a) {
            uint32_t *rect = (uint32_t *)guest_pointer(b);
            if (!target || rect[0] || rect[1] || rect[2] != target->width || rect[3] != target->height)
                return false;
        }
        if (arg(g, 3) & 1)
            g->clear = arg(g, 4);
        uint64_t start = native_now_ns();
        bool cleared = render_clear(g, arg(g, 3), g->clear, rdf32(g->cpu.esp + 24));
        g->render_ns += native_now_ns() - start;
        if (!cleared)
            return false;
        g->clears++;
        return result(g, S_OK, 28);
    }
    if (IS("Present") && !a && !b && !arg(g, 3) && !arg(g, 4)) {
        ++g->presents;
        g->presented_draws = g->frame_draws;
        g->frame_draws = 0;
        if (g->presents <= 7 || !(g->presents % 60))
            fprintf(g->log, "Present #%u, Clear calls %u, ARGB %08x\n", g->presents, g->clears, g->clear);
        return result(g, S_OK, 20);
    }
    return false;
}

bool d3d_dispatch(Game *g, const Import *call)
{
    const char *name = call->name;
    if (program_dispatch(g, call))
        return true;
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
    if (!strcmp(call->dll, d3d_vertex_buffer.dll) || !strcmp(call->dll, d3d_index_buffer.dll))
        return buffer(g, name);
    if (!strcmp(call->dll, d3d_texture.dll))
        return texture(g, name);
    return false;
}
