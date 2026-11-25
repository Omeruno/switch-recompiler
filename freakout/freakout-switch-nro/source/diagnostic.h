#ifndef FREAKOUT_DIAGNOSTIC_H
#define FREAKOUT_DIAGNOSTIC_H

#include <stddef.h>
#include <stdint.h>

enum {
    GUEST_SIZE = 0x800000,
    FREAKOUT_IMAGE_BASE = 0x400000,
    FREAKOUT_ENTRY_RVA = 0x1AF59E,
    STARTUP_RESULT = 0x800,
};

int map_demo_image(uint8_t *memory, const uint8_t *file, size_t size);
void check_startup(uint8_t *memory);
uint64_t check_leaf(uint8_t *memory, uint32_t a, uint32_t b);

#endif
