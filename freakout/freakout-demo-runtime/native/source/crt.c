#include "game.h"
#include <ctype.h>
#include <math.h>
#include <sys/stat.h>
#include "exports.inc"
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const char *str(uint32_t address)
{
    return (const char *)guest_pointer(address);
}

static bool open_file(Game *g, uint32_t name, uint32_t mode)
{
    if (strcmp(str(mode), "rb") && strcmp(str(mode), "r"))
        return false;
    FILE *file = open_asset(g, str(name));
    if (!file)
        return result(g, 0, 0);
    for (unsigned i = 0; i < MAX_FILES; ++i) {
        if (!g->files[i]) {
            uint32_t handle = BSS + 0x1000 + 32 * i;
            memset(guest_pointer(handle), 0, 32);
            wr32(handle + 12, 1);
            g->files[i] = file;
            g->archive_file[i] = !strcasecmp(str(name), "Data2.bnd");
            if (g->archive_file[i])
                fprintf(g->log, "Opened game archive Data2.bnd\n");
            return result(g, handle, 0);
        }
    }
    fclose(file);
    return result(g, 0, 0);
}

static bool file_call(Game *g, const char *name, uint32_t arguments, uint32_t handle)
{
    uint32_t slot = handle - (BSS + 0x1000);
    if (slot % 32 || slot / 32 >= MAX_FILES || !g->files[slot / 32])
        return false;
    FILE *file = g->files[slot / 32];
    if (!strcmp(name, "fclose")) {
        g->files[slot / 32] = NULL;
        return result(g, fclose(file), 0);
    }
    if (!strcmp(name, "fseek")) {
        int value = fseek(file, (int32_t)arg(g, 1), arg(g, 2));
        if (!value)
            wr32(handle + 12, rd32(handle + 12) & ~0x10);
        return result(g, value, 0);
    }
    uint32_t size = arg(g, 1), count = arg(g, 2);
    if (!size || !count)
        return result(g, 0, 0);
    uint64_t requested = (uint64_t)size * count;
    void *output = requested <= LOW_SIZE ? game_bytes(g, arguments, requested) : NULL;
    if (!output)
        return false;
    long offset = g->archive_file[slot / 32] ? ftell(file) : -1;
    size_t read = fread(output, 1, requested, file);
    if (offset >= 0 && read) {
        ++g->archive_reads;
        fprintf(g->log, "Archive read #%u offset=%ld size=%zu sha256=", g->archive_reads, offset, read);
        log_hash(g->log, output, read);
        fputc('\n', g->log);
    }
    g->file_bytes += read;
    if (feof(file))
        wr32(handle + 12, rd32(handle + 12) | 0x10);
    if (ferror(file))
        wr32(handle + 12, rd32(handle + 12) | 0x20);
    return result(g, read / size, 0);
}

static bool format(Game *g, uint32_t output, uint32_t pattern, uint32_t arguments)
{
    char data[4096];
    size_t offset = 0;
    for (const char *p = str(pattern); *p;) {
        if (*p != '%') {
            if (offset >= sizeof data - 1)
                return false;
            data[offset++] = *p++;
            continue;
        }
        char spec[32];
        const char *start = p++;
        p += strspn(p, "-+ #0.123456789");
        if (!*p || (size_t)(p - start) + 2 > sizeof spec)
            return false;
        const char conversion = *p++;
        size_t length = p - start;
        memcpy(spec, start, length);
        spec[length] = 0;
        int written;
        char *dest = data + offset;
        size_t available = sizeof data - offset;
        if (conversion == '%') {
            written = snprintf(dest, available, "%%");
        } else if (conversion == 's') {
            uint32_t pointer = rd32(arguments);
            written = snprintf(dest, available, spec, pointer ? str(pointer) : "(null)");
        } else if (strchr("di", conversion)) {
            written = snprintf(dest, available, spec, (int32_t)rd32(arguments));
        } else if (strchr("xXuo", conversion)) {
            written = snprintf(dest, available, spec, rd32(arguments));
        } else if (conversion == 'c') {
            written = snprintf(dest, available, spec, (int)rd32(arguments));
        } else if (strchr("fFeEgG", conversion)) {
            written = snprintf(dest, available, spec, rdf64(arguments));
            arguments += 4;
        } else {
            return false;
        }
        if (conversion != '%')
            arguments += 4;
        if (written < 0 || (size_t)written >= available)
            return false;
        offset += written;
    }
    data[offset] = 0;
    void *destination = game_bytes(g, output, offset + 1);
    if (!destination)
        return false;
    memcpy(destination, data, offset + 1);
    return result(g, offset, 0);
}

