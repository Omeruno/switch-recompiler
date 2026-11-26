#include "game.h"
#include <stdlib.h>
#include <string.h>

enum { DXT1 = 0x31545844, DXT3 = 0x33545844, DXT5 = 0x35545844 };

static unsigned channel(uint32_t color, unsigned shift)
{
    return color >> shift & 255;
}

static unsigned distance(uint32_t a, uint32_t b)
{
    int r = (int)channel(a, 0) - (int)channel(b, 0);
    int g = (int)channel(a, 8) - (int)channel(b, 8);
    int blue = (int)channel(a, 16) - (int)channel(b, 16);
    return 2 * r * r + 4 * g * g + 3 * blue * blue;
}

static unsigned rgb565(uint32_t color)
{
    return (channel(color, 0) >> 3) << 11 | (channel(color, 8) >> 2) << 5 | channel(color, 16) >> 3;
}

static uint32_t expand565(unsigned color)
{
    unsigned r = color >> 11, g = color >> 5 & 63, b = color & 31;
    return ((r << 3) | (r >> 2)) | (((g << 2) | (g >> 4)) << 8) | (((b << 3) | (b >> 2)) << 16);
}

static uint32_t interpolate_rgb(uint32_t a, uint32_t b, unsigned wa, unsigned wb)
{
    uint32_t out = 0;
    for (unsigned shift = 0; shift < 24; shift += 8)
        out |= ((channel(a, shift) * wa + channel(b, shift) * wb) / (wa + wb)) << shift;
    return out;
}

static void encode_color(uint8_t *output, const uint32_t pixels[16], bool dxt1)
{
    unsigned best = 0, a = 0, b = 0;
    bool transparent = false, opaque = false;
    for (unsigned i = 0; i < 16; ++i) {
        if (dxt1 && pixels[i] >> 24 < 128) {
            transparent = true;
            continue;
        }
        if (!opaque) {
            a = b = i;
            opaque = true;
        }
        for (unsigned k = 0; k < i; ++k) {
            if (dxt1 && pixels[k] >> 24 < 128)
                continue;
            unsigned separation = distance(pixels[i], pixels[k]);
            if (separation > best) {
                best = separation;
                a = i;
                b = k;
            }
        }
    }
    unsigned first = rgb565(pixels[a]), second = rgb565(pixels[b]);
    if ((transparent && first > second) || (!transparent && first < second)) {
        unsigned temp = first;
        first = second;
        second = temp;
    }
    output[0] = first;
    output[1] = first >> 8;
    output[2] = second;
    output[3] = second >> 8;
    bool four_colors = !dxt1 || first > second;
    uint32_t palette[] = {expand565(first), expand565(second), 0, 0};
    palette[2] = interpolate_rgb(palette[0], palette[1], four_colors ? 2 : 1, 1);
    if (four_colors)
        palette[3] = interpolate_rgb(palette[0], palette[1], 1, 2);
    uint32_t selectors = 0;
    for (unsigned i = 0; i < 16; ++i) {
        unsigned selected = 0, error = UINT32_MAX;
        if (transparent && pixels[i] >> 24 < 128) {
            selected = 3;
        } else {
            for (unsigned k = 0; k < (four_colors ? 4u : 3u); ++k) {
                unsigned candidate = distance(pixels[i], palette[k]);
                if (candidate < error) {
                    error = candidate;
                    selected = k;
                }
            }
        }
        selectors |= selected << (2 * i);
    }
    for (unsigned i = 0; i < 4; ++i)
        output[4 + i] = selectors >> (8 * i);
}

static void encode_alpha(uint8_t *output, const uint32_t pixels[16], unsigned format)
{
    if (format == DXT3) {
        for (unsigned i = 0; i < 8; ++i)
            output[i] = (pixels[i * 2] >> 28) | ((pixels[i * 2 + 1] >> 28) << 4);
        return;
    }
    int high = 0, low = 255;
    for (unsigned i = 0; i < 16; ++i) {
        int alpha = pixels[i] >> 24;
        if (alpha > high) high = alpha;
        if (alpha < low) low = alpha;
    }
    output[0] = high;
    output[1] = low;
    int palette[8] = {high, low};
    for (int i = 1; i <= 6; ++i)
        palette[1 + i] = ((7 - i) * high + i * low) / 7;
    uint64_t indices = 0;
    for (unsigned i = 0; i < 16; ++i) {
        unsigned selected = 0;
        int alpha = pixels[i] >> 24, error = 256;
        for (unsigned k = 0; k < 8; ++k) {
            int candidate = abs(alpha - palette[k]);
            if (candidate < error) {
                error = candidate;
                selected = k;
            }
        }
        indices |= (uint64_t)selected << (3 * i);
    }
    for (unsigned i = 0; i < 6; ++i)
        output[2 + i] = indices >> (8 * i);
}

