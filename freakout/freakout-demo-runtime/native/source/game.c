#include "game.h"
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#ifdef __SWITCH__
#include <switch.h>
#else
#include <openssl/sha.h>
#endif

#include "game_data.inc"
#include "interfaces.inc"

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

uint8_t *game_bytes(Game *game, uint32_t address, size_t length)
{
    if (address < LOW_SIZE && length <= LOW_SIZE - address)
        return game->low + address;
    if (address >= RUNTIME_BASE && address - RUNTIME_BASE < RUNTIME_SIZE &&
        length <= RUNTIME_SIZE - (address - RUNTIME_BASE))
        return game->runtime + (address - RUNTIME_BASE);
    return NULL;
}

static uint16_t le16(const uint8_t *p)
{
    return p[0] | p[1] << 8;
}

static uint32_t le32(const uint8_t *p)
{
    return le16(p) | (uint32_t)le16(p + 2) << 16;
}

static bool fits(size_t position, size_t length, size_t limit)
{
    return position <= limit && length <= limit - position;
}

void hash_bytes(const void *data, size_t size, uint8_t digest[32])
{
#ifdef __SWITCH__
    sha256CalculateHash(digest, data, size);
#else
    SHA256(data, size, digest);
#endif
}

void log_hash(FILE *log, const void *data, size_t size)
{
    uint8_t digest[32];
    hash_bytes(data, size, digest);
    for (unsigned i = 0; i < sizeof digest; ++i)
        fprintf(log, "%02x", (unsigned)digest[i]);
}
static bool load_image(Game *game, const Module *module)
{
    FILE *file = open_asset(game, module->name);
    if (!file) {
        fprintf(game->log, "Missing %s\n", module->name);
        return false;
    }
    size_t size = module->size;
    uint8_t *raw = malloc(size);
    bool read = raw && fread(raw, 1, size, file) == size && fgetc(file) == EOF && !ferror(file);
    fclose(file);
    if (!read) {
        free(raw);
        return false;
    }
    uint8_t digest[32];
    hash_bytes(raw, size, digest);
    if (memcmp(digest, module->sha256, sizeof digest)) {
        fprintf(game->log, "Unexpected version of %s\n", module->name);
        free(raw);
        return false;
    }

    // The exact demo modules were validated above. Bounds still govern all copies.
    size_t pe = le32(raw + 0x3c);
    size_t optional = pe + 24;
    size_t count = le16(raw + pe + 6);
    size_t sections = optional + le16(raw + pe + 20);
    size_t image = le32(raw + optional + 56), headers = le32(raw + optional + 60);
    uint8_t *target = game_bytes(game, module->base, image);
    bool valid = target && le32(raw + optional + 28) == module->base &&
                 fits(0, headers, image) && fits(0, headers, size) && fits(sections, count * 40, headers);
    if (valid)
        memcpy(target, raw, headers);
    for (size_t i = 0; valid && i < count; ++i) {
        const uint8_t *section = raw + sections + i * 40;
        uint32_t rva = le32(section + 12), bytes = le32(section + 16), offset = le32(section + 20);
        valid = fits(rva, bytes, image) && fits(offset, bytes, size);
        if (valid && bytes)
            memcpy(target + rva, raw + offset, bytes);
    }
    free(raw);
    return valid;
}

