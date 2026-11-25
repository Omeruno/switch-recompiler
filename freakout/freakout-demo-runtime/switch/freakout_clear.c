#include "freakout_clear.h"

enum {
    CLEAR_COLOR = 1,
    CLEAR_DEPTH = 2,
    CLEAR_STENCIL = 4,
    MAX_COLOR_TARGETS = 4,
};

static float channel(uint32_t argb, unsigned shift)
{
    return (float)((argb >> shift) & 0xFFu) / 255.0f;
}

bool freakout_clear_full_target(DkCmdBuf commands, uint32_t color_targets,
                                int32_t width, int32_t height, const FreakOutClear *clear)
{
    if (!commands || !clear || width <= 0 || height <= 0 || (clear->flags & ~7u))
        return false;
    if (clear->count != 0) {
        if (clear->count != 1 || !clear->rects)
            return false;
        const FreakOutRect *rect = clear->rects;
        if (rect->left != 0 || rect->top != 0 || rect->right != width || rect->bottom != height)
            return false;
    }

    bool depth = (clear->flags & CLEAR_DEPTH) != 0;
    bool stencil = (clear->flags & CLEAR_STENCIL) != 0;
    if ((depth && !(clear->depth >= 0.0f && clear->depth <= 1.0f)) ||
        (stencil && clear->stencil > 0xFFu))
        return false;
    if ((clear->flags & CLEAR_COLOR) && (!color_targets || (color_targets >> MAX_COLOR_TARGETS)))
        return false;

    if (clear->flags & CLEAR_COLOR) {
        for (unsigned target = 0; target < MAX_COLOR_TARGETS; target++) {
            if (color_targets & (1u << target))
                dkCmdBufClearColorFloat(commands, target, DkColorMask_RGBA,
                                        channel(clear->argb, 16), channel(clear->argb, 8),
                                        channel(clear->argb, 0), channel(clear->argb, 24));
        }
    }
    if (depth || stencil)
        dkCmdBufClearDepthStencil(commands, depth, clear->depth,
                                  stencil ? 0xFFu : 0u, (uint8_t)clear->stencil);
    return true;
}