bool crt_dispatch(Game *g, const Import *call)
{
    if (strcmp(call->dll, "MSVCR71.dll") && strcmp(call->dll, "msvcr71.dll"))
        return false;
    const char *name = call->name;
    #define IS(s) (!strcmp(name, (s)))
    uint32_t a = arg(g, 0);
    if (IS("__p__fmode") || IS("__p__commode"))
        return result(g, BSS + (IS("__p__fmode") ? 8 : 12), 0);
    if (IS("__set_app_type")) {
        wr32(BSS + 0x20, a);
        return result(g, 0, 0);
    }
    if (IS("_controlfp")) {
        uint32_t flags = (rd32(BSS + 0x24) & ~arg(g, 1)) | (a & arg(g, 1));
        wr32(BSS + 0x24, flags);
        return result(g, flags, 0);
    }
    if (IS("_onexit"))
        return result(g, a, 0);
    if (IS("__getmainargs")) {
        wr32(a, 1);
        wr32(arg(g, 1), ARGV);
        wr32(arg(g, 2), ARGV + 4);
        return result(g, 0, 0);
    }
    if (IS("___setlc_active_func"))
        return result(g, rd32(BSS + 0x210), 0);
    if (IS("___unguarded_readlc_active_add_func"))
        return result(g, BSS + 0x214, 0);
    if (IS("___lc_handle_func"))
        return result(g, BSS + 0x220, 0);
    if (IS("__pctype_func"))
        return result(g, BSS + 0x402, 0);
    if (IS("___lc_codepage_func") || IS("_ismbblead"))
        return result(g, 0, 0); // C locale uses single byte characters.
    if (IS("setlocale")) {
        if (arg(g, 1)) {
            const char *locale = str(arg(g, 1));
            if (*locale && strcmp(locale, "C"))
                return result(g, 0, 0);
        }
        memcpy(guest_pointer(BSS + 0x200), "C", 2);
        return result(g, BSS + 0x200, 0);
    }
    if (IS("malloc") || IS("??2@YAPAXI@Z"))
        return result(g, allocate(g, a), 0);
    if (IS("free") || IS("??3@YAXPAX@Z") || IS("??_V@YAXPAX@Z"))
        return release(g, a) && result(g, 0, 0);
    if (IS("fopen"))
        return open_file(g, a, arg(g, 1));
    if (IS("fread") || IS("fseek") || IS("fclose"))
        return file_call(g, name, a, IS("fread") ? arg(g, 3) : a);
    if (IS("srand")) {
        g->random = a;
        return result(g, 0, 0);
    }
    if (IS("rand")) {
        g->random = g->random * 214013 + 2531011;
        return result(g, (g->random >> 16) & 0x7fff, 0);
    }
    if (IS("toupper") || IS("tolower"))
        return result(g, a > 255 ? a : (uint32_t)(IS("toupper") ? toupper(a) : tolower(a)), 0);
    if (IS("strlen"))
        return result(g, strlen(str(a)), 0);
    if (IS("atoi"))
        return result(g, atoi(str(a)), 0);
    if (IS("strchr") || IS("strstr")) {
        const char *text = str(a);
        const char *position = IS("strchr") ? strchr(text, arg(g, 1)) : strstr(text, str(arg(g, 1)));
        return result(g, position ? a + (position - text) : 0, 0);
    }
    if (IS("qsort") && has_block(guest_qsort)) {
        g->cpu.eip = guest_qsort;
        return true;
    }
    if (IS("_mkdir")) {
        char path[1024];
        if (strcasecmp(str(a), "Data\\User") && strcasecmp(str(a), "Data/User"))
            return false;
        return result(g, asset_path(g, str(a), path) ? mkdir(path, 0777) : UINT32_MAX, 0);
    }
    if (IS("ceil")) {
        fpush(&g->cpu, ceil(rdf64(g->cpu.esp + 4)));
        return result(g, 0, 0);
    }
    if (IS("wcslen")) {
        uint32_t n = 0;
        while (n < 0x10000 && rd16(a + 2 * n))
            ++n;
        return n < 0x10000 && result(g, n, 0);
    }
    if (IS("wcstombs") || IS("mbstowcs")) {
        uint32_t source = arg(g, 1), limit = a ? arg(g, 2) : 0x10000;
        bool to_wide = IS("mbstowcs");
        for (uint32_t i = 0; i < limit; ++i) {
            uint16_t c = to_wide ? rd8(source + i) : rd16(source + 2 * i);
            if (c > 127)
                return result(g, UINT32_MAX, 0);
            if (a) {
                if (to_wide)
                    wr16(a + 2 * i, c);
                else
                    wr8(a + i, c);
            }
            if (!c)
                return result(g, i, 0);
        }
        return result(g, limit, 0);
    }
    if (IS("_makepath")) {
        const char *drive = arg(g, 1) ? str(arg(g, 1)) : "";
        const char *dir = arg(g, 2) ? str(arg(g, 2)) : "";
        const char *name_part = arg(g, 3) ? str(arg(g, 3)) : "";
        const char *ext = arg(g, 4) ? str(arg(g, 4)) : "";
        size_t drive_size = strlen(drive), dir_size = strlen(dir);
        const char *colon = drive_size && drive[drive_size - 1] != ':' ? ":" : "";
        const char *slash = dir_size && dir[dir_size - 1] != '/' && dir[dir_size - 1] != '\\' ? "\\" : "";
        const char *dot = *ext && *ext != '.' ? "." : "";
        char path[260];
        int n = snprintf(path, sizeof path, "%s%s%s%s%s%s%s", drive, colon, dir, slash, name_part, dot, ext);
        if (n < 0 || (size_t)n >= sizeof path)
            return false;
        memcpy(guest_pointer(a), path, (size_t)n + 1);
        return result(g, 0, 0);
    }
    if (IS("_stricmp"))
        return result(g, strcasecmp(str(a), str(arg(g, 1))), 0);
    if (IS("_splitpath")) {
        const char *path = str(a);
        size_t drive_size = path[0] && path[1] == ':' ? 2 : 0;
        const char *dir = path + drive_size, *leaf = dir;
        for (const char *p = dir; *p; ++p)
            if (*p == '/' || *p == '\\')
                leaf = p + 1;
        const char *end = leaf + strlen(leaf), *dot = strrchr(leaf, '.');
        if (!dot)
            dot = end;
        const char *starts[] = {path, dir, leaf, dot};
        const char *ends[] = {dir, leaf, dot, end};
        for (unsigned i = 0; i < 4; ++i) {
            uint32_t out = arg(g, i + 1);
            if (out) {
                size_t n = ends[i] - starts[i];
                memcpy(guest_pointer(out), starts[i], n);
                wr8(out + n, 0);
            }
        }
        return result(g, 0, 0);
    }
    if (IS("memcpy") || IS("memmove") || IS("strncpy")) {
        uint32_t n = arg(g, 2);
        void *target = game_bytes(g, a, n);
        const void *source = game_bytes(g, arg(g, 1), n);
        if (!target || !source)
            return false;
        if (IS("strncpy"))
            strncpy(target, source, n);
        else
            memmove(target, source, n);
        return result(g, a, 0);
    }
    if (IS("sprintf") || IS("vsprintf"))
        return format(g, a, arg(g, 1), IS("vsprintf") ? arg(g, 2) : g->cpu.esp + 12);
    return false;
    #undef IS
}