static size_t span_index(const Heap *g, uint32_t start)
{
    size_t lo = 0, hi = g->span_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (g->spans[mid].start < start)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

static size_t free_span_index(const Heap *g, uint32_t start, uint32_t size)
{
    size_t lo = 0, hi = g->free_span_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        uint32_t bytes = g->free_spans[mid].size;
        if (bytes < size || (bytes == size && g->free_spans[mid].start < start))
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

static void remove_free_span(Heap *g, size_t i)
{
    memmove(g->free_spans + i, g->free_spans + i + 1,
            (--g->free_span_count - i) * sizeof *g->free_spans);
}

static void insert_free_span(Heap *g, uint32_t start, uint32_t size)
{
    size_t i = free_span_index(g, start, size);
    memmove(g->free_spans + i + 1, g->free_spans + i,
            (g->free_span_count++ - i) * sizeof *g->free_spans);
    g->free_spans[i].start = start;
    g->free_spans[i].size = size;
}

static uint32_t heap_allocate(Heap *g, uint32_t size)
{
    if (size > LOW_SIZE - HEAP_START)
        return 0;
    size = ((size ? size : 1) + 15) & ~15u;
    size_t fit = free_span_index(g, 0, size);
    if (fit == g->free_span_count) {
        if (g->span_count == MAX_SPANS || size > g->limit - g->top)
            return 0;
        uint32_t address = g->top;
        g->spans[g->span_count++] = (Span){address, size, true};
        g->top += size;
        return address;
    }
    uint32_t address = g->free_spans[fit].start;
    size_t index = span_index(g, address);
    Span *span = g->spans + index;
    uint32_t remaining = span->size - size;
    if (remaining && g->span_count == MAX_SPANS)
        return 0;
    remove_free_span(g, fit);
    *span = (Span){address, size, true};
    if (remaining) {
        memmove(span + 2, span + 1, (g->span_count++ - index - 1) * sizeof *span);
        span[1] = (Span){address + size, remaining, false};
        insert_free_span(g, address + size, remaining);
    }
    return address;
}

static bool heap_release(Heap *g, uint32_t address)
{
    if (!address)
        return true;
    size_t i = span_index(g, address);
    Span *span = g->spans + i;
    if (i == g->span_count || span->start != address || !span->used)
        return false;
    span->used = false;
    if (i && !span[-1].used) {
        --i;
        --span;
        remove_free_span(g, free_span_index(g, span->start, span->size));
        span->size += span[1].size;
        memmove(span + 1, span + 2, (--g->span_count - i - 1) * sizeof *span);
    }
    if (i + 1 < g->span_count && !span[1].used) {
        remove_free_span(g, free_span_index(g, span[1].start, span[1].size));
        span->size += span[1].size;
        memmove(span + 1, span + 2, (--g->span_count - i - 1) * sizeof *span);
    }
    if (i + 1 == g->span_count) {
        g->top = span->start;
        --g->span_count;
    } else {
        insert_free_span(g, span->start, span->size);
    }
    return true;
}

uint32_t allocate(Game *g, uint32_t size)
{
    return heap_allocate(&g->heap, size);
}

uint32_t reallocate(Game *g, uint32_t address, uint32_t size)
{
    if (!address)
        return allocate(g, size);
    Heap *heap = &g->heap;
    size_t i = span_index(heap, address);
    if (i == heap->span_count || heap->spans[i].start != address || !heap->spans[i].used)
        return 0;
    if (!size) {
        heap_release(heap, address);
        return 0;
    }
    uint32_t previous = heap->spans[i].size;
    if (size <= previous)
        return address;
    uint32_t next = heap_allocate(heap, size);
    if (!next)
        return 0;
    memcpy(guest_pointer(next), guest_pointer(address), previous);
    heap_release(heap, address);
    return next;
}

uint32_t resource_allocate(Game *g, uint32_t size)
{
    return heap_allocate(&g->resources, size);
}

bool release(Game *g, uint32_t address)
{
    return heap_release(address < RESOURCE_HEAP_START ? &g->heap : &g->resources, address);
}

uint32_t arg(Game *game, unsigned index)
{
    return rd32(game->cpu.esp + 4 + 4 * index);
}

bool result(Game *game, uint32_t eax, unsigned pop)
{
    CPU *c = &game->cpu;
    c->eip = rd32(c->esp);
    c->esp += 4 + pop;
    c->eax = eax;
    return true;
}

uint32_t add_import(Game *game, const char *dll, const char *name)
{
    if (game->import_count == MAX_IMPORTS)
        return 0;
    uint32_t address = API_BASE + 16 * game->import_count;
    game->imports[game->import_count++] = (Import){dll, name};
    return address;
}

uint32_t make_interface(Game *game, const Methods *methods)
{
    uint32_t object = resource_allocate(game, 4);
    if (!object)
        return 0;
    for (size_t i = 0; i < game->vtable_count; ++i) {
        if (game->vtables[i].methods == methods) {
            wr32(object, game->vtables[i].address);
            return object;
        }
    }
    if (game->vtable_count == COUNT(game->vtables) || methods->count > MAX_IMPORTS - game->import_count) {
        release(game, object);
        return 0;
    }
    uint32_t table = resource_allocate(game, 4 * methods->count);
    if (!table) {
        release(game, object);
        return 0;
    }
    wr32(object, table);
    for (size_t i = 0; i < methods->count; ++i)
        wr32(table + 4 * i, add_import(game, methods->dll, methods->names[i]));
    game->vtables[game->vtable_count].methods = methods;
    game->vtables[game->vtable_count++].address = table;
    return object;
}

bool asset_path(const Game *game, const char *name, char path[1024])
{
    size_t length = strlen(game->assets);
    if (length + 2 >= 1024 || *name == '/' || *name == '\\')
        return false;
    memcpy(path, game->assets, length);
    path[length++] = '/';
    while (*name) {
        size_t count = strcspn(name, "/\\:");
        if (!count || name[count] == ':' ||
            (count == 2 && !memcmp(name, "..", 2)) || length + count + 1 >= 1024)
            return false;
        if (count != 1 || *name != '.') {
            memcpy(path + length, name, count);
            length += count;
            if (name[count])
                path[length++] = '/';
        }
        name += count;
        while (*name == '/' || *name == '\\')
            ++name;
    }
    path[length] = 0;
    return true;
}

FILE *open_asset(const Game *game, const char *name)
{
    char path[1024];
    return asset_path(game, name, path) ? fopen(path, "rb") : NULL;
}

bool game_init(Game *game, const char *assets, FILE *log)
{
    game->assets = assets;
    game->log = log;
    game->low = calloc(1, LOW_SIZE);
    game->runtime = calloc(1, RUNTIME_SIZE);
    if (!game->low || !game->runtime) {
        game->stop = "guest allocation failed";
        return false;
    }
    for (size_t i = 0; i < COUNT(modules); ++i)
        if (!load_image(game, &modules[i])) {
            game->stop = "could not load exact Windows demo modules";
            return false;
        }
    memcpy(game->imports, initial_imports, sizeof initial_imports);
    game->import_count = COUNT(initial_imports);
    for (size_t i = 0; i < COUNT(bindings); ++i) {
        uint8_t *slot = game_bytes(game, bindings[i].slot, 4);
        if (!slot) {
            game->stop = "IAT entry out of bounds";
            return false;
        }
        memcpy(slot, &bindings[i].target, 4);
    }
    uint32_t command = COMMAND, except = UINT32_MAX;
    memcpy(game->low, &except, 4);
    memcpy(game->low + BSS, &command, 4);
    memcpy(game->low + COMMAND, "FreakOut.exe", sizeof "FreakOut.exe");
    memcpy(game->low + ARGV, &command, 4);
    memcpy(game->low + BSS + 0x402, ctype_table, sizeof ctype_table);
    game->heap.top = HEAP_START;
    game->heap.limit = RESOURCE_HEAP_START;
    game->resources.top = RESOURCE_HEAP_START;
    game->resources.limit = LOW_SIZE;
    game->cpu.esp = STACK;
    game->cpu.eip = PE_ENTRY;
    game->random = 1;
    game->thread_count = 1;
    return true;
}

static bool next_constructor(Game *game)
{
    if (!game->ctor_count)
        return false;
    Constructors *list = &game->ctors[game->ctor_count - 1];
    while (list->next < list->end) {
        uint32_t function = rd32(list->next);
        list->next += 4;
        if (function) {
            game->cpu.esp = list->stack;
            wr32(list->stack, CTOR_GATE);
            game->cpu.eip = function;
            return true;
        }
    }
    game->cpu.eip = list->ret;
    game->cpu.esp = list->stack + 4;
    game->ctor_count--;
    return true;
}

bool game_step(Game *game)
{
    game->recent[game->recent_count++ % COUNT(game->recent)] = game->cpu.eip;
    if (game->cpu.eip == FO_SKIN_DEFORM || game->cpu.eip == FO_SKIN_MATRICES)
        skin_observe(game);
    // WinMain is recognized in the generated dispatcher once without skipping its body.
    uint32_t address = advance(game->low, game->runtime, &game->cpu);
    if (!address || ((address == FO_SKIN_DEFORM || address == FO_SKIN_MATRICES) && has_block(address)))
        return true;
    if (address == GAME_ENTRY && !game->winmain && has_block(address)) {
        game->winmain = true;
        fprintf(game->log, "WinMain(%08x, %08x, %s, %u)\n",
                arg(game, 0), arg(game, 1), guest_pointer(arg(game, 2)), arg(game, 3));
        return true;
    }
    if (address == THREAD_RETURN) {
        game->threads[game->current_thread].exited = true;
        return switch_thread(game);
    }
    if (address == CTOR_GATE && next_constructor(game))
        return true;
    if (address == WINDOW_RETURN && game->window_depth) {
        unsigned depth = --game->window_depth;
        game->cpu.esp = game->window_callbacks[depth].stack + 8;
        game->cpu.eip = game->window_callbacks[depth].ret;
        return true;
    }
    if (address >= API_BASE && !(address & 15) && (address - API_BASE) / 16 < game->import_count) {
        const Import *call = &game->imports[(address - API_BASE) / 16];
        game->calls++;
        unsigned previous = game->current_thread;
        bool handled;
        if (!strcmp(call->name, "_initterm") && game->ctor_count < MAX_CTORS) {
            uint32_t first = arg(game, 0), end = arg(game, 1), stack = game->cpu.esp;
            game->ctors[game->ctor_count++] = (Constructors){first, end, stack, rd32(stack)};
            handled = next_constructor(game);
        } else {
            handled = d3d_dispatch(game, call) || input_dispatch(game, call) ||
                      thread_dispatch(game, call) || win32_dispatch(game, call) || crt_dispatch(game, call) ||
                      media_dispatch(game, call) || file_dispatch(game, call);
        }
        if (handled) {
            if (game->thread_count > 1 && previous == game->current_thread && ++game->import_slice >= 8192) {
                ++game->clock;
                return switch_thread(game);
            }
            return true;
        }
        fprintf(game->log, "Unhandled import %s!%s at %08x from %08x\n",
                call->dll, call->name, address, rd32(game->cpu.esp));
    } else {
        fprintf(game->log, "Untranslated code at %08x esp=%08x\n", address, game->cpu.esp);
        fputs("Recent dispatcher entries:", game->log);
        unsigned count = game->recent_count < COUNT(game->recent) ? game->recent_count : COUNT(game->recent);
        for (unsigned i = game->recent_count - count; i < game->recent_count; ++i)
            fprintf(game->log, " %08x", game->recent[i % COUNT(game->recent)]);
        fputc('\n', game->log);
    }
    if (!game->stop)
        game->stop = "runtime stopped";
    return false;
}

void game_close(Game *game)
{
    for (size_t i = 0; i < MAX_FILES; ++i)
        if (game->files[i])
            fclose(game->files[i]);
    for (size_t i = 0; i < MAX_FILES; ++i) {
        if (game->pending_save[i])
            unlink(game->pending_save[i]);
        free(game->pending_save[i]);
        free(game->save_path[i]);
    }
    file_close_searches(game);
    audio_bank_close(game);
    media_close(game);
    for (unsigned i = 0; i < MAX_TEXTURES; ++i)
        for (unsigned j = 0; j < game->textures[i].levels; ++j)
            free(game->textures[i].mip[j].decoded);
    free(game->pixels);
    free(game->depth_pixels);
    free(game->low);
    free(game->runtime);
}
