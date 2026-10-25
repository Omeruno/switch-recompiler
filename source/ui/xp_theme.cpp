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


// --- Окна ---

namespace {
// Скруглённый прямоугольник: покрытие пикселя (x, y) с радиусом rad
float RoundCover(const Rect& r, int x, int y, float rad, bool top, bool bottom) {
    float px = x + 0.5f, py = y + 0.5f;
    float cx = px, cy = py;
    if (top && py < r.top + rad) cy = r.top + rad;
    else if (bottom && py > r.bottom - rad) cy = r.bottom - rad;
    else return 1;
    if (px < r.left + rad) cx = r.left + rad;
    else if (px > r.right - rad) cx = r.right - rad;
    else return 1;
    return std::min(1.0f, std::max(0.0f, rad - std::hypot(px - cx, py - cy) + 0.5f));
}
void Blend(Surface& s, int x, int y, uint32_t c, float a) {
    if (x < 0 || y < 0 || x >= s.width || y >= s.height || a <= 0) return;
    s.Set(x, y, a >= 1 ? c : Mix(s.Get(x, y), c, a));
}
void Frame(Surface& s, const Rect& r, uint32_t c) {
    Fill(s, Rect(r.left, r.top, r.right, r.top + 1), c);
    Fill(s, Rect(r.left, r.bottom - 1, r.right, r.bottom), c);
    Fill(s, Rect(r.left, r.top, r.left + 1, r.bottom), c);
    Fill(s, Rect(r.right - 1, r.top, r.right, r.bottom), c);
}
// Заливка скруглённого прямоугольника вертикальным градиентом по опорным точкам
template <size_t N>
void RoundRamp(Surface& s, const Rect& r, float rad, const Stop (&stops)[N], bool top = true, bool bottom = true) {
    for (int y = r.top; y < r.bottom; ++y) {
        uint32_t c = Ramp(stops, static_cast<float>(y - r.top) / std::max(1, r.Height() - 1));
        for (int x = r.left; x < r.right; ++x) Blend(s, x, y, c, RoundCover(r, x, y, rad, top, bottom));
    }
}
void Disc(Surface& s, float cx, float cy, float rad, uint32_t c0, uint32_t c1) {
    for (int y = static_cast<int>(cy - rad - 1); y <= static_cast<int>(cy + rad + 1); ++y)
        for (int x = static_cast<int>(cx - rad - 1); x <= static_cast<int>(cx + rad + 1); ++x) {
            float d = std::hypot(x + 0.5f - cx, y + 0.5f - cy);
            Blend(s, x, y, Mix(c0, c1, (y - (cy - rad)) / (2 * rad)), std::min(1.0f, std::max(0.0f, rad - d + 0.5f)));
        }
}
// Толстая сглаженная линия
void Thick(Surface& s, float x0, float y0, float x1, float y1, float w, uint32_t c) {
    int minx = static_cast<int>(std::min(x0, x1) - w - 1), maxx = static_cast<int>(std::max(x0, x1) + w + 1);
    int miny = static_cast<int>(std::min(y0, y1) - w - 1), maxy = static_cast<int>(std::max(y0, y1) + w + 1);
    float vx = x1 - x0, vy = y1 - y0, len2 = std::max(1e-6f, vx * vx + vy * vy);
    for (int y = miny; y <= maxy; ++y)
        for (int x = minx; x <= maxx; ++x) {
            float px = x + 0.5f - x0, py = y + 0.5f - y0;
            float t = std::max(0.0f, std::min(1.0f, (px * vx + py * vy) / len2));
            float d = std::hypot(px - vx * t, py - vy * t);
            Blend(s, x, y, c, std::min(1.0f, std::max(0.0f, w / 2 - d + 0.5f)));
        }
}

const Stop kTitle[] = {{0.0f, 0x0997FF}, {0.08f, 0x0053EE}, {0.4f, 0x0050EE}, {0.86f, 0x0066FF}, {1.0f, 0x0058E8}};
const Stop kTitleInactive[] = {{0.0f, 0xA9C2F1}, {0.1f, 0x8AABEB}, {0.6f, 0x7A9EE6}, {1.0f, 0x8FB1EF}};
} // namespace

