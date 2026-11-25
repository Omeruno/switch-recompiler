#include "freakout_clear.h"
#include <assert.h>
#include <math.h>
#include <string.h>

static DkCmdBuf expected;
static unsigned color_calls, depth_calls, target_index;
static float rgba[4], clear_depth;
static bool use_depth;
static uint8_t stencil_mask, stencil_value;

void dkCmdBufClearColor(DkCmdBuf buf, uint32_t target, uint32_t mask, const void *color)
{
    assert(buf == expected && mask == DkColorMask_RGBA);
    color_calls++;
    target_index = target;
    memcpy(rgba, color, sizeof rgba);
}

void dkCmdBufClearDepthStencil(DkCmdBuf buf, bool depth, float value,
                               uint8_t mask, uint8_t stencil)
{
    assert(buf == expected);
    depth_calls++;
    use_depth = depth;
    clear_depth = value;
    stencil_mask = mask;
    stencil_value = stencil;
}

int main(void)
{
    int opaque;
    expected = (DkCmdBuf)&opaque;
    FreakOutRect full_target = { 0, 0, 1024, 768 };
    FreakOutClear first_clear = { 1, &full_target, 3, 0xFF000000u, 1.0f, 0 };
    assert(freakout_clear_full_target(expected, 1, 1024, 768, &first_clear));
    assert(color_calls == 1 && depth_calls == 1 && target_index == 0);
    assert(rgba[0] == 0 && rgba[1] == 0 && rgba[2] == 0 && rgba[3] == 1);
    assert(use_depth && clear_depth == 1 && stencil_mask == 0);

    FreakOutClear masked = { 0, NULL, 5, 0x80402010u, 0.0f, 31 };
    assert(freakout_clear_full_target(expected, 4, 1024, 768, &masked));
    assert(color_calls == 2 && depth_calls == 2 && target_index == 2);
    assert(fabsf(rgba[0] - 64.0f/255) < 0.00001f && fabsf(rgba[1] - 32.0f/255) < 0.00001f);
    assert(fabsf(rgba[2] - 16.0f/255) < 0.00001f && fabsf(rgba[3] - 128.0f/255) < 0.00001f);
    assert(!use_depth && stencil_mask == 255 && stencil_value == 31);

    full_target.bottom = 767;
    assert(!freakout_clear_full_target(expected, 1, 1024, 768, &first_clear));
    assert(color_calls == 2 && depth_calls == 2);
}
