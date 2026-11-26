#include "game.h"
#include <string.h>
#include <strings.h>

#define INVALID_CALL 0x8876086cu
#define INVALID_DATA 0x88760b59u
#define NO_INTERFACE 0x80004002u
#define OUT_OF_MEMORY 0x8007000eu
#define IS(s) (!strcmp(name, (s)))

enum { ASSEMBLY, DECLARATION, VERTEX, PIXEL };
static const char *const buffer_methods[] = {
    "QueryInterface", "AddRef", "Release", "GetBufferPointer", "GetBufferSize",
};
static const char *const declaration_methods[] = {
    "QueryInterface", "AddRef", "Release", "GetDevice", "GetDeclaration",
};
static const char *const shader_methods[] = {
    "QueryInterface", "AddRef", "Release", "GetDevice", "GetFunction",
};
static const Methods interfaces[] = {
    {"ID3DXBuffer", buffer_methods, 5},
    {"IDirect3DVertexDeclaration9", declaration_methods, 5},
    {"IDirect3DVertexShader9", shader_methods, 5},
    {"IDirect3DPixelShader9", shader_methods, 5},
};
static const uint8_t interface_ids[][16] = {
    {0x08, 0xfb, 0xa5, 0x8b, 0x95, 0x51, 0xe2, 0x40, 0xac, 0x58, 0x0d, 0x98, 0x9c, 0x3a, 0x01, 0x02},
    {0x9c, 0xc5, 0x13, 0xdd, 0xfa, 0x36, 0x98, 0x40, 0xa8, 0xfb, 0xc7, 0xed, 0x39, 0xdc, 0x85, 0x46},
    {0x7e, 0x55, 0xc5, 0xef, 0x65, 0x62, 0x13, 0x46, 0x8a, 0x94, 0x43, 0x85, 0x78, 0x89, 0xeb, 0x36},
    {0xdc, 0xdb, 0x3b, 0x6d, 0x02, 0x5b, 0x15, 0x44, 0xb8, 0x52, 0xce, 0x5e, 0x8b, 0xcc, 0xb2, 0x89},
};
typedef struct {
    const char *path;
    const uint32_t *tokens;
    size_t bytes, raw_bytes;
    uint8_t digest[32];
} ShaderSource;
#include "shaders.inc"

static Program *lookup(Game *g, uint32_t handle)
{
    for (unsigned i = 0; handle && i < MAX_PROGRAMS; ++i)
        if (g->programs[i].handle == handle)
            return &g->programs[i];
    return NULL;
}

const void *program_data(Game *g, uint32_t handle, size_t *bytes)
{
    const Program *program = lookup(g, handle);
    *bytes = program ? program->bytes : 0;
    return program ? game_bytes(g, program->data, program->bytes) : NULL;
}

static uint32_t drop(Game *g, Program *program)
{
    uint32_t refs = --program->refs;
    if (!refs) {
        release(g, program->data);
        release(g, program->handle);
        *program = (Program){0};
    }
    return refs;
}

static uint32_t create(Game *g, const void *source, size_t bytes, unsigned kind)
{
    for (unsigned i = 0; i < MAX_PROGRAMS; ++i) {
        if (g->programs[i].handle)
            continue;
        uint32_t data = resource_allocate(g, bytes);
        uint32_t handle = data ? make_interface(g, &interfaces[kind]) : 0;
        if (!handle) {
            release(g, data);
            return 0;
        }
        memcpy(guest_pointer(data), source, bytes);
        g->programs[i] = (Program){handle, 1, bytes, data, kind};
        return handle;
    }
    return 0;
}

static bool assemble_file(Game *g)
{
    uint32_t path = arg(g, 0), output = arg(g, 4), errors = arg(g, 5);
    if (!path || !output || !game_bytes(g, output, 4))
        return result(g, INVALID_CALL, 24);
    wr32(output, 0);
    if (errors) {
        if (!game_bytes(g, errors, 4))
            return result(g, INVALID_CALL, 24);
        wr32(errors, 0);
    }
    if (arg(g, 1) || arg(g, 2) || arg(g, 3))
        return result(g, 0x80004001u, 24);

    char filename[1024], root[1024];
    const char *guest_name = (const char *)game_bytes(g, path, sizeof filename);
    if (!guest_name || !memchr(guest_name, 0, sizeof filename) ||
        !asset_path(g, guest_name, filename) || !asset_path(g, "", root))
        return result(g, INVALID_DATA, 24);
    const char *relative = filename + strlen(root);
    for (size_t i = 0; i < sizeof sources / sizeof *sources; ++i) {
        const ShaderSource *source = &sources[i];
        if (strcasecmp(relative, source->path))
            continue;
        FILE *file = open_asset(g, source->path);
        if (!file)
            break;
        uint8_t raw[16384], digest[32];
        bool read = source->raw_bytes <= sizeof raw &&
                    fread(raw, 1, source->raw_bytes, file) == source->raw_bytes &&
                    fgetc(file) == EOF && !ferror(file);
        fclose(file);
        if (!read)
            break;
        hash_bytes(raw, source->raw_bytes, digest);
        if (memcmp(digest, source->digest, sizeof digest))
            break;
        uint32_t handle = create(g, source->tokens, source->bytes, ASSEMBLY);
        if (!handle)
            return result(g, OUT_OF_MEMORY, 24);
        wr32(output, handle);
        fprintf(g->log, "Assembled DX9 %s\n", relative);
        return result(g, 0, 24);
    }
    fprintf(g->log, "Shader missing from verified DX9 sources: %s\n", relative);
    return result(g, INVALID_DATA, 24);
}

