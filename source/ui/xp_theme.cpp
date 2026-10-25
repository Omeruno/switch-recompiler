// xp_theme.cpp — обои, панель задач и кнопка «Пуск» в духе Windows XP.

#include "xp_theme.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ui {
namespace xp {
namespace {

uint32_t Mix(uint32_t a, uint32_t b, float t) {
    t = std::min(1.0f, std::max(0.0f, t));
    uint32_t out = 0;
    for (int sh = 0; sh <= 16; sh += 8) {
        float ca = static_cast<float>((a >> sh) & 255), cb = static_cast<float>((b >> sh) & 255);
        out |= static_cast<uint32_t>(ca + (cb - ca) * t + 0.5f) << sh;
    }
    return out;
}
void Fill(Surface& s, Rect r, uint32_t c) {
    r = r.Intersect(Rect(0, 0, s.width, s.height));
    for (int y = r.top; y < r.bottom; ++y)
        for (int x = r.left; x < r.right; ++x) s.Set(x, y, c);
}
// Вертикальный градиент по опорным точкам (доля высоты, цвет)
struct Stop { float at; uint32_t color; };
template <size_t N>
uint32_t Ramp(const Stop (&stops)[N], float t) {
    if (t <= stops[0].at) return stops[0].color;
    for (size_t i = 1; i < N; ++i)
        if (t <= stops[i].at) return Mix(stops[i - 1].color, stops[i].color, (t - stops[i - 1].at) / (stops[i].at - stops[i - 1].at));
    return stops[N - 1].color;
}

// Плавный шум по решётке (детерминированный)
float Hash(int x, int y) {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return static_cast<float>((h ^ (h >> 16)) & 0xFFFF) / 65535.0f;
}
float Smooth(float t) { return t * t * (3 - 2 * t); }
float Noise(float x, float y) {
    int xi = static_cast<int>(std::floor(x)), yi = static_cast<int>(std::floor(y));
    float fx = Smooth(x - xi), fy = Smooth(y - yi);
    float a = Hash(xi, yi), b = Hash(xi + 1, yi), c = Hash(xi, yi + 1), d = Hash(xi + 1, yi + 1);
    return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy;
}
float Fbm(float x, float y, int octaves) {
    float sum = 0, amp = 0.5f, norm = 0;
    for (int i = 0; i < octaves; ++i) {
        sum += Noise(x, y) * amp;
        norm += amp;
        x *= 2.03f; y *= 2.03f; amp *= 0.5f;
    }
    return sum / norm;
}

// Небо, облака и холм; w x h
void BuildWallpaper(Surface& out) {
    const int w = out.width, h = out.height;
    const Stop sky[] = {{0.0f, 0x2457C8}, {0.35f, 0x3D7CDC}, {0.62f, 0x76AEEB}, {0.75f, 0xA9CDF2}};
    const Stop grass[] = {{0.0f, 0x8ACB4C}, {0.12f, 0x6DB534}, {0.45f, 0x4E9A22}, {1.0f, 0x2F7414}};
    for (int y = 0; y < h; ++y) {
        float fy = static_cast<float>(y) / h;
        for (int x = 0; x < w; ++x) {
            float fx = static_cast<float>(x) / w;
            // гребень холма: подъём слева, вершина около трети, пологий спуск вправо
            float crest = 0.60f - 0.085f * std::exp(-std::pow((fx - 0.30f) / 0.42f, 2.0f)) + 0.03f * fx;
            uint32_t c = Ramp(sky, fy);
            // облака: растянутый по горизонтали шум, гуще в верхней половине
            float n = Fbm(fx * 6.0f, fy * 11.0f, 5);
            float band = 1.0f - std::fabs(fy - 0.28f) * 2.6f;
            float cloud = std::max(0.0f, (n - 0.52f) * 3.2f) * std::max(0.0f, band);
            c = Mix(c, 0xFFFFFF, std::min(0.92f, cloud));
            // далёкие голубоватые горы у правого края горизонта: зубчатый
            // гребень, плавно поднимающийся из-за холма
            float rise = std::min(1.0f, std::max(0.0f, (fx - 0.62f) / 0.18f));
            float ridge = crest - rise * (0.012f + 0.03f * Fbm(fx * 14.0f, 7.0f, 4));
            if (rise > 0 && fy > ridge) c = Mix(c, Mix(0x9DB3CB, 0x7D95B0, (fy - ridge) * 40.0f), 0.85f);
            if (fy > crest) {
                float depth = (fy - crest) / (1.0f - crest);
                uint32_t g = Ramp(grass, depth);
                float tex = Fbm(fx * 60.0f, fy * 90.0f, 3) - 0.5f;
                g = tex > 0 ? Mix(g, 0xB4E07A, tex * 0.35f) : Mix(g, 0x1E5A0C, -tex * 0.35f);
                // мягкий край холма: сглаживание по вертикали на одну строку
                float edge = std::min(1.0f, (fy - crest) * h);
                c = Mix(c, g, edge);
            }
            out.Set(x, y, c);
        }
    }
}

// Строки панели задач Luna (сверху вниз), растягиваются на высоту панели
const Stop kTaskbar[] = {{0.0f, 0x3E87F2}, {0.06f, 0x5A9CF8}, {0.14f, 0x3B7FEC}, {0.5f, 0x2763DC}, {0.9f, 0x2257D2}, {1.0f, 0x1A47B4}};
const Stop kStart[] = {{0.0f, 0x6CC86A}, {0.12f, 0x4AAF48}, {0.5f, 0x379F37}, {0.85f, 0x2E8C2E}, {1.0f, 0x2A7A2A}};
const Stop kStartDown[] = {{0.0f, 0x2E7D2E}, {0.5f, 0x2A852A}, {1.0f, 0x297229}};
const Stop kTray[] = {{0.0f, 0x1A9BF0}, {0.1f, 0x2DB0FA}, {0.5f, 0x149AEE}, {1.0f, 0x0F7FD4}};

} // namespace

const Surface& Wallpaper(int w, int h) {
    static OwnedSurface cache;
    if (cache.s.width != w || cache.s.height != h) {
        cache.Allocate(w, h);
        BuildWallpaper(cache.s);
    }
    return cache.s;
}

uint32_t WallpaperPixel(int w, int h, int x, int y) {
    const Surface& c = Wallpaper(w, h);
    return (x >= 0 && y >= 0 && x < w && y < h) ? c.Get(x, y) : 0;
}

void DrawWallpaper(Surface& s, const Rect& r) {
    const Surface& cache = Wallpaper(r.Width(), r.Height());
    Rect d = r.Intersect(Rect(0, 0, s.width, s.height));
    for (int y = d.top; y < d.bottom; ++y)
        for (int x = d.left; x < d.right; ++x) s.Set(x, y, cache.Get(x - r.left, y - r.top));
}

void DrawTaskbar(Surface& s, const Rect& r) {
    for (int y = r.top; y < r.bottom; ++y)
        Fill(s, Rect(r.left, y, r.right, y + 1), Ramp(kTaskbar, static_cast<float>(y - r.top) / std::max(1, r.Height() - 1)));
}

void DrawStartButton(Surface& s, const Rect& r, bool pressed, bool hot) {
    const int radius = r.Height() / 2;
    for (int y = r.top; y < r.bottom; ++y) {
        float t = static_cast<float>(y - r.top) / std::max(1, r.Height() - 1);
        uint32_t c = pressed ? Ramp(kStartDown, t) : Ramp(kStart, t);
        if (hot && !pressed) c = Mix(c, 0xFFFFFF, 0.12f);
        // правый край — дуга радиуса половины высоты, со сглаживанием
        float dy = (y - r.top) + 0.5f - r.Height() / 2.0f;
        float half = std::sqrt(std::max(0.0f, radius * radius - dy * dy));
        float edge = static_cast<float>(r.right - radius) + half;
        for (int x = r.left; x < r.right; ++x) {
            float cover = std::min(1.0f, std::max(0.0f, edge - x));
            if (cover <= 0) continue;
            uint32_t px = c;
            if (edge - x < 2.5f) px = Mix(c, 0x1B5E1B, 0.55f);  // тёмная кромка справа
            if (x == r.left) px = Mix(c, 0x1B5E1B, 0.4f);
            s.Set(x, y, Mix(s.Get(x, y), px, cover));
        }
    }
}

void DrawStartGlyph(Surface& s, int x0, int y0) {
    // глянцевый белый круг с зелёной точкой: свой знак, не логотип Windows
    const float cx = x0 + 10.0f, cy = y0 + 10.0f;
    for (int y = y0; y < y0 + 20; ++y)
        for (int x = x0; x < x0 + 20; ++x) {
            float d = std::hypot(x + 0.5f - cx, y + 0.5f - cy);
            float cover = std::min(1.0f, std::max(0.0f, 9.5f - d));
            if (cover <= 0) continue;
            uint32_t c = Mix(0xFFFFFF, 0xD7EFD5, (y - y0) / 20.0f);
            float inner = std::hypot(x + 0.5f - cx, y + 0.5f - cy - 0.5f);
            if (inner < 4.5f) c = Mix(0x3FA63D, 0x2C8A2C, inner / 4.5f);
            s.Set(x, y, Mix(s.Get(x, y), c, cover));
        }
}

void DrawTaskButton(Surface& s, const Rect& r, bool active, bool hot) {
    const Stop normal[] = {{0.0f, 0x5C9CF8}, {0.15f, 0x3C82F0}, {1.0f, 0x2A68DE}};
    const Stop down[] = {{0.0f, 0x1A49B0}, {0.2f, 0x1E54C2}, {1.0f, 0x2260CF}};
    for (int y = r.top; y < r.bottom; ++y) {
        float t = static_cast<float>(y - r.top) / std::max(1, r.Height() - 1);
        uint32_t c = active ? Ramp(down, t) : Ramp(normal, t);
        if (hot && !active) c = Mix(c, 0xFFFFFF, 0.12f);
        Fill(s, Rect(r.left + 1, y, r.right - 1, y + 1), c);
    }
    Fill(s, Rect(r.left, r.top + 2, r.left + 1, r.bottom - 2), active ? 0x113A91 : 0x6FA8FA);
    Fill(s, Rect(r.right - 1, r.top + 2, r.right, r.bottom - 2), 0x1A48AE);
}

void DrawTray(Surface& s, const Rect& r) {
    for (int y = r.top; y < r.bottom; ++y)
        Fill(s, Rect(r.left, y, r.right, y + 1), Ramp(kTray, static_cast<float>(y - r.top) / std::max(1, r.Height() - 1)));
    Fill(s, Rect(r.left, r.top, r.left + 1, r.bottom), 0x0B4FB0);
    Fill(s, Rect(r.left + 1, r.top, r.left + 2, r.bottom), 0x3CC1FF);
}

} // namespace xp
} // namespace ui
