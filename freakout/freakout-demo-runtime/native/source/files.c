#include "game.h"
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

typedef struct Search {
    char path[1024];
    char **names;
    size_t count, next;
} Search;

static void close_search(Search *it)
{
    if (!it)
        return;
    for (size_t i = 0; i < it->count; ++i)
        free(it->names[i]);
    free(it->names);
    free(it);
}

void file_close_searches(Game *g)
{
    for (unsigned i = 0; i < MAX_SEARCHES; ++i)
        close_search(g->searches[i]);
}

static int compare(const void *left, const void *right)
{
    return strcmp(*(char *const *)left, *(char *const *)right);
}

static Search *open_search(Game *g, const char *pattern)
{
    char path[1024];
    if (!asset_path(g, pattern, path))
        return NULL;
    char *filter = strrchr(path, '/');
    if (!filter || strcmp(filter, "/*"))
        return NULL;
    *filter = 0;
    DIR *dir = opendir(path);
    if (!dir)
        return NULL;

    Search *it = calloc(1, sizeof *it);
    size_t capacity = 0;
    bool ok = it != NULL;
    if (ok)
        memcpy(it->path, path, strlen(path) + 1);
    struct dirent *entry;
    while (ok && (entry = readdir(dir))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;
        if (it->count == capacity) {
            capacity = capacity ? capacity * 2 : 32;
            if (capacity > 4096) {
                ok = false;
                break;
            }
            char **next = realloc(it->names, capacity * sizeof *next);
            if (!next) {
                ok = false;
                break;
            }
            it->names = next;
        }
        char *copy = strdup(entry->d_name);
        if (!copy) {
            ok = false;
            break;
        }
        it->names[it->count++] = copy;
    }
    closedir(dir);
    if (!ok || !it->count) {
        close_search(it);
        return NULL;
    }
    qsort(it->names, it->count, sizeof(char *), compare);
    return it;
}

static bool next_search(Game *g, Search *it, uint32_t address, bool crt)
{
    if (it->next == it->count)
        return false;
    unsigned bytes = crt ? 280 : 320;
    if (!address || !game_bytes(g, address, bytes))
        return false;
    const char *name = it->names[it->next++];
    char path[1284];
    snprintf(path, sizeof path, "%s/%s", it->path, name);
    struct stat statbuf;
    if (stat(path, &statbuf))
        return false;
    memset(guest_pointer(address), 0, bytes);
    wr32(address, S_ISDIR(statbuf.st_mode) ? 0x10 : 0x20);
    if (crt) {
        wr32(address + 4, statbuf.st_ctime);
        wr32(address + 8, statbuf.st_atime);
        wr32(address + 12, statbuf.st_mtime);
        wr32(address + 16, statbuf.st_size);
    } else {
        wr32(address + 28, (uint64_t)statbuf.st_size >> 32);
        wr32(address + 32, (uint32_t)statbuf.st_size);
    }
    size_t count = strlen(name);
    if (count > 259)
        count = 259;
    memcpy(guest_pointer(address + (crt ? 20 : 44)), name, count);
    return true;
}

bool file_dispatch(Game *g, const Import *call)
{
    const char *name = call->name;
    bool crt = !strcasecmp(call->dll, "msvcr71.dll");
    if (!strcmp(name, "GetFileAttributesA")) {
        char path[1024];
        struct stat state;
        bool found = asset_path(g, (const char *)guest_pointer(arg(g, 0)), path) && !stat(path, &state);
        return result(g, found ? S_ISDIR(state.st_mode) ? 0x10 : 0x20 : UINT32_MAX, 4);
    }
    if (!strcmp(name, crt ? "_findfirst" : "FindFirstFileA")) {
        unsigned slot = 0;
        while (slot < MAX_SEARCHES && g->searches[slot])
            slot++;
        if (slot == MAX_SEARCHES)
            return result(g, UINT32_MAX, crt ? 0 : 8);
        Search *it = open_search(g, (const char *)guest_pointer(arg(g, 0)));
        if (!it || !next_search(g, it, arg(g, 1), crt)) {
            close_search(it);
            if (crt) {
                uint32_t thread = thread_crt(g);
                if (thread)
                    wr32(thread + 8, 2);
            }
            return result(g, UINT32_MAX, crt ? 0 : 8);
        }
        g->searches[slot] = it;
        return result(g, 0x900 + slot, crt ? 0 : 8);
    }
    if (!strcmp(name, crt ? "_findnext" : "FindNextFileA") || !strcmp(name, crt ? "_findclose" : "FindClose")) {
        unsigned slot = arg(g, 0) - 0x900;
        bool close = !strcmp(name, crt ? "_findclose" : "FindClose");
        if (slot >= MAX_SEARCHES || !g->searches[slot])
            return result(g, crt ? UINT32_MAX : 0, crt ? 0 : close ? 4 : 8);
        if (!close) {
            bool found = next_search(g, g->searches[slot], arg(g, 1), crt);
            return result(g, crt ? found ? 0 : UINT32_MAX : found, crt ? 0 : 8);
        }
        close_search(g->searches[slot]);
        g->searches[slot] = NULL;
        return result(g, crt ? 0 : 1, crt ? 0 : 4);
    }
    return false;
}
