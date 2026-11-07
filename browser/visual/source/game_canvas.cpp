#include "game_canvas.h"
#include "container.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <litehtml/render_image.h>
#include <stdexcept>
namespace visual {
GameCanvas::GameCanvas(const std::shared_ptr<litehtml::document> &doc, SDL_Renderer *r,
                       std::shared_ptr<CanvasBudget> b)
    : html_tag(doc), renderer(r), budget(std::move(b)) {
    if (budget->count >= 32)
        throw std::range_error("Canvas count quota exceeded");
    ++budget->count;
}
GameCanvas::~GameCanvas() {
    if (texture)
        SDL_DestroyTexture(texture);
    budget->bytes -= pixels.size();
    --budget->count;
}
void GameCanvas::resize(unsigned width, unsigned height) {
    if (width > axis_limit || height > axis_limit)
        throw std::range_error("Canvas size quota exceeded");
    size_t size = size_t(width) * height * 4;
    if (size > 16u * 1024u * 1024u - (budget->bytes - pixels.size()))
        throw std::range_error("Canvas memory quota exceeded");
    std::vector<uint8_t> replacement(size, 0);
    budget->bytes = budget->bytes - pixels.size() + size;
    pixels.swap(replacement);
    if (texture) {
        SDL_DestroyTexture(texture);
        texture = nullptr;
    }
    w = width;
    h = height;
    dirty = true;
    initialized = true;
}
void GameCanvas::ensure() {
    if (!initialized)
        read_attributes(false);
}
void GameCanvas::read_attributes(bool force) {
    auto attribute = [&](const char *name) {
        auto *value = get_attr(name);
        return value ? "1" + std::string(value) : std::string("0");
    };
    auto next_width = attribute("width"), next_height = attribute("height");
    if (!force && initialized && next_width == width_attribute && next_height == height_attribute)
        return;
    auto dimension = [&](const char *name, unsigned fallback) {
        auto *s = get_attr(name);
        if (!s)
            return fallback;
        while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')
            s++;
        if (*s < '0' || *s > '9')
            return fallback;
        unsigned n = 0;
        while (*s >= '0' && *s <= '9') {
            if (n > axis_limit)
                throw std::range_error("Canvas size quota exceeded");
            n = n * 10 + unsigned(*s++ - '0');
        }
        return n;
    };
    resize(dimension("width", 300), dimension("height", 150));
    width_attribute = std::move(next_width);
    height_attribute = std::move(next_height);
}
void GameCanvas::get_content_size(litehtml::size &size, litehtml::pixel_t) {
    ensure();
    size.width = int(w);
    size.height = int(h);
}
void GameCanvas::blend(unsigned x, unsigned y, const uint8_t *src, double alpha) {
    auto *dest = pixels.data() + (size_t(y) * w + x) * 4;
    unsigned a = unsigned(std::round(src[3] * std::clamp(alpha, 0., 1.)));
    if (!a)
        return;
    unsigned denominator = a * 255 + dest[3] * (255 - a);
    for (unsigned c = 0; c < 3; c++)
        dest[c] = uint8_t((src[c] * a * 255 + dest[c] * dest[3] * (255 - a) + denominator / 2) /
                          denominator);
    dest[3] = uint8_t((denominator + 127) / 255);
}
void GameCanvas::fill(double x, double y, double width, double height, uint32_t color, double alpha,
                      bool clear) {
    ensure();
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) || !std::isfinite(height))
        return;
    if (!width || !height)
        return;
    if (width < 0) {
        x += width;
        width = -width;
    }
    if (height < 0) {
        y += height;
        height = -height;
    }
    auto edge = [](double value, unsigned bound) {
        return int(std::clamp(value, 0., double(bound)));
    };
    int left = edge(std::floor(x), w), right = edge(std::ceil(x + width), w),
        top = edge(std::floor(y), h), bottom = edge(std::ceil(y + height), h);
    uint8_t source[4] = {uint8_t(color), uint8_t(color >> 8), uint8_t(color >> 16),
                         uint8_t(color >> 24)};
    for (int py = top; py < bottom; py++)
        for (int px = left; px < right; px++) {
            if (clear)
                std::memset(pixels.data() + (size_t(py) * w + px) * 4, 0, 4);
            else
                blend(unsigned(px), unsigned(py), source, alpha);
        }
    if (right > left && bottom > top)
        dirty = true;
}
void GameCanvas::put(const uint8_t *source, unsigned sw, unsigned sh, int dx, int dy, int sx,
                     int sy, int rw, int rh) {
    ensure();
    int64_t left = std::max<int64_t>({0, sx, -int64_t(dx)}),
            top = std::max<int64_t>({0, sy, -int64_t(dy)}),
            right = std::min<int64_t>({sw, int64_t(sx) + rw, int64_t(w) - dx}),
            bottom = std::min<int64_t>({sh, int64_t(sy) + rh, int64_t(h) - dy});
    for (auto y = top; y < bottom; y++)
        if (right > left)
            std::memcpy(pixels.data() + ((size_t(y + dy) * w) + size_t(left + dx)) * 4,
                        source + ((size_t(y) * sw) + size_t(left)) * 4, size_t(right - left) * 4);
    if (right > left && bottom > top)
        dirty = true;
}
std::vector<uint8_t> GameCanvas::get(int x, int y, unsigned width, unsigned height) {
    ensure();
    if (!width || !height || width > axis_limit || height > axis_limit)
        throw std::range_error("ImageData size quota exceeded");
    std::vector<uint8_t> out(size_t(width) * height * 4, 0);
    for (unsigned py = 0; py < height; py++) {
        int64_t sy = int64_t(y) + py;
        if (sy < 0 || sy >= h)
            continue;
        int64_t left = std::max<int64_t>(0, -int64_t(x)),
                right = std::min<int64_t>(width, int64_t(w) - x);
        if (right > left)
            std::memcpy(out.data() + (size_t(py) * width + size_t(left)) * 4,
                        pixels.data() + (size_t(sy) * w + size_t(int64_t(x) + left)) * 4,
                        size_t(right - left) * 4);
    }
    return out;
}
void GameCanvas::image(GameCanvas &source, double sx, double sy, double sw, double sh, double dx,
                       double dy, double dw, double dh, double alpha, bool smooth) {
    ensure();
    source.ensure();
    for (auto v : {sx, sy, sw, sh, dx, dy, dw, dh})
        if (!std::isfinite(v))
            return;
    if (!sw || !sh || !dw || !dh)
        return;
    if (sw < 0) {
        sx += sw;
        sw = -sw;
    }
    if (sh < 0) {
        sy += sh;
        sh = -sh;
    }
    if (dw < 0) {
        dx += dw;
        dw = -dw;
    }
    if (dh < 0) {
        dy += dh;
        dh = -dh;
    }
    std::vector<uint8_t> copy;
    if (&source == this)
        copy = source.pixels;
    auto *data = &source == this ? copy.data() : source.pixels.data();
    auto edge = [](double value, unsigned bound) {
        return int(std::clamp(value, 0., double(bound)));
    };
    int left = edge(std::floor(dx), w), right = edge(std::ceil(dx + dw), w),
        top = edge(std::floor(dy), h), bottom = edge(std::ceil(dy + dh), h);
    for (int y = top; y < bottom; y++)
        for (int x = left; x < right; x++) {
            double cx = sx + (x + .5 - dx) * sw / dw, cy = sy + (y + .5 - dy) * sh / dh;
            if (!std::isfinite(cx) || !std::isfinite(cy) || cx < 0 || cy < 0 || cx >= source.w ||
                cy >= source.h)
                continue;
            if (!smooth) {
                blend(x, y, data + (size_t(cy) * source.w + size_t(cx)) * 4, alpha);
                continue;
            }
            double px = cx - .5, py = cy - .5, fx = px - std::floor(px), fy = py - std::floor(py),
                   sum[4] = {};
            int ix = int(std::floor(px)), iy = int(std::floor(py));
            for (int oy = 0; oy < 2; oy++)
                for (int ox = 0; ox < 2; ox++) {
                    auto *p = data + (size_t(std::clamp(iy + oy, 0, int(source.h) - 1)) * source.w +
                                      size_t(std::clamp(ix + ox, 0, int(source.w) - 1))) *
                                         4;
                    double weight = (ox ? fx : 1 - fx) * (oy ? fy : 1 - fy);
                    sum[3] += p[3] * weight;
                    for (int c = 0; c < 3; c++)
                        sum[c] += p[c] * p[3] * weight;
                }
            uint8_t pixel[4] = {0, 0, 0, uint8_t(std::round(sum[3]))};
            if (sum[3] > 0)
                for (int c = 0; c < 3; c++)
                    pixel[c] = uint8_t(std::clamp(std::round(sum[c] / sum[3]), 0., 255.));
            blend(x, y, pixel, alpha);
        }
    if (right > left && bottom > top)
        dirty = true;
}
void GameCanvas::draw(litehtml::uint_ptr hdc, litehtml::pixel_t x, litehtml::pixel_t y,
                      const litehtml::position *clip,
                      const std::shared_ptr<litehtml::render_item> &item) {
    html_tag::draw(hdc, x, y, clip, item);
    auto position = item->pos();
    position.x += x;
    position.y += y;
    if (!position.does_intersect(clip))
        return;
    ensure();
    if (!w || !h)
        return;
    if (!texture) {
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
                                    int(w), int(h));
        if (texture)
            SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    }
    auto *container = dynamic_cast<Container *>(get_document()->container());
    if (!texture || !container) {
        if (container)
            ++container->unsupported;
        return;
    }
    bool updated = dirty;
    if (dirty && SDL_UpdateTexture(texture, nullptr, pixels.data(), int(w * 4)) != 0) {
        ++container->unsupported;
        return;
    }
    dirty = false;
    container->draw_canvas(texture, position);
    if (updated)
        ++budget->frames;
}
std::shared_ptr<litehtml::render_item>
GameCanvas::create_render_item(const std::shared_ptr<litehtml::render_item> &parent) {
    auto item = std::make_shared<litehtml::render_item_image>(shared_from_this());
    item->parent(parent);
    return item;
}

} // namespace visual