static size_t declaration_bytes(Game *g, uint32_t address)
{
    static const uint8_t end[] = {255, 0, 0, 0, 17, 0, 0, 0};
    for (unsigned i = 0; i <= 64; ++i) {
        const uint8_t *p = game_bytes(g, address, 8 * (i + 1));
        if (!p)
            return 0;
        p += 8 * i;
        if (!memcmp(p, end, sizeof end))
            return i ? 8 * (i + 1) : 0;
        if (p[0] >= 16 || p[1] || p[4] >= 17 || p[5] > 6 || p[6] > 13 || p[7] > 15)
            return 0;
    }
    return 0;
}

static size_t shader_bytes(Game *g, uint32_t address, unsigned kind)
{
    static const uint8_t model_one_arity[] = {
        [1] = 2, [2] = 3, [3] = 3, [4] = 4, [5] = 3, [6] = 2, [7] = 2, [8] = 3,
        [9] = 3, [10] = 3, [11] = 3, [12] = 3, [13] = 3, [18] = 4, [19] = 2,
        [20] = 3, [21] = 3, [22] = 3, [23] = 3, [24] = 3, [31] = 2, [32] = 3,
        [36] = 2, [46] = 2, [64] = 1, [66] = 1, [80] = 4, [81] = 5, [88] = 4,
    };
    if (!game_bytes(g, address, 4))
        return 0;
    uint32_t version = rd32(address), profile = version & 0xffff;
    if (version >> 16 != (kind == VERTEX ? 0xfffeu : 0xffffu) ||
        (profile != 0x101 && profile != 0x200))
        return 0;
    for (unsigned i = 1; i < 4096;) {
        if (!game_bytes(g, address, 4 * (i + 1)))
            return 0;
        uint32_t token = rd32(address + 4 * i++), operation = token & 0xffff;
        if (token == 0xffff)
            return 4 * i;
        unsigned operands = 0;
        if (operation == 0xfffe)
            operands = (token >> 16) & 0x7fff;
        else if (profile == 0x200)
            operands = (token >> 24) & 15;
        else if (operation < sizeof model_one_arity)
            operands = model_one_arity[operation];
        if (!operands)
            return 0;
        i += operands;
    }
    return 0;
}

static bool create_program(Game *g, unsigned kind)
{
    uint32_t input = arg(g, 1), output = arg(g, 2);
    if (!input || !output || !game_bytes(g, output, 4))
        return result(g, INVALID_CALL, 12);
    wr32(output, 0);
    size_t bytes = kind == DECLARATION ? declaration_bytes(g, input) : shader_bytes(g, input, kind);
    if (!bytes)
        return result(g, INVALID_CALL, 12);
    uint32_t handle = create(g, guest_pointer(input), bytes, kind);
    wr32(output, handle);
    return result(g, handle ? 0 : OUT_OF_MEMORY, 12);
}

static bool bind(Game *g, uint32_t *binding, uint32_t handle, unsigned kind)
{
    Program *next = lookup(g, handle);
    if (handle && (!next || next->kind != kind))
        return false;
    Program *previous = lookup(g, *binding);
    if (next)
        ++next->refs;
    if (previous)
        drop(g, previous);
    *binding = handle;
    return true;
}

void program_clear_declaration(Game *g)
{
    bind(g, &g->declaration, 0, DECLARATION);
}

