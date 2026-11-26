#include "game.h"
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <math.h>
#include <sys/stat.h>
#include <time.h>
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
    const char *filename = str(name), *method = str(mode);
    bool writing = !strcmp(method, "wb") || !strcmp(method, "w");
    if (!writing && strcmp(method, "rb") && strcmp(method, "r"))
        return result(g, 0, 0);
    char path[1024], temp[1056];
    FILE *file = NULL;
    if (writing) {
        const char *eligible = !strcasecmp(filename, "language.cfg") ? "language.cfg" :
            (!strcasecmp(filename, "Data\\User\\profile.pro") ||
             !strcasecmp(filename, "Data/User/profile.pro")) ? "Data/User/profile.pro" : NULL;
        if (!eligible || !asset_path(g, eligible, path))
            return result(g, 0, 0);
        snprintf(temp, sizeof temp, "%s.tmp.XXXXXX", path);
        int descriptor = mkstemp(temp);
        if (descriptor < 0)
            return result(g, 0, 0);
        file = fdopen(descriptor, "wb");
        if (!file) {
            close(descriptor);
            unlink(temp);
        }
    } else {
        file = open_asset(g, filename);
    }
    if (!file)
        return result(g, 0, 0);
    for (unsigned i = 0; i < MAX_FILES; ++i) {
        if (!g->files[i]) {
            if (writing) {
                char *pending = strdup(temp), *final = strdup(path);
                if (!pending || !final) {
                    free(pending);
                    free(final);
                    break;
                }
                g->pending_save[i] = pending;
                g->save_path[i] = final;
            }
            uint32_t handle = BSS + 0x1000 + 32 * i;
            memset(guest_pointer(handle), 0, 32);
            wr32(handle + 12, writing ? 2 : 1);
            g->files[i] = file;
            g->archive_file[i] = !strcasecmp(str(name), "Data2.bnd");
            if (g->archive_file[i])
                fprintf(g->log, "Opened game archive Data2.bnd\n");
            return result(g, handle, 0);
        }
    }
    fclose(file);
    if (writing)
        unlink(temp);
    return result(g, 0, 0);
}

static bool file_call(Game *g, const char *name, uint32_t arguments, uint32_t handle)
{
    uint32_t slot = handle - (BSS + 0x1000);
    if (slot % 32 || slot / 32 >= MAX_FILES || !g->files[slot / 32])
        return false;
    slot /= 32;
    FILE *file = g->files[slot];
    const char *pending = g->pending_save[slot];
    if (!strcmp(name, "fclose")) {
        g->files[slot] = NULL;
        bool saved = !ferror(file);
        if (pending && (fflush(file) || fsync(fileno(file))))
            saved = false;
        if (fclose(file))
            saved = false;
        if (pending) {
            if (saved && rename(pending, g->save_path[slot]))
                saved = false;
            if (!saved)
                unlink(pending);
            fprintf(g->log, "%s %s\n", saved ? "Saved" : "Could not save", g->save_path[slot]);
            free(g->pending_save[slot]);
            free(g->save_path[slot]);
            g->pending_save[slot] = g->save_path[slot] = NULL;
        }
        return result(g, saved ? 0 : UINT32_MAX, 0);
    }
    if (!strcmp(name, "ftell"))
        return result(g, ftell(file), 0);
    if (!strcmp(name, "getc") || !strcmp(name, "fgetc")) {
        int value = fgetc(file);
        if (value == EOF) {
            uint32_t flags = (feof(file) ? 0x10 : 0) | (ferror(file) ? 0x20 : 0);
            wr32(handle + 12, rd32(handle + 12) | flags);
        } else {
            ++g->file_bytes;
        }
        return result(g, value == EOF ? UINT32_MAX : (uint32_t)value, 0);
    }
    if (!strcmp(name, "ungetc")) {
        int value = ungetc((int32_t)arguments, file);
        if (value != EOF)
            wr32(handle + 12, rd32(handle + 12) & ~0x10u);
        return result(g, value == EOF ? UINT32_MAX : (uint32_t)value, 0);
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
    if (!strcmp(name, "fwrite")) {
        if (!pending)
            return result(g, 0, 0);
        size_t written = fwrite(output, size, count, file);
        if (written != count)
            wr32(handle + 12, rd32(handle + 12) | 0x20);
        return result(g, written, 0);
    }
    long offset = g->archive_file[slot] ? ftell(file) : -1;
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
        size_t length = p - start;
        if (*p == 'l' && p[1] && strchr("diouxX", p[1]))
            ++p; // MSVCR71 long is 32 bits.
        if (!*p || length + 2 > sizeof spec)
            return false;
        const char conversion = *p++;
        memcpy(spec, start, length);
        spec[length++] = conversion;
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
            fprintf(g->log, "Unsupported CRT format: %s\n", str(pattern));
            return false;
        }
        if (conversion != '%')
            arguments += 4;
        if (written < 0 || (size_t)written >= available)
            return false;
        offset += written;
    }
    data[offset] = 0;
    if (!output) {
        fputs(data, g->log);
        return result(g, offset, 0);
    }
    void *destination = game_bytes(g, output, offset + 1);
    if (!destination)
        return false;
    memcpy(destination, data, offset + 1);
    return result(g, offset, 0);
}