void DrawWindowFrame(Surface& s, const Rect& r, int titleH, bool active) {
    const float rad = 8;
    // заголовок с верхними скруглениями
    Rect title(r.left, r.top, r.right, r.top + titleH);
    if (active) RoundRamp(s, title, rad, kTitle, true, false);
    else RoundRamp(s, title, rad, kTitleInactive, true, false);
    // рамка окна: синяя, 4 px по бокам и снизу
    const uint32_t border = active ? 0x0841D2 : 0x7D9CE4;
    Fill(s, Rect(r.left, r.top + titleH, r.left + 4, r.bottom), border);
    Fill(s, Rect(r.right - 4, r.top + titleH, r.right, r.bottom), border);
    Fill(s, Rect(r.left, r.bottom - 4, r.right, r.bottom), border);
    Fill(s, Rect(r.left + 1, r.top + titleH, r.left + 2, r.bottom - 1), active ? 0x166AEE : 0xA5BDF1);
    Fill(s, Rect(r.left + 4, r.top + titleH, r.right - 4, r.bottom - 4), 0xECE9D8);
    // тёмная кромка снаружи
    Fill(s, Rect(r.left, r.top + titleH, r.left + 1, r.bottom), active ? 0x0019CF : 0x6E8AD3);
    Fill(s, Rect(r.right - 1, r.top + titleH, r.right, r.bottom), active ? 0x0019CF : 0x6E8AD3);
    Fill(s, Rect(r.left, r.bottom - 1, r.right, r.bottom), active ? 0x0019CF : 0x6E8AD3);
}

void DrawCaptionButton(Surface& s, const Rect& r, CaptionButton kind, bool hot, bool pressed) {
    const Stop blue[] = {{0.0f, 0x7DA8F5}, {0.45f, 0x3D78EC}, {1.0f, 0x2F64E0}};
    const Stop blueHot[] = {{0.0f, 0xA3C3FB}, {0.45f, 0x5E92F6}, {1.0f, 0x4A7FF0}};
    const Stop blueDown[] = {{0.0f, 0x1C46B8}, {1.0f, 0x2D5FD8}};
    const Stop red[] = {{0.0f, 0xF29B84}, {0.45f, 0xE2593B}, {1.0f, 0xC93A20}};
    const Stop redHot[] = {{0.0f, 0xF8B9A6}, {0.45f, 0xEC7558}, {1.0f, 0xD54B30}};
    const Stop redDown[] = {{0.0f, 0xA62A12}, {1.0f, 0xC53C22}};
    Rect in(r.left + 1, r.top + 1, r.right - 1, r.bottom - 1);
    // белая кайма и цветная середина
    const Stop rim[] = {{0.0f, 0xFFFFFF}, {0.5f, 0xF0F4FF}, {1.0f, 0xE0E8FA}};
    RoundRamp(s, r, 4, rim);
    if (kind == CaptionButton::Close) {
        if (pressed) RoundRamp(s, in, 3, redDown); else if (hot) RoundRamp(s, in, 3, redHot); else RoundRamp(s, in, 3, red);
    } else {
        if (pressed) RoundRamp(s, in, 3, blueDown); else if (hot) RoundRamp(s, in, 3, blueHot); else RoundRamp(s, in, 3, blue);
    }
    float cx = (r.left + r.right) / 2.0f, cy = (r.top + r.bottom) / 2.0f, k = r.Width() / 24.0f;
    const uint32_t w = 0xFFFFFF;
    switch (kind) {
    case CaptionButton::Close:
        Thick(s, cx - 5 * k, cy - 5 * k, cx + 5 * k, cy + 5 * k, 2.6f * k, w);
        Thick(s, cx + 5 * k, cy - 5 * k, cx - 5 * k, cy + 5 * k, 2.6f * k, w);
        break;
    case CaptionButton::Minimize:
        Fill(s, Rect(static_cast<int>(cx - 6 * k), static_cast<int>(cy + 3 * k), static_cast<int>(cx + 2 * k), static_cast<int>(cy + 6 * k)), w);
        break;
    case CaptionButton::Maximize: {
        Rect box(static_cast<int>(cx - 6 * k), static_cast<int>(cy - 6 * k), static_cast<int>(cx + 6 * k), static_cast<int>(cy + 6 * k));
        Frame(s, box, w);
        Fill(s, Rect(box.left, box.top, box.right, box.top + static_cast<int>(3 * k)), w);
        break;
    }
    }
}