static bool device_call(Game *g, const char *name)
{
    uint32_t value = arg(g, 1);
    if (IS("CreateVertexDeclaration") || IS("CreateVertexShader") || IS("CreatePixelShader"))
        return create_program(g, IS("CreateVertexDeclaration") ? DECLARATION :
                                   IS("CreateVertexShader") ? VERTEX : PIXEL);
    if (IS("SetVertexDeclaration")) {
        if (!bind(g, &g->declaration, value, DECLARATION))
            return result(g, INVALID_CALL, 8);
        g->fvf = 0;
        return result(g, 0, 8);
    }
    if (IS("SetVertexShader") || IS("SetPixelShader")) {
        bool vertex = IS("SetVertexShader");
        bool bound = bind(g, vertex ? &g->vertex_shader : &g->pixel_shader, value, vertex ? VERTEX : PIXEL);
        return result(g, bound ? 0 : INVALID_CALL, 8);
    }
    if (IS("GetVertexDeclaration") || IS("GetVertexShader") || IS("GetPixelShader")) {
        if (!value || !game_bytes(g, value, 4))
            return result(g, INVALID_CALL, 8);
        uint32_t handle = IS("GetVertexDeclaration") ? g->declaration :
                          IS("GetVertexShader") ? g->vertex_shader : g->pixel_shader;
        Program *program = lookup(g, handle);
        if (program)
            ++program->refs;
        wr32(value, handle);
        return result(g, 0, 8);
    }
    if (IS("SetVertexShaderConstantF") || IS("SetPixelShaderConstantF") ||
        IS("GetVertexShaderConstantF") || IS("GetPixelShaderConstantF")) {
        bool vertex = strstr(name, "Vertex") != NULL;
        unsigned limit = vertex ? 256 : 32;
        uint32_t count = arg(g, 3), address = arg(g, 2);
        if (value > limit || count > limit - value)
            return result(g, INVALID_CALL, 16);
        if (!count)
            return result(g, 0, 16);
        void *parameter = address ? game_bytes(g, address, count * 16) : NULL;
        if (!parameter)
            return result(g, INVALID_CALL, 16);
        uint32_t (*constants)[4] = vertex ? g->vertex_constants : g->pixel_constants;
        if (*name == 'S')
            memcpy(constants[value], parameter, count * 16);
        else
            memcpy(parameter, constants[value], count * 16);
        return result(g, 0, 16);
    }
    return false;
}

static bool resource_call(Game *g, const Import *call, Program *program)
{
    static const uint8_t unknown[] = {0, 0, 0, 0, 0, 0, 0, 0, 0xc0, 0, 0, 0, 0, 0, 0, 0x46};
    const char *name = call->name;
    uint32_t a = arg(g, 1);
    if (IS("AddRef"))
        return result(g, ++program->refs, 4);
    if (IS("Release"))
        return result(g, drop(g, program), 4);
    if (IS("QueryInterface")) {
        uint32_t b = arg(g, 2);
        const uint8_t *iid = a ? game_bytes(g, a, 16) : NULL;
        if (!iid || !b || !game_bytes(g, b, 4))
            return result(g, INVALID_CALL, 12);
        bool accepted = !memcmp(iid, unknown, 16) || !memcmp(iid, interface_ids[program->kind], 16);
        wr32(b, accepted ? program->handle : 0);
        if (accepted)
            ++program->refs;
        return result(g, accepted ? 0 : NO_INTERFACE, 12);
    }
    if (IS("GetDevice")) {
        if (!a || !game_bytes(g, a, 4))
            return result(g, INVALID_CALL, 8);
        ++g->device_refs;
        wr32(a, g->device);
        return result(g, 0, 8);
    }
    if (IS("GetBufferPointer"))
        return result(g, program->data, 4);
    if (IS("GetBufferSize"))
        return result(g, program->bytes, 4);
    if (IS("GetFunction") || IS("GetDeclaration")) {
        uint32_t b = arg(g, 2);
        if (!b || !game_bytes(g, b, 4))
            return result(g, INVALID_CALL, 12);
        unsigned units = program->bytes / (IS("GetDeclaration") ? 8 : 1);
        uint32_t capacity = rd32(b);
        wr32(b, units);
        if (a && (capacity < units || !game_bytes(g, a, program->bytes)))
            return result(g, INVALID_CALL, 12);
        if (a)
            memcpy(guest_pointer(a), guest_pointer(program->data), program->bytes);
        return result(g, 0, 12);
    }
    return false;
}

bool program_dispatch(Game *g, const Import *call)
{
    const char *name = call->name;
    if (!strcasecmp(call->dll, "d3dx9_30.dll") && IS("D3DXLoadSurfaceFromSurface"))
        return d3dx_load_surface(g);
    if (!strcasecmp(call->dll, "d3dx9_30.dll"))
        return IS("D3DXAssembleShaderFromFileA") ? assemble_file(g) : d3dx_math_dispatch(g, name);
    if (!strcmp(call->dll, d3d_device.dll) && g->device && arg(g, 0) == g->device)
        return device_call(g, name);
    for (unsigned kind = 0; kind < sizeof interfaces / sizeof *interfaces; ++kind) {
        if (!strcmp(call->dll, interfaces[kind].dll)) {
            Program *program = lookup(g, arg(g, 0));
            return program && program->kind == kind && resource_call(g, call, program);
        }
    }
    return false;
}
