#include "game.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static uint32_t word(const uint8_t *bytes)
{
    return bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

static uint8_t *read_script(FILE *archive, size_t *bytes)
{
    uint8_t header[12], entry[8];
    if (fseek(archive, 0, SEEK_END))
        return NULL;
    long length = ftell(archive);
    if (length < (long)sizeof header || fseek(archive, 0, SEEK_SET) ||
        fread(header, 1, sizeof header, archive) != sizeof header || word(header) != 0x1b356852)
        return NULL;
    uint32_t count = word(header + 8);
    long index = length - (long)count * (long)sizeof entry;
    if (!count || count > 65536 || index < (long)sizeof header || fseek(archive, index, SEEK_SET))
        return NULL;
    uint32_t offset = 0;
    for (unsigned i = 0; i < count; ++i) {
        if (fread(entry, 1, sizeof entry, archive) != sizeof entry)
            return NULL;
        if (word(entry) == 0x7a6686c4) {
            if (offset)
                return NULL;
            offset = word(entry + 4);
            if (offset < sizeof header || offset > index - 4)
                return NULL;
        }
    }
    if (!offset || fseek(archive, offset, SEEK_SET) || fread(entry, 1, 4, archive) != 4)
        return NULL;
    uint32_t size = word(entry);
    if (!size || size > 1024 * 1024 || size > index - offset - 4)
        return NULL;
    uint8_t *data = malloc(size);
    if (!data || fread(data, 1, size, archive) != size) {
        free(data);
        return NULL;
    }
    *bytes = size;
    return data;
}

FILE *open_level_script(Game *g, const char *name, bool text, void **memory)
{
    char candidate[1024], expected[1024];
    if (!asset_path(g, name, candidate) ||
        !asset_path(g, "Data/Locations/Mountain_2/Logic/LevelScript.lua", expected) ||
        strcasecmp(candidate, expected))
        return NULL;
    FILE *archive = open_asset(g, "Data2.bnd");
    if (!archive)
        return NULL;
    size_t bytes;
    uint8_t *data = read_script(archive, &bytes);
    fclose(archive);
    if (!data)
        return NULL;
    if (text) {
        size_t end = 0;
        for (size_t i = 0; i < bytes; ++i) {
            if (data[i] == '\r' && i + 1 < bytes && data[i + 1] == '\n')
                continue;
            data[end++] = data[i];
        }
        bytes = end;
    }
    FILE *script = fmemopen(data, bytes, "rb");
    if (!script) {
        free(data);
        return NULL;
    }
    *memory = data;
    fprintf(g->log, "Opened Frozen Creek Lua script from Data2.bnd (%zu bytes)\n", bytes);
    return script;
}
