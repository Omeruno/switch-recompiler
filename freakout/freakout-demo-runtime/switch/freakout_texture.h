#ifndef FREAKOUT_TEXTURE_H
#define FREAKOUT_TEXTURE_H

#include <stdbool.h>
#include <stdint.h>
#include <deko3d.h>

typedef struct {
    DkImageFormat format;
    uint32_t mip_width, mip_height;
    uint32_t row_bytes, block_rows;
    uint64_t size;
    bool premultiplied_alpha;
} FreakOutTextureLayout;

bool freakout_dxt_layout(uint32_t fourcc, uint32_t width, uint32_t height,
                         uint32_t mip, FreakOutTextureLayout *layout);

#endif
