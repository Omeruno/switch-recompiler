#include "diagnostic.h"
#include <string.h>

static uint16_t get16(const uint8_t *pointer)
{
    return (uint16_t)(pointer[0] | ((uint16_t)pointer[1] << 8));
}

static uint32_t get32(const uint8_t *pointer)
{
    return (uint32_t)get16(pointer) | ((uint32_t)get16(pointer + 2) << 16);
}

static int within(size_t offset, size_t width, size_t total)
{
    return offset <= total && width <= total - offset;
}

int map_demo_image(uint8_t *memory, const uint8_t *file, size_t size)
{
    if (size < 0x40 || get16(file) != 0x5A4D)
        return 0;
    size_t pe = get32(file + 0x3C);
    if (!within(pe, 24, size) || get32(file + pe) != 0x4550 ||
        get16(file + pe + 4) != 0x14C)
        return 0;

    size_t count = get16(file + pe + 6);
    size_t optional_size = get16(file + pe + 20);
    size_t optional = pe + 24;
    size_t first_section = optional + optional_size;
    size_t capacity = GUEST_SIZE - FREAKOUT_IMAGE_BASE;
    uint8_t *image = memory + FREAKOUT_IMAGE_BASE;
    if (optional_size < 0x40 || !within(optional, optional_size, size) ||
        !within(first_section, count * 40, size) || get16(file + optional) != 0x10B ||
        get32(file + optional + 16) != FREAKOUT_ENTRY_RVA ||
        get32(file + optional + 28) != FREAKOUT_IMAGE_BASE)
        return 0;

    size_t image_size = get32(file + optional + 56);
    size_t headers = get32(file + optional + 60);
    if (image_size > capacity || headers < first_section + count * 40 ||
        headers > image_size || headers > size)
        return 0;

    memcpy(image, file, headers);
    for (size_t index = 0; index < count; index++) {
        const uint8_t *section = file + first_section + index * 40;
        size_t virtual_size = get32(section + 8);
        size_t rva = get32(section + 12);
        size_t raw_size = get32(section + 16);
        size_t offset = get32(section + 20);
        if (!within(rva, virtual_size, image_size) || !within(rva, raw_size, image_size) ||
            !within(offset, raw_size, size))
            return 0;
        if (raw_size)
            memcpy(image + rva, file + offset, raw_size);
    }
    return 1;
}
