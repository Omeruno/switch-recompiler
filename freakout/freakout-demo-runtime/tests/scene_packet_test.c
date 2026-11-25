#include "scene_packet.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void rejected_mutation(FoScenePacket *parsed, uint8_t *data, size_t size,
                              size_t at, const char change[4])
{
    assert(at <= size - 4);
    uint8_t original[4];
    memcpy(original, data + at, sizeof original);
    memcpy(data + at, change, sizeof original);
    assert(!fo_scene_parse(parsed, data, size));
    memcpy(data + at, original, sizeof original);
    assert(fo_scene_parse(parsed, data, size));
}

int main(int argc, char **argv)
{
    assert(argc >= 2);
    for (int i = 1; i < argc; i++) {
        FILE *file = fopen(argv[i], "rb");
        assert(file && fseek(file, 0, SEEK_END) == 0);
        long length = ftell(file);
        assert(length >= FO_SCENE_HEADER_SIZE && length <= FO_SCENE_MAX_SIZE);
        rewind(file);
        size_t size = (size_t)length;
        uint8_t *data = malloc(size);
        FoScenePacket *packet = malloc(sizeof *packet);
        assert(data && packet && fread(data, 1, size, file) == size);
        fclose(file);

        assert(fo_scene_parse(packet, data, size));
        assert(!fo_scene_parse(packet, data, size - 1));
        assert(fo_scene_parse(packet, data, size));
        rejected_mutation(packet, data, size, packet->shaders[0].data - data, "oops");
        rejected_mutation(packet, data, size, packet->draws[0].indices - data, "\xff\xff\xff\xff");
        rejected_mutation(packet, data, size, packet->draws[0].vertices - data, "\x00\x00\x80\x7f");
        unsigned programmable = 0;
        for (uint32_t draw = 0; draw < packet->draw_count; draw++)
            if (packet->draws[draw].flags & FO_SCENE_PROGRAM)
                programmable++;
        assert(programmable > 0 && programmable < packet->draw_count);
        printf("%u draws (%u programmable), %u textures, %u shaders, %u clears\n",
               packet->draw_count, programmable, packet->texture_count, packet->shader_count, packet->clear_count);
        free(packet);
        free(data);
    }
    return 0;
}