void DrawPushButton(Surface& s, const Rect& r, bool enabled, bool hot, bool pressed, bool focused) {
    const Stop normal[] = {{0.0f, 0xFFFFFF}, {0.7f, 0xF4F4F0}, {0.9f, 0xE6E4DA}, {1.0f, 0xD6D0C5}};
    const Stop down[] = {{0.0f, 0xE3E1D7}, {1.0f, 0xF0EFE9}};
    const Stop border[] = {{0.0f, 0x003C74}, {1.0f, 0x003C74}};
    const Stop disabledBorder[] = {{0.0f, 0xC9C7BA}, {1.0f, 0xC9C7BA}};
    if (enabled) RoundRamp(s, r, 3, border); else RoundRamp(s, r, 3, disabledBorder);
    Rect in(r.left + 1, r.top + 1, r.right - 1, r.bottom - 1);
    const Stop flat[] = {{0.0f, 0xF5F4EA}, {1.0f, 0xF5F4EA}};
    if (!enabled) { RoundRamp(s, in, 2, flat); return; }
    if (pressed) RoundRamp(s, in, 2, down); else RoundRamp(s, in, 2, normal);
    if (hot && !pressed) {
        // оранжевая подсветка наведения
        Frame(s, Rect(in.left, in.top, in.right, in.bottom), 0xF8B330);
        Frame(s, Rect(in.left + 1, in.top + 1, in.right - 1, in.bottom - 1), 0xFDD889);
    } else if (focused) {
        Frame(s, Rect(in.left, in.top, in.right, in.bottom), 0x99B4E8);
        Frame(s, Rect(in.left + 1, in.top + 1, in.right - 1, in.bottom - 1), 0xCEE7FF);
    }
    if (focused) DrawFocusRect(s, Rect(r.left + 4, r.top + 4, r.right - 4, r.bottom - 4));
}

void DrawToolButton(Surface& s, const Rect& r, bool hot, bool pressed) {
    if (!hot && !pressed) return;
    const Stop hover[] = {{0.0f, 0xFFFFFF}, {0.6f, 0xF7F6F1}, {1.0f, 0xE1DED3}};
    const Stop down[] = {{0.0f, 0xD8D5C8}, {1.0f, 0xE9E7DE}};
    const Stop edge[] = {{0.0f, 0xC5C2B2}, {1.0f, 0xC5C2B2}};
    RoundRamp(s, r, 3, edge);
    Rect in(r.left + 1, r.top + 1, r.right - 1, r.bottom - 1);
    if (pressed) RoundRamp(s, in, 2, down); else RoundRamp(s, in, 2, hover);
}

void DrawToolSeparator(Surface& s, int x, int top, int bottom) {
    Fill(s, Rect(x, top, x + 1, bottom), 0xC5C2B2);
    Fill(s, Rect(x + 1, top, x + 2, bottom), 0xFFFFFF);
}

