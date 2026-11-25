#include "frame_packet.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *read_packet(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb");
    assert(file && fseek(file, 0, SEEK_END) == 0);
    long length = ftell(file);
    assert(length > 0 && length < FO_FRAME_MAX_SIZE);
    rewind(file);
    uint8_t *bytes = malloc((size_t)length);
    assert(bytes && fread(bytes, 1, (size_t)length, file) == (size_t)length);
    fclose(file);
    *size = (size_t)length;
    return bytes;
}

int main(int argc, char **argv)
{
    assert(argc == 2 || (argc == 3 && strcmp(argv[2], "--menu") == 0));
    bool menu = argc == 3;
    size_t size;
    uint8_t *bytes = read_packet(argv[1], &size);
    FoFramePacket packet;
    assert(fo_frame_parse(&packet, bytes, size));
    assert(packet.texture_count == 5 && packet.draw_count == (menu ? 19 : 6));
    uint32_t triangles = 0;
    for (uint32_t i = 0; i < packet.draw_count; i++) {
        FoFrameDraw *draw = &packet.draws[i];
        triangles += draw->primitive == 4 ? draw->index_count / 3 : draw->index_count - 2;
    }
    assert(triangles == (menu ? 942 : 153));
    const FoFrameTexture *background = &packet.textures[packet.draws[menu ? 0 : 1].texture];
    assert(background->width == 1024 && background->height == 512 && background->level_count == 11);
    assert(fo_frame_mip_size(background->format, 1, 1) == 16);
    assert(!fo_frame_parse(&packet, bytes, size - 1));

    size_t index = (size_t)(packet.draws[0].indices - bytes);
    uint8_t original_index = bytes[index];
    bytes[index] = 255;
    assert(!fo_frame_parse(&packet, bytes, size));
    bytes[index] = original_index;

    size_t vertex = (size_t)(packet.draws[0].vertices - bytes);
    uint8_t coordinate[4];
    memcpy(coordinate, bytes + vertex, sizeof coordinate);
    memcpy(bytes + vertex, "\x00\x00\x80\x7f", sizeof coordinate);
    assert(!fo_frame_parse(&packet, bytes, size));
    memcpy(bytes + vertex, coordinate, sizeof coordinate);

    size_t width_byte = FO_FRAME_HEADER_SIZE + (bytes[4] >= 2 ? 8 : 0) + 3;
    uint8_t original_width = bytes[width_byte];
    bytes[width_byte] = 255;
    assert(!fo_frame_parse(&packet, bytes, size));
    bytes[width_byte] = original_width;
    assert(fo_frame_parse(&packet, bytes, size));
    free(bytes);
    puts("Frame packet: geometry and invalid-packet checks passed.");
    return 0;
}
