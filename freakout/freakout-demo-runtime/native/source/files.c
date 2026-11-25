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

static bool next_search(Search *it, uint32_t address)
{
    if (it->next == it->count)
        return false;
    const char *name = it->names[it->next++];
    char path[1284];
    snprintf(path, sizeof path, "%s/%s", it->path, name);
    struct stat statbuf;
    if (stat(path, &statbuf))
        return false;
    memset(guest_pointer(address), 0, 320);
    wr32(address, S_ISDIR(statbuf.st_mode) ? 0x10 : 0x20);
    wr32(address + 28, (uint64_t)statbuf.st_size >> 32);
    wr32(address + 32, (uint32_t)statbuf.st_size);
    size_t count = strlen(name);
    if (count > 259)
        count = 259;
    memcpy(guest_pointer(address + 44), name, count);
    return true;
}

bool file_dispatch(Game *g, const Import *call)
{
    const char *name = call->name;
    if (!strcmp(name, "GetFileAttributesA")) {
        char path[1024];
        struct stat state;
        bool found = asset_path(g, (const char *)guest_pointer(arg(g, 0)), path) && !stat(path, &state);
        return result(g, found ? S_ISDIR(state.st_mode) ? 0x10 : 0x20 : UINT32_MAX, 4);
    }
    if (!strcmp(name, "FindFirstFileA")) {
        unsigned slot = 0;
        while (slot < MAX_SEARCHES && g->searches[slot])
            slot++;
        if (slot == MAX_SEARCHES)
            return result(g, UINT32_MAX, 8);
        Search *it = open_search(g, (const char *)guest_pointer(arg(g, 0)));
        if (!it || !next_search(it, arg(g, 1))) {
            close_search(it);
            return result(g, UINT32_MAX, 8);
        }
        g->searches[slot] = it;
        return result(g, 0x900 + slot, 8);
    }
    if (!strcmp(name, "FindNextFileA") || !strcmp(name, "FindClose")) {
        unsigned slot = arg(g, 0) - 0x900;
        bool close = !strcmp(name, "FindClose");
        if (slot >= MAX_SEARCHES || !g->searches[slot])
            return result(g, 0, close ? 4 : 8);
        if (!close)
            return result(g, next_search(g->searches[slot], arg(g, 1)), 8);
        close_search(g->searches[slot]);
        g->searches[slot] = NULL;
        return result(g, 1, 4);
    }
    return false;
}