void DrawToolGlyph(Surface& s, ToolGlyph kind, int x, int y, int size, bool enabled) {
    const float k = size / 24.0f, cx = x + size / 2.0f, cy = y + size / 2.0f;
    auto greenDisc = [&]() {
        Disc(s, cx, cy, 11 * k, enabled ? 0x2A8A2A : 0x9A9A9A, enabled ? 0x2A8A2A : 0x9A9A9A);
        Disc(s, cx, cy, 10 * k, enabled ? 0x8BDA6E : 0xD6D6D6, enabled ? 0x2F9E2F : 0xAAAAAA);
    };
    switch (kind) {
    case ToolGlyph::Back:
    case ToolGlyph::Forward: {
        greenDisc();
        float d = kind == ToolGlyph::Back ? -1.0f : 1.0f;
        Thick(s, cx - 5 * k * d, cy, cx + 5 * k * d, cy, 3.2f * k, 0xFFFFFF);
        Thick(s, cx + 5 * k * d, cy, cx + 0.5f * k * d, cy - 4.5f * k, 3.2f * k, 0xFFFFFF);
        Thick(s, cx + 5 * k * d, cy, cx + 0.5f * k * d, cy + 4.5f * k, 3.2f * k, 0xFFFFFF);
        break;
    }
    case ToolGlyph::Go:
        Fill(s, Rect(x, y, x + size, y + size), enabled ? 0x2E9E2E : 0xA0A0A0);
        Frame(s, Rect(x, y, x + size, y + size), 0x1E6A1E);
        Thick(s, cx - 4 * k, cy, cx + 4 * k, cy, 2.6f * k, 0xFFFFFF);
        Thick(s, cx + 4 * k, cy, cx, cy - 4 * k, 2.6f * k, 0xFFFFFF);
        Thick(s, cx + 4 * k, cy, cx, cy + 4 * k, 2.6f * k, 0xFFFFFF);
        break;
    case ToolGlyph::Up:
        // папка со стрелкой вверх
        Fill(s, Rect(static_cast<int>(x + 2 * k), static_cast<int>(y + 9 * k), static_cast<int>(x + 22 * k), static_cast<int>(y + 21 * k)), enabled ? 0xF1C04E : 0xC8C8C8);
        Fill(s, Rect(static_cast<int>(x + 2 * k), static_cast<int>(y + 7 * k), static_cast<int>(x + 10 * k), static_cast<int>(y + 9 * k)), enabled ? 0xE2AD38 : 0xBBBBBB);
        Frame(s, Rect(static_cast<int>(x + 2 * k), static_cast<int>(y + 9 * k), static_cast<int>(x + 22 * k), static_cast<int>(y + 21 * k)), enabled ? 0xAE7E1A : 0x909090);
        Thick(s, cx + 2 * k, y + 18 * k, cx + 2 * k, y + 4 * k, 3.4f * k, enabled ? 0x2F9E2F : 0x909090);
        Thick(s, cx + 2 * k, y + 3 * k, cx - 3 * k, y + 8 * k, 3.0f * k, enabled ? 0x2F9E2F : 0x909090);
        Thick(s, cx + 2 * k, y + 3 * k, cx + 7 * k, y + 8 * k, 3.0f * k, enabled ? 0x2F9E2F : 0x909090);
        break;
    case ToolGlyph::Search:
        Disc(s, x + 10 * k, y + 10 * k, 7 * k, 0x3A6EA5, 0x3A6EA5);
        Disc(s, x + 10 * k, y + 10 * k, 5.2f * k, 0xD8ECFF, 0x9CC8F2);
        Thick(s, x + 15 * k, y + 15 * k, x + 21 * k, y + 21 * k, 3.6f * k, 0x7A5A2A);
        break;
    case ToolGlyph::Folders:
        Fill(s, Rect(static_cast<int>(x + 3 * k), static_cast<int>(y + 6 * k), static_cast<int>(x + 11 * k), static_cast<int>(y + 9 * k)), 0xE2AD38);
        Fill(s, Rect(static_cast<int>(x + 3 * k), static_cast<int>(y + 9 * k), static_cast<int>(x + 21 * k), static_cast<int>(y + 20 * k)), 0xF6CD62);
        Frame(s, Rect(static_cast<int>(x + 3 * k), static_cast<int>(y + 9 * k), static_cast<int>(x + 21 * k), static_cast<int>(y + 20 * k)), 0xAE7E1A);
        break;
    case ToolGlyph::Views:
        for (int row = 0; row < 2; ++row)
            for (int col = 0; col < 2; ++col) {
                Rect t(static_cast<int>(x + (3 + col * 10) * k), static_cast<int>(y + (3 + row * 10) * k),
                       static_cast<int>(x + (11 + col * 10) * k), static_cast<int>(y + (11 + row * 10) * k));
                Fill(s, t, 0x5A8FE0);
                Frame(s, t, 0x1D4B9E);
            }
        break;
    }
}

void DrawTaskPane(Surface& s, const Rect& r) {
    const Stop pane[] = {{0.0f, 0x7BA2E7}, {1.0f, 0x6375D6}};
    for (int y = r.top; y < r.bottom; ++y)
        Fill(s, Rect(r.left, y, r.right, y + 1), Ramp(pane, static_cast<float>(y - r.top) / std::max(1, r.Height() - 1)));
}

