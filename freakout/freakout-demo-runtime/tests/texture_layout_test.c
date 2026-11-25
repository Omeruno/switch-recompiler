#include "freakout_texture.h"
#include <assert.h>

int main(void)
{
    FreakOutTextureLayout layout;
    for (uint32_t level = 0; level < 3; level++) {
        assert(freakout_dxt_layout(0x31545844, 4, 4, level, &layout));
        assert(layout.format == DkImageFormat_RGBA_BC1);
        assert(layout.row_bytes == 8 && layout.size == 8);
        assert(layout.mip_width == (4u >> level) && layout.mip_height == layout.mip_width);
    }
    assert(!freakout_dxt_layout(0x31545844, 4, 4, 3, &layout));
    assert(freakout_dxt_layout(0x33545844, 13, 7, 1, &layout));
    assert(layout.format == DkImageFormat_RGBA_BC2 && layout.size == 32);
    assert(freakout_dxt_layout(0x34545844, 7, 13, 0, &layout));
    assert(layout.format == DkImageFormat_RGBA_BC3 && layout.premultiplied_alpha);
    assert(layout.row_bytes == 32 && layout.block_rows == 4 && layout.size == 128);
    assert(!freakout_dxt_layout(0x34545844, UINT32_MAX, 1, 0, &layout));
}
