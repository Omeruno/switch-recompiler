#ifndef FREAKOUT_CLEAR_H
#define FREAKOUT_CLEAR_H

#include <stdbool.h>
#include <stdint.h>
#include <deko3d.h>

typedef struct {
    int32_t left, top, right, bottom;
} FreakOutRect;

typedef struct {
    unsigned count;
    const FreakOutRect *rects;
    uint32_t flags;
    uint32_t argb;
    float depth;
    uint32_t stencil;
} FreakOutClear;

/* Color and depth/stencil images must already be bound by the caller. */
bool freakout_clear_full_target(DkCmdBuf commands, uint32_t color_targets,
                                int32_t width, int32_t height, const FreakOutClear *clear);

#endif