static bool scan(Game *g, const char *input, const char *pattern)
{
    unsigned assigned = 0;
    while (*pattern) {
        if (isspace((unsigned char)*pattern)) {
            while (isspace((unsigned char)*input))
                ++input;
            ++pattern;
            continue;
        }
        if (*pattern != '%' || pattern[1] == '%') {
            if (*pattern == '%')
                ++pattern;
            if (!*input)
                return result(g, assigned ? assigned : UINT32_MAX, 0);
            if (*input++ != *pattern++)
                break;
            continue;
        }
        ++pattern;
        unsigned width = 0;
        while (*pattern >= '0' && *pattern <= '9') {
            width = width * 10 + (unsigned)(*pattern++ - '0');
            if (width > 65536)
                return false;
        }
        bool wide = *pattern == 'l';
        if (wide)
            ++pattern;
        char conversion = *pattern++;
        if (!conversion || !strchr("csfdiux", conversion) || (wide && conversion != 'f'))
            return false;
        if (conversion != 'c')
            while (isspace((unsigned char)*input))
                ++input;
        if (!*input)
            return result(g, assigned ? assigned : UINT32_MAX, 0);

        uint32_t destination = arg(g, 2 + assigned);
        if (conversion == 's' || conversion == 'c') {
            size_t count = 0;
            size_t limit = width ? width : conversion == 'c' ? 1 : 65536;
            while (count < limit && input[count] && (conversion == 'c' || !isspace((unsigned char)input[count])))
                ++count;
            if (conversion == 'c' && count != limit)
                return result(g, assigned ? assigned : UINT32_MAX, 0);
            char *output = destination ? (char *)game_bytes(g, destination, count + (conversion == 's')) : NULL;
            if (!output)
                return false;
            memcpy(output, input, count);
            if (conversion == 's')
                output[count] = 0;
            input += count;
        } else {
            char spec[16];
            if (width)
                snprintf(spec, sizeof spec, "%%%u%s%c%%n", width, wide ? "l" : "", conversion);
            else
                snprintf(spec, sizeof spec, "%%%s%c%%n", wide ? "l" : "", conversion);
            union { unsigned u; int i; float f; double d; } value;
            int consumed = 0, matches;
            if (wide) matches = sscanf(input, spec, &value.d, &consumed);
            else if (conversion == 'f') matches = sscanf(input, spec, &value.f, &consumed);
            else if (conversion == 'd' || conversion == 'i') matches = sscanf(input, spec, &value.i, &consumed);
            else matches = sscanf(input, spec, &value.u, &consumed);
            if (matches != 1 || consumed <= 0)
                break;
            size_t bytes = wide ? 8 : 4;
            void *output = destination ? game_bytes(g, destination, bytes) : NULL;
            if (!output)
                return false;
            memcpy(output, &value, bytes);
            input += consumed;
        }
        ++assigned;
    }
    return result(g, assigned, 0);
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
    if (IS("malloc") || IS("??2@YAPAXI@Z")) {
        uint32_t pointer = allocate(g, a);
        if (!pointer)
            fprintf(g->log, "Guest allocation failed: %u bytes\n", a);
        return result(g, pointer, 0);
    }
    if (IS("free") || IS("??3@YAXPAX@Z") || IS("??_V@YAXPAX@Z"))
        return release(g, a) && result(g, 0, 0);
    if (IS("realloc"))
        return result(g, reallocate(g, a, arg(g, 1)), 0);
    if (IS("fopen"))
        return open_file(g, a, arg(g, 1));
    if (IS("sscanf"))
        return scan(g, str(a), str(arg(g, 1)));
    if (IS("fread") || IS("fwrite") || IS("fseek") || IS("ftell") || IS("fclose"))
        return file_call(g, name, a, IS("fread") || IS("fwrite") ? arg(g, 3) : a);
    if (IS("getc") || IS("fgetc") || IS("ungetc"))
        return file_call(g, name, a, IS("ungetc") ? arg(g, 1) : a);
    if (IS("time")) {
        time_t now = time(NULL);
        uint32_t value = now < 0 || now > INT32_MAX ? UINT32_MAX : (uint32_t)now;
        if (a) {
            if (!game_bytes(g, a, 4))
                return false;
            wr32(a, value);
        }
        return result(g, value, 0);
    }
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
    if (IS("isalnum") || IS("isalpha") || IS("iscntrl") || IS("isdigit") || IS("isprint") || IS("isspace")) {
        if (a > 255)
            return result(g, 0, 0);
        bool matched;
        if (IS("isalnum")) matched = isalnum(a);
        else if (IS("isalpha")) matched = isalpha(a);
        else if (IS("iscntrl")) matched = iscntrl(a);
        else if (IS("isdigit")) matched = isdigit(a);
        else if (IS("isprint")) matched = isprint(a);
        else matched = isspace(a);
        return result(g, matched, 0);
    }
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
    if (IS("_setjmp3") && has_block(guest_setjmp3)) {
        g->cpu.eip = guest_setjmp3;
        return true;
    }
    if (IS("_mkdir")) {
        char path[1024];
        if (strcasecmp(str(a), "Data\\User") && strcasecmp(str(a), "Data/User"))
            return false;
        return result(g, asset_path(g, str(a), path) ? (uint32_t)mkdir(path, 0777) : UINT32_MAX, 0);
    }
    if (IS("_errno") || IS("__doserrno")) {
        uint32_t data = thread_crt(g);
        return data && result(g, data + (IS("_errno") ? 8 : 12), 0);
    }
    if (IS("__uncaught_exception")) {
        uint32_t data = thread_crt(g);
        return data && result(g, rd32(data + 0x84) != 0, 0);
    }
    if (IS("localeconv")) {
        uint32_t table = BSS + 0x800, decimal = table + 64;
        memset(guest_pointer(table), 127, 48);
        memcpy(guest_pointer(decimal), ".", 2);
        wr32(table, decimal);
        for (unsigned i = 1; i < 10; ++i)
            wr32(table + i * 4, decimal + 1);
        return result(g, table, 0);
    }
    if (IS("atof") || IS("strtod")) {
        char *end;
        errno = 0;
        fpush(&g->cpu, strtod(str(a), &end));
        if (IS("strtod") && arg(g, 1))
            wr32(arg(g, 1), a + (end - str(a)));
        if (errno == ERANGE) {
            uint32_t data = thread_crt(g);
            if (data)
                wr32(data + 8, 34);
        }
        return result(g, 0, 0);
    }
    if (IS("strtol") || IS("strtoul")) {
        char *end = (char *)str(a);
        uint32_t base = arg(g, 2);
        if (base == 1 || base > 36)
            return result(g, 0, 0);
        errno = 0;
        long long value = strtoll(str(a), &end, base);
        int64_t lower = IS("strtoul") ? -(int64_t)UINT32_MAX : INT32_MIN;
        int64_t upper = IS("strtoul") ? UINT32_MAX : INT32_MAX;
        if (value < lower || value > upper || errno == ERANGE) {
            uint32_t data = thread_crt(g);
            if (data)
                wr32(data + 8, 34);
            value = IS("strtoul") ? upper : value < lower ? lower : upper;
        }
        if (arg(g, 1))
            wr32(arg(g, 1), a + (end - str(a)));
        return result(g, (uint32_t)value, 0);
    }
    if (IS("memset")) {
        void *target = game_bytes(g, a, arg(g, 2));
        if (!target)
            return false;
        memset(target, arg(g, 1), arg(g, 2));
        return result(g, a, 0);
    }
    if (IS("_CIfmod")) {
        double divisor = fpop(&g->cpu);
        double dividend = fpop(&g->cpu);
        fpush(&g->cpu, fmod(dividend, divisor));
        return result(g, 0, 0);
    }
    if (IS("_CIasin") || IS("_CIacos")) {
        double value = fpop(&g->cpu);
        fpush(&g->cpu, IS("_CIasin") ? asin(value) : acos(value));
        return result(g, 0, 0);
    }
    if (IS("memchr")) {
        uint32_t n = arg(g, 2);
        const unsigned char *source = game_bytes(g, a, n);
        if (!source)
            return false;
        const unsigned char *found = memchr(source, arg(g, 1), n);
        return result(g, found ? a + (found - source) : 0, 0);
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
    if (IS("wcsncpy") || IS("wcscpy")) {
        bool bounded = IS("wcsncpy");
        uint32_t source = arg(g, 1), count = bounded ? arg(g, 2) : 0x10000;
        if (count > LOW_SIZE / 2 || (bounded && !game_bytes(g, a, count * 2)))
            return false;
        bool ended = false;
        for (uint32_t i = 0; i < count; ++i) {
            if (!ended && (!source || !game_bytes(g, source, (i + 1) * 2)))
                return false;
            uint16_t value = ended ? 0 : rd16(source + i * 2);
            ended |= value == 0;
            if (!bounded && !game_bytes(g, a, (i + 1) * 2))
                return false;
            wr16(a + i * 2, value);
            if (ended && !bounded)
                return result(g, a, 0);
        }
        return bounded && result(g, a, 0);
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
    if (IS("strncmp"))
        return result(g, strncmp(str(a), str(arg(g, 1)), arg(g, 2)), 0);
    if (IS("strncat")) {
        const char *text = str(arg(g, 1));
        size_t count = strnlen(text, arg(g, 2)), offset = strlen(str(a));
        void *destination = game_bytes(g, a, offset + count + 1);
        if (!destination)
            return false;
        memcpy((char *)destination + offset, text, count);
        ((char *)destination)[offset + count] = 0;
        return result(g, a, 0);
    }
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
    if (IS("printf"))
        return format(g, 0, a, g->cpu.esp + 8);
    if (IS("sprintf") || IS("vsprintf"))
        return format(g, a, arg(g, 1), IS("vsprintf") ? arg(g, 2) : g->cpu.esp + 12);
    return false;
    #undef IS
}
