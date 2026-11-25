#include "freakout_texture.h"

#define DXT(c) (0x00545844u | ((uint32_t)(c) << 24))

bool freakout_dxt_layout(uint32_t fourcc, uint32_t width, uint32_t height,
                         uint32_t mip, FreakOutTextureLayout *layout)
{
    if (!layout || !width || !height || mip >= 32)
        return false;

    uint32_t bytes_per_block = 16;
    DkImageFormat format;
    switch (fourcc) {
    case DXT('1'):
        bytes_per_block = 8;
        format = DkImageFormat_RGBA_BC1;
        break;
    case DXT('2'):
    case DXT('3'):
        format = DkImageFormat_RGBA_BC2;
        break;
    case DXT('4'):
    case DXT('5'):
        format = DkImageFormat_RGBA_BC3;
        break;
    default:
        return false;
    }

    uint32_t w = width >> mip;
    uint32_t h = height >> mip;
    if (!w && !h)
        return false;
    if (!w)
        w = 1;
    if (!h)
        h = 1;

    uint64_t row = (((uint64_t)w + 3) / 4) * bytes_per_block;
    uint32_t rows = ((uint64_t)h + 3) / 4;
    if (row > UINT32_MAX)
        return false;
    *layout = (FreakOutTextureLayout){
        .format = format,
        .mip_width = w, .mip_height = h,
        .row_bytes = (uint32_t)row, .block_rows = rows, .size = row * rows,
        .premultiplied_alpha = fourcc == DXT('2') || fourcc == DXT('4'),
    };
    return true;
}