void DrawTaskGroup(Surface& s, const Rect& r, int headerH, bool special) {
    Rect head(r.left, r.top, r.right, r.top + headerH);
    for (int y = head.top; y < head.bottom; ++y)
        for (int x = head.left; x < head.right; ++x) {
            float t = static_cast<float>(x - head.left) / std::max(1, head.Width() - 1);
            uint32_t c = special ? Mix(0x0055E5, 0x2D74E9, t) : Mix(0xFFFFFF, 0xC6D3F7, t);
            Blend(s, x, y, c, RoundCover(head, x, y, 4, true, false));
        }
    // кнопка свёртки: круг с шевроном
    float cx = head.right - 14.0f, cy = head.top + headerH / 2.0f;
    Disc(s, cx, cy, 8.5f, special ? 0x5A8CEB : 0xB8C8EE, special ? 0x5A8CEB : 0xB8C8EE);
    Disc(s, cx, cy, 7.5f, 0xFFFFFF, special ? 0xD8E4FB : 0xEDF2FC);
    uint32_t ch = special ? 0x0055E5 : 0x3A5FCD;
    for (int i = 0; i < 2; ++i) {
        float yy = cy - 1.5f + i * 3.5f;
        Thick(s, cx - 3, yy + 1.5f, cx, yy - 1.5f, 1.6f, ch);
        Thick(s, cx, yy - 1.5f, cx + 3, yy + 1.5f, 1.6f, ch);
    }
    Rect body(r.left, r.top + headerH, r.right, r.bottom);
    Fill(s, body, special ? 0xEFF3FF : 0xD6DFF7);
    Frame(s, body, 0xFFFFFF);
}

void DrawEditField(Surface& s, const Rect& r) {
    Fill(s, r, 0xFFFFFF);
    Frame(s, r, 0x7F9DB9);
}

void DrawStatusBar(Surface& s, const Rect& r) {
    Fill(s, r, 0xECE9D8);
    Fill(s, Rect(r.left, r.top, r.right, r.top + 1), 0xD6D2C2);
    Fill(s, Rect(r.left, r.top + 1, r.right, r.top + 2), 0xFFFFFF);
}

void DrawFocusRect(Surface& s, const Rect& r) {
    for (int x = r.left; x < r.right; x += 2) { Blend(s, x, r.top, 0x000000, 0.7f); Blend(s, x, r.bottom - 1, 0x000000, 0.7f); }
    for (int y = r.top; y < r.bottom; y += 2) { Blend(s, r.left, y, 0x000000, 0.7f); Blend(s, r.right - 1, y, 0x000000, 0.7f); }
}


void DrawStartMenuFrame(Surface& s, const Rect& r, int headerH, int footerH, int splitX) {
    const Stop header[] = {{0.0f, 0x4F9AF2}, {0.12f, 0x1E73E6}, {0.6f, 0x1962D9}, {1.0f, 0x1A5ACF}};
    const Stop footer[] = {{0.0f, 0x4B8FE6}, {0.1f, 0x3A7FDD}, {1.0f, 0x1C58C4}};
    // тень справа и снизу
    for (int i = 1; i <= 4; ++i) {
        float a = 0.22f / i;
        for (int y = r.top + 8; y < r.bottom; ++y) Blend(s, r.right + i - 1, y, 0x000000, a);
        for (int x = r.left + 4; x < r.right + i; ++x) Blend(s, x, r.bottom + i - 1, 0x000000, a);
    }
    // шапка
    RoundRamp(s, Rect(r.left, r.top, r.right, r.top + headerH), 7, header, true, false);
    // оранжевая полоска под шапкой, ярче к середине
    for (int x = r.left; x < r.right; ++x) {
        float t = 1.0f - std::fabs((x - r.left) / static_cast<float>(r.Width()) - 0.5f) * 2;
        Fill(s, Rect(x, r.top + headerH, x + 1, r.top + headerH + 2), Mix(0x2F6FD8, 0xF2A14A, t * 1.4f));
    }
    const int bodyTop = r.top + headerH + 2, bodyBottom = r.bottom - footerH;
    Fill(s, Rect(r.left, bodyTop, splitX, bodyBottom), 0xFFFFFF);
    Fill(s, Rect(splitX, bodyTop, r.right, bodyBottom), 0xD3E5FA);
    Fill(s, Rect(splitX, bodyTop, splitX + 1, bodyBottom), 0x95BDEE);
    // низ
    for (int y = bodyBottom; y < r.bottom; ++y)
        Fill(s, Rect(r.left, y, r.right, y + 1), Ramp(footer, static_cast<float>(y - bodyBottom) / std::max(1, footerH - 1)));
    // внешняя рамка (кроме скруглённой шапки)
    Fill(s, Rect(r.left, r.top + headerH, r.left + 1, r.bottom), 0x1C5AD0);
    Fill(s, Rect(r.right - 1, r.top + headerH, r.right, r.bottom), 0x1C5AD0);
    Fill(s, Rect(r.left, r.bottom - 1, r.right, r.bottom), 0x123E9E);
}

