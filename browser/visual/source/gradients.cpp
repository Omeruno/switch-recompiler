#include "container.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
namespace visual {
using Stops = std::vector<litehtml::background_layer::color_point>;
// Interpolate premultiplied sRGB; transparent stops do not introduce black fringes.
static litehtml::web_color sample(const Stops &stops, float x) {
    if (stops.empty())
        return {0, 0, 0, 0};
    auto upper = std::upper_bound(stops.begin(), stops.end(), x,
                                  [](float t, const auto &s) { return t < s.offset; });
    if (upper == stops.begin())
        return upper->color;
    if (upper == stops.end())
        return stops.back().color;
    const auto &b = *upper, &a = *std::prev(upper);
    float t =
        b.offset > a.offset ? std::clamp((x - a.offset) / (b.offset - a.offset), 0.f, 1.f) : 1;
    if (a.hint && b.offset > a.offset) {
        float h = (*a.hint - a.offset) / (b.offset - a.offset);
        if (h > 0 && h < 1)
            t = std::pow(t, std::log(.5f) / std::log(h));
    }
    float alpha = a.color.alpha * (1 - t) + b.color.alpha * t;
    auto channel = [&](int c1, int c2) {
        return uint8_t(std::clamp(
            std::lround(alpha > 0 ? (c1 * a.color.alpha * (1 - t) + c2 * b.color.alpha * t) / alpha
                                  : 0),
            0l, 255l));
    };
    return {channel(a.color.red, b.color.red), channel(a.color.green, b.color.green),
            channel(a.color.blue, b.color.blue), uint8_t(std::clamp(std::lround(alpha), 0l, 255l))};
}
void Container::paint_gradient(const litehtml::background_layer &l,
                               const litehtml::background_layer::gradient_base &g,
                               const std::string &geometry,
                               const std::function<float(float, float)> &at) {
    SDL_Rect active_clip, border = screen(l.border_box), background_clip = screen(l.clip_box),
                          visible;
    SDL_RenderGetClipRect(renderer, &active_clip);
    if (!SDL_IntersectRect(&active_clip, &border, &visible) ||
        !SDL_IntersectRect(&visible, &background_clip, &border))
        return;
    float w = float(l.origin_box.width), h = float(l.origin_box.height);
    if (!(w > 0 && h > 0) || !std::isfinite(w) || !std::isfinite(h) || g.color_points.empty())
        return;
    std::ostringstream key;
    key << std::setprecision(9) << geometry << ':' << w << ':' << h;
    for (auto &stop : g.color_points)
        key << ':' << stop.offset << ',' << int(stop.color.red) << ',' << int(stop.color.green)
            << ',' << int(stop.color.blue) << ',' << int(stop.color.alpha) << ','
            << (stop.hint ? *stop.hint : -1);
    auto found = gradients.find(key.str());
    if (found == gradients.end()) {
        // A bounded, linearly filtered tile avoids allocating a full tall document.
        int tw = std::clamp(int(std::ceil(std::min(w, 512.f))), 1, 512),
            th = std::clamp(int(std::ceil(std::min(h, 512.f))), 1, 512);
        SDL_Surface *surface =
            SDL_CreateRGBSurfaceWithFormat(0, tw, th, 32, SDL_PIXELFORMAT_RGBA32);
        if (!surface)
            return;
        for (int y = 0; y < th; y++)
            for (int x = 0; x < tw; x++) {
                auto c = sample(g.color_points, at((x + .5f) * w / tw, (y + .5f) * h / th));
                uint8_t *p = static_cast<uint8_t *>(surface->pixels) + y * surface->pitch + x * 4;
                p[0] = c.red;
                p[1] = c.green;
                p[2] = c.blue;
                p[3] = c.alpha;
            }
        SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
        SDL_FreeSurface(surface);
        if (!texture)
            return;
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(texture, SDL_ScaleModeLinear);
        size_t bytes = size_t(tw) * th * 4;
        if (gradient_bytes + bytes > 8u * 1024u * 1024u || gradients.size() >= 64) {
            for (auto &kv : gradients)
                SDL_DestroyTexture(kv.second);
            gradients.clear();
            gradient_bytes = 0;
        }
        found = gradients.emplace(key.str(), texture).first;
        gradient_bytes += bytes;
    }
    paint_tile(found->second, l);
    ++painted_gradients;
}
void Container::draw_linear_gradient(litehtml::uint_ptr, const litehtml::background_layer &l,
                                     const litehtml::background_layer::linear_gradient &g) {
    float sx = g.start.x - float(l.origin_box.x), sy = g.start.y - float(l.origin_box.y),
          dx = g.end.x - g.start.x, dy = g.end.y - g.start.y, len = dx * dx + dy * dy;
    std::ostringstream key;
    key << std::setprecision(9) << "L:" << sx << ':' << sy << ':' << dx << ':' << dy;
    paint_gradient(l, g, key.str(), [=](float x, float y) {
        return len > 0 ? ((x - sx) * dx + (y - sy) * dy) / len : 1.f;
    });
}
void Container::draw_radial_gradient(litehtml::uint_ptr, const litehtml::background_layer &l,
                                     const litehtml::background_layer::radial_gradient &g) {
    float cx = g.position.x - float(l.origin_box.x), cy = g.position.y - float(l.origin_box.y),
          rx = g.radius.x, ry = g.radius.y;
    std::ostringstream key;
    key << std::setprecision(9) << "R:" << cx << ':' << cy << ':' << rx << ':' << ry;
    paint_gradient(l, g, key.str(), [=](float x, float y) {
        return rx > 0 && ry > 0 ? std::hypot((x - cx) / rx, (y - cy) / ry) : 1.f;
    });
}
void Container::draw_conic_gradient(litehtml::uint_ptr, const litehtml::background_layer &l,
                                    const litehtml::background_layer::conic_gradient &g) {
    float cx = g.position.x - float(l.origin_box.x), cy = g.position.y - float(l.origin_box.y),
          angle = g.angle;
    std::ostringstream key;
    key << std::setprecision(9) << "C:" << cx << ':' << cy << ':' << angle;
    paint_gradient(l, g, key.str(), [=](float x, float y) {
        float t = std::atan2(x - cx, cy - y) / 6.28318530718f - angle / 360;
        t -= std::floor(t);
        return t;
    });
}
} // namespace visual