static void encode_level(uint8_t *output, const Surface *dest, const uint32_t *colors)
{
    unsigned width = dest->width, height = dest->height;
    if (dest->format >= 20 && dest->format <= 22) {
        unsigned bytes = dest->format == 20 ? 3 : 4, pitch = surface_pitch(dest);
        for (unsigned y = 0; y < height; ++y) {
            uint8_t *pixel = output + y * pitch;
            for (unsigned x = 0; x < width; ++x, pixel += bytes) {
                uint32_t color = colors[y * width + x];
                pixel[0] = color >> 16;
                pixel[1] = color >> 8;
                pixel[2] = color;
                if (bytes == 4)
                    pixel[3] = dest->format == 21 ? color >> 24 : 255;
            }
        }
        return;
    }
    for (unsigned y = 0; y < height; y += 4) {
        for (unsigned x = 0; x < width; x += 4) {
            uint32_t block[16];
            for (unsigned v = 0; v < 4; ++v) {
                unsigned row = (y + v < height ? y + v : height - 1) * width;
                for (unsigned u = 0; u < 4; ++u)
                    block[v * 4 + u] = colors[row + (x + u < width ? x + u : width - 1)];
            }
            bool dxt1 = dest->format == DXT1;
            if (!dxt1)
                encode_alpha(output, block, dest->format);
            encode_color(output + (dxt1 ? 0 : 8), block, dxt1);
            output += dxt1 ? 8 : 16;
        }
    }
}

static bool supported(unsigned format)
{
    return (format >= 20 && format <= 22) || format == DXT1 || format == DXT3 || format == DXT5;
}

bool d3dx_load_surface(Game *g)
{
    Surface *dest = d3d_surface_lookup(g, arg(g, 0), NULL);
    const Surface *source = d3d_surface_lookup(g, arg(g, 3), NULL);
    unsigned filter = arg(g, 6);
    uint32_t color = arg(g, 7);
    if (!source || !dest || !source->data || source->locked || dest->locked ||
        source->width != dest->width || source->height != dest->height ||
        arg(g, 1) || arg(g, 2) || arg(g, 4) || arg(g, 5) ||
        ((filter < 1 || filter > 5) && filter != UINT32_MAX)) {
        fprintf(g->log, "Unsupported D3DX surface copy geometry or filter %08x\n", filter);
        return result(g, 0x8876086cu, 32);
    }
    size_t bytes = surface_bytes(dest);
    bool copy = source->format == dest->format && !color;
    if (!bytes || (!copy && (!supported(source->format) || !supported(dest->format)))) {
        fprintf(g->log, "Unsupported D3DX surface copy format %08x -> %08x\n", source->format, dest->format);
        return false;
    }
    uint32_t *colors = copy ? NULL : decode_texture(g, source);
    if (!copy && !colors)
        return result(g, 0x8007000eu, 32);
    if (!dest->data)
        dest->data = resource_allocate(g, bytes);
    if (!dest->data) {
        free(colors);
        return result(g, 0x8007000eu, 32);
    }
    uint8_t *output = guest_pointer(dest->data);
    if (copy) {
        memmove(output, guest_pointer(source->data), bytes);
    } else {
        if (color) {
            uint32_t key = (color & 0xff00ff00u) | ((color >> 16) & 255) | ((color & 255) << 16);
            size_t count = (size_t)dest->width * dest->height;
            for (size_t i = 0; i < count; ++i)
                if (colors[i] == key)
                    colors[i] = 0;
        }
        encode_level(output, dest, colors);
        free(colors);
    }
    free(dest->decoded);
    dest->decoded = NULL;
    return result(g, 0, 32);
}