void DrawUserPicture(Surface& s, int x, int y, int size) {
    Rect frame(x, y, x + size, y + size);
    const Stop rim[] = {{0.0f, 0xFFFFFF}, {1.0f, 0xD8E6FA}};
    RoundRamp(s, frame, 4, rim);
    Rect in(x + 3, y + 3, x + size - 3, y + size - 3);
    // маленький пейзаж: небо, солнце, холм
    for (int yy = in.top; yy < in.bottom; ++yy)
        for (int xx = in.left; xx < in.right; ++xx) {
            float fx = (xx - in.left) / static_cast<float>(in.Width()), fy = (yy - in.top) / static_cast<float>(in.Height());
            uint32_t c = Mix(0x2F73DB, 0x9CCBF5, fy * 1.4f);
            if (std::hypot(fx - 0.72f, fy - 0.3f) < 0.13f) c = 0xFFE78A;
            float hill = 0.62f - 0.12f * std::sin(fx * 3.2f + 0.4f);
            if (fy > hill) c = Mix(0x7CC242, 0x2E7A16, (fy - hill) * 2.5f);
            s.Set(xx, yy, c);
        }
}

void DrawPowerGlyph(Surface& s, int x, int y, int size, bool hot) {
    Rect r(x, y, x + size, y + size);
    const Stop body[] = {{0.0f, 0xF59A6B}, {0.5f, 0xE0542C}, {1.0f, 0xC0391B}};
    const Stop bodyHot[] = {{0.0f, 0xFFB78E}, {0.5f, 0xEE6A3F}, {1.0f, 0xD0472A}};
    const Stop rim[] = {{0.0f, 0xFFFFFF}, {1.0f, 0xF2D3C6}};
    RoundRamp(s, r, 4, rim);
    if (hot) RoundRamp(s, Rect(x + 2, y + 2, x + size - 2, y + size - 2), 3, bodyHot);
    else RoundRamp(s, Rect(x + 2, y + 2, x + size - 2, y + size - 2), 3, body);
    const float cx = x + size / 2.0f, cy = y + size / 2.0f + 1, rad = size * 0.27f, w = std::max(1.6f, size * 0.09f);
    // кольцо с разрывом сверху и вертикальная черта
    for (int yy = y; yy < y + size; ++yy)
        for (int xx = x; xx < x + size; ++xx) {
            float dx = xx + 0.5f - cx, dy = yy + 0.5f - cy;
            float d = std::fabs(std::hypot(dx, dy) - rad);
            float ang = std::atan2(dx, -dy);
            if (std::fabs(ang) < 0.55f) continue;
            Blend(s, xx, yy, 0xFFFFFF, std::min(1.0f, std::max(0.0f, w / 2 - d + 0.5f)));
        }
    Thick(s, cx, cy - rad - 1, cx, cy - 1, w, 0xFFFFFF);
}

void DrawAllProgramsArrow(Surface& s, int x, int y, int size) {
    const float cx = x + size / 2.0f, cy = y + size / 2.0f;
    Disc(s, cx, cy, size / 2.0f, 0x2A8A2A, 0x2A8A2A);
    Disc(s, cx, cy, size / 2.0f - 1, 0x8BDA6E, 0x2F9E2F);
    const float k = size / 20.0f;
    Thick(s, cx - 4 * k, cy, cx + 4 * k, cy, 2.6f * k, 0xFFFFFF);
    Thick(s, cx + 4 * k, cy, cx, cy - 4 * k, 2.6f * k, 0xFFFFFF);
    Thick(s, cx + 4 * k, cy, cx, cy + 4 * k, 2.6f * k, 0xFFFFFF);
}

void DrawMenuSeparator(Surface& s, int left, int right, int y) {
    for (int x = left; x < right; ++x) {
        float t = 1.0f - std::fabs((x - left) / static_cast<float>(std::max(1, right - left)) - 0.5f) * 2;
        Blend(s, x, y, 0x9DB9E6, std::min(1.0f, t * 1.6f));
    }
}

} // namespace xp
} // namespace ui
