// xp_icons.cpp — векторные значки в духе Windows XP.
//
// Каждый значок описан фигурами в единичных координатах (0..1): многоугольники,
// скруглённые прямоугольники, эллипсы с градиентной заливкой и контуром.
// Растеризация — 4x4 подвыборки на пиксель; результат кэшируется по размеру.

#include "xp_icons.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <vector>

namespace ui {
namespace xp {
namespace {

struct P { float x, y; };

// Холст значка: премультиплицированный RGBA во float
struct Canvas {
    int n;
    std::vector<float> r, g, b, a;
    explicit Canvas(int size) : n(size), r(size * size), g(size * size), b(size * size), a(size * size) {}

    void Blend(int i, uint32_t rgb, float cover) {
        if (cover <= 0) return;
        float cr = ((rgb >> 16) & 255) / 255.0f, cg = ((rgb >> 8) & 255) / 255.0f, cb = (rgb & 255) / 255.0f;
        r[i] = cr * cover + r[i] * (1 - cover);
        g[i] = cg * cover + g[i] * (1 - cover);
        b[i] = cb * cover + b[i] * (1 - cover);
        a[i] = cover + a[i] * (1 - cover);
    }
};

uint32_t Mix(uint32_t c0, uint32_t c1, float t) {
    t = std::min(1.0f, std::max(0.0f, t));
    uint32_t out = 0;
    for (int sh = 0; sh <= 16; sh += 8) {
        float a = static_cast<float>((c0 >> sh) & 255), b = static_cast<float>((c1 >> sh) & 255);
        out |= static_cast<uint32_t>(a + (b - a) * t + 0.5f) << sh;
    }
    return out;
}

// Заливка: цвет от точки (линейный градиент между y0 и y1, либо по x)
struct Paint {
    uint32_t c0, c1;
    float from, to;
    bool horizontal = false;
    uint32_t At(float x, float y) const {
        float v = horizontal ? x : y;
        return to == from ? c0 : Mix(c0, c1, (v - from) / (to - from));
    }
};
Paint Solid(uint32_t c) { return Paint{c, c, 0, 0}; }
Paint VGrad(uint32_t a, uint32_t b, float y0, float y1) { return Paint{a, b, y0, y1}; }
Paint HGrad(uint32_t a, uint32_t b, float x0, float x1) { Paint p{a, b, x0, x1}; p.horizontal = true; return p; }

template <typename Inside>
void FillShape(Canvas& cv, const Paint& paint, float x0, float y0, float x1, float y1, Inside inside) {
    const int n = cv.n;
    int px0 = std::max(0, static_cast<int>(std::floor(x0 * n))), px1 = std::min(n, static_cast<int>(std::ceil(x1 * n)));
    int py0 = std::max(0, static_cast<int>(std::floor(y0 * n))), py1 = std::min(n, static_cast<int>(std::ceil(y1 * n)));
    for (int py = py0; py < py1; ++py)
        for (int px = px0; px < px1; ++px) {
            int hits = 0;
            for (int sy = 0; sy < 4; ++sy)
                for (int sx = 0; sx < 4; ++sx) {
                    float ux = (px + (sx + 0.5f) / 4) / n, uy = (py + (sy + 0.5f) / 4) / n;
                    if (inside(ux, uy)) ++hits;
                }
            if (hits) cv.Blend(py * n + px, paint.At((px + 0.5f) / n, (py + 0.5f) / n), hits / 16.0f);
        }
}

bool InPoly(const std::vector<P>& pts, float x, float y) {
    bool in = false;
    for (size_t i = 0, j = pts.size() - 1; i < pts.size(); j = i++)
        if (((pts[i].y > y) != (pts[j].y > y)) &&
            (x < (pts[j].x - pts[i].x) * (y - pts[i].y) / (pts[j].y - pts[i].y) + pts[i].x))
            in = !in;
    return in;
}
float SegDist(P a, P b, float x, float y) {
    float vx = b.x - a.x, vy = b.y - a.y, wx = x - a.x, wy = y - a.y;
    float t = std::max(0.0f, std::min(1.0f, (vx * wx + vy * wy) / std::max(1e-9f, vx * vx + vy * vy)));
    return std::hypot(wx - vx * t, wy - vy * t);
}
void Bounds(const std::vector<P>& pts, float pad, float& x0, float& y0, float& x1, float& y1) {
    x0 = y0 = 1e9f; x1 = y1 = -1e9f;
    for (P p : pts) { x0 = std::min(x0, p.x); y0 = std::min(y0, p.y); x1 = std::max(x1, p.x); y1 = std::max(y1, p.y); }
    x0 -= pad; y0 -= pad; x1 += pad; y1 += pad;
}

// Минимальная толщина контура — около пикселя при любом размере
float Line(const Canvas& cv) { return std::max(1.0f / cv.n, 0.022f); }

void Poly(Canvas& cv, const std::vector<P>& pts, const Paint& fill, uint32_t outline = 0xFFFFFFFF) {
    float x0, y0, x1, y1;
    Bounds(pts, 0, x0, y0, x1, y1);
    FillShape(cv, fill, x0, y0, x1, y1, [&](float x, float y) { return InPoly(pts, x, y); });
    if (outline != 0xFFFFFFFF) {
        float w = Line(cv) / 2;
        Bounds(pts, w, x0, y0, x1, y1);
        FillShape(cv, Solid(outline), x0, y0, x1, y1, [&](float x, float y) {
            for (size_t i = 0, j = pts.size() - 1; i < pts.size(); j = i++)
                if (SegDist(pts[j], pts[i], x, y) <= w) return true;
            return false;
        });
    }
}
void Stroke(Canvas& cv, P a, P b, uint32_t color, float width = 0) {
    float w = (width > 0 ? width : Line(cv)) / 2;
    FillShape(cv, Solid(color), std::min(a.x, b.x) - w, std::min(a.y, b.y) - w, std::max(a.x, b.x) + w, std::max(a.y, b.y) + w,
              [&](float x, float y) { return SegDist(a, b, x, y) <= w; });
}
bool InRound(float x, float y, float x0, float y0, float x1, float y1, float rad) {
    if (x < x0 || x > x1 || y < y0 || y > y1) return false;
    float cx = std::max(x0 + rad, std::min(x1 - rad, x)), cy = std::max(y0 + rad, std::min(y1 - rad, y));
    return std::hypot(x - cx, y - cy) <= rad;
}
void Round(Canvas& cv, float x0, float y0, float x1, float y1, float rad, const Paint& fill, uint32_t outline = 0xFFFFFFFF) {
    if (outline != 0xFFFFFFFF) {
        float w = Line(cv);
        FillShape(cv, Solid(outline), x0, y0, x1, y1, [&](float x, float y) { return InRound(x, y, x0, y0, x1, y1, rad); });
        FillShape(cv, fill, x0 + w, y0 + w, x1 - w, y1 - w,
                  [&](float x, float y) { return InRound(x, y, x0 + w, y0 + w, x1 - w, y1 - w, std::max(0.0f, rad - w)); });
    } else {
        FillShape(cv, fill, x0, y0, x1, y1, [&](float x, float y) { return InRound(x, y, x0, y0, x1, y1, rad); });
    }
}
void Ellipse(Canvas& cv, float cx, float cy, float rx, float ry, const Paint& fill, uint32_t outline = 0xFFFFFFFF) {
    auto in = [&](float x, float y, float ax, float ay) {
        float dx = (x - cx) / ax, dy = (y - cy) / ay;
        return dx * dx + dy * dy <= 1;
    };
    if (outline != 0xFFFFFFFF) {
        float w = Line(cv);
        FillShape(cv, Solid(outline), cx - rx, cy - ry, cx + rx, cy + ry, [&](float x, float y) { return in(x, y, rx, ry); });
        FillShape(cv, fill, cx - rx, cy - ry, cx + rx, cy + ry, [&](float x, float y) { return in(x, y, rx - w, ry - w); });
    } else {
        FillShape(cv, fill, cx - rx, cy - ry, cx + rx, cy + ry, [&](float x, float y) { return in(x, y, rx, ry); });
    }
}
// Мягкая тень под значком
void Shadow(Canvas& cv, float cx, float cy, float rx, float ry, float strength = 0.28f) {
    const int n = cv.n;
    for (int py = 0; py < n; ++py)
        for (int px = 0; px < n; ++px) {
            float dx = ((px + 0.5f) / n - cx) / rx, dy = ((py + 0.5f) / n - cy) / ry;
            float d = dx * dx + dy * dy;
            if (d < 1) cv.Blend(py * n + px, 0x000000, strength * (1 - d) * (1 - d));
        }
}

// --- Значки ---

// Задняя стенка папки с язычком и передняя створка
void FolderBack(Canvas& cv) {
    Poly(cv, {{0.06f, 0.22f}, {0.38f, 0.22f}, {0.45f, 0.30f}, {0.92f, 0.30f}, {0.92f, 0.84f}, {0.06f, 0.84f}},
         VGrad(0xF3C24C, 0xD9A12E, 0.22f, 0.84f), 0xAE7E1A);
}
void FolderFront(Canvas& cv, bool open) {
    if (open)
        Poly(cv, {{0.18f, 0.44f}, {0.98f, 0.44f}, {0.88f, 0.86f}, {0.05f, 0.86f}}, VGrad(0xFFE9A3, 0xF4C24F, 0.44f, 0.86f), 0xB4851F);
    else
        Poly(cv, {{0.06f, 0.40f}, {0.92f, 0.40f}, {0.92f, 0.86f}, {0.06f, 0.86f}}, VGrad(0xFFE7A0, 0xF2BF4A, 0.40f, 0.86f), 0xB4851F);
    // блик по верхнему краю створки
    Stroke(cv, {open ? 0.20f : 0.09f, open ? 0.475f : 0.435f}, {open ? 0.94f : 0.89f, open ? 0.475f : 0.435f}, 0xFFF6D2);
}
void Page(Canvas& cv, float x0, float y0, float x1, float y1, bool lines) {
    float fold = (x1 - x0) * 0.28f;
    Poly(cv, {{x0, y0}, {x1 - fold, y0}, {x1, y0 + fold}, {x1, y1}, {x0, y1}}, VGrad(0xFFFFFF, 0xE8EEF5, y0, y1), 0x7F8C99);
    Poly(cv, {{x1 - fold, y0}, {x1, y0 + fold}, {x1 - fold, y0 + fold}}, Solid(0xD3DCE6), 0x7F8C99);
    if (lines)
        for (float y = y0 + (y1 - y0) * 0.32f; y < y1 - (y1 - y0) * 0.12f; y += (y1 - y0) * 0.13f)
            Stroke(cv, {x0 + (x1 - x0) * 0.16f, y}, {x1 - (x1 - x0) * 0.16f, y}, 0x9AA9BA);
}
void Landscape(Canvas& cv, float x0, float y0, float x1, float y1) {
    Round(cv, x0, y0, x1, y1, 0.01f, VGrad(0x4C8BE8, 0xB5D5F7, y0, y1), 0x4A5A6A);
    float w = Line(cv);
    Poly(cv, {{x0 + w, y1 - w}, {x0 + w, y0 + (y1 - y0) * 0.62f}, {x0 + (x1 - x0) * 0.45f, y0 + (y1 - y0) * 0.48f},
              {x1 - w, y0 + (y1 - y0) * 0.66f}, {x1 - w, y1 - w}},
         VGrad(0x7CC242, 0x3C8A1E, y0 + (y1 - y0) * 0.5f, y1));
    Ellipse(cv, x0 + (x1 - x0) * 0.75f, y0 + (y1 - y0) * 0.28f, (x1 - x0) * 0.09f, (y1 - y0) * 0.11f, Solid(0xFFF2A0));
}
void AppWindow(Canvas& cv, float x0, float y0, float x1, float y1) {
    Round(cv, x0, y0, x1, y1, 0.03f, Solid(0xF5F7FA), 0x24429B);
    float bar = y0 + (y1 - y0) * 0.24f;
    Round(cv, x0, y0, x1, bar, 0.03f, VGrad(0x4F8FF5, 0x1A55D0, y0, bar));
    Ellipse(cv, x1 - (x1 - x0) * 0.12f, (y0 + bar) / 2, (x1 - x0) * 0.06f, (bar - y0) * 0.28f, Solid(0xE5553B));
    Stroke(cv, {x0 + (x1 - x0) * 0.15f, y0 + (y1 - y0) * 0.45f}, {x1 - (x1 - x0) * 0.25f, y0 + (y1 - y0) * 0.45f}, 0xA3B3C6);
    Stroke(cv, {x0 + (x1 - x0) * 0.15f, y0 + (y1 - y0) * 0.65f}, {x1 - (x1 - x0) * 0.4f, y0 + (y1 - y0) * 0.65f}, 0xA3B3C6);
}

void DrawFolder(Canvas& cv, bool open) {
    Shadow(cv, 0.52f, 0.88f, 0.44f, 0.06f);
    FolderBack(cv);
    FolderFront(cv, open);
}

void DrawComputer(Canvas& cv) {
    Shadow(cv, 0.5f, 0.92f, 0.42f, 0.05f);
    // монитор
    Round(cv, 0.10f, 0.08f, 0.90f, 0.68f, 0.05f, VGrad(0xF4F1E6, 0xCFC9B6, 0.08f, 0.68f), 0x7D776A);
    Round(cv, 0.17f, 0.14f, 0.83f, 0.58f, 0.02f, VGrad(0x6FA9F2, 0x1B4FB5, 0.14f, 0.58f), 0x3B4D6A);
    Poly(cv, {{0.19f, 0.16f}, {0.62f, 0.16f}, {0.19f, 0.42f}}, Solid(0x9FC6F7));  // блик на экране
    Ellipse(cv, 0.78f, 0.63f, 0.018f, 0.018f, Solid(0x3FBF3F));
    // ножка и подставка
    Poly(cv, {{0.42f, 0.68f}, {0.58f, 0.68f}, {0.62f, 0.78f}, {0.38f, 0.78f}}, VGrad(0xD8D2C0, 0xB5AE9A, 0.68f, 0.78f), 0x7D776A);
    Round(cv, 0.24f, 0.78f, 0.76f, 0.88f, 0.03f, VGrad(0xEAE5D6, 0xBDB6A2, 0.78f, 0.88f), 0x7D776A);
}

void DrawHardDrive(Canvas& cv) {
    Shadow(cv, 0.5f, 0.82f, 0.44f, 0.06f);
    // корпус в перспективе: верхняя грань и передняя панель
    Poly(cv, {{0.14f, 0.36f}, {0.30f, 0.22f}, {0.92f, 0.22f}, {0.80f, 0.36f}}, VGrad(0xF2F2F2, 0xC9CDD3, 0.22f, 0.36f), 0x6F757D);
    Poly(cv, {{0.80f, 0.36f}, {0.92f, 0.22f}, {0.92f, 0.62f}, {0.80f, 0.76f}}, HGrad(0xA3A9B1, 0x80868F, 0.8f, 0.92f), 0x6F757D);
    Poly(cv, {{0.06f, 0.36f}, {0.80f, 0.36f}, {0.80f, 0.76f}, {0.06f, 0.76f}}, VGrad(0xE3E6EA, 0xA7ADB5, 0.36f, 0.76f), 0x6F757D);
    Stroke(cv, {0.12f, 0.62f}, {0.62f, 0.62f}, 0x8A9098);
    Round(cv, 0.66f, 0.58f, 0.74f, 0.66f, 0.02f, VGrad(0x8DF28D, 0x1E9E1E, 0.58f, 0.66f), 0x2A6B2A);
}

void DrawSdCard(Canvas& cv) {
    Shadow(cv, 0.5f, 0.9f, 0.34f, 0.05f);
    Poly(cv, {{0.24f, 0.10f}, {0.64f, 0.10f}, {0.78f, 0.24f}, {0.78f, 0.88f}, {0.24f, 0.88f}},
         VGrad(0x3E5C8E, 0x1E3561, 0.10f, 0.88f), 0x0F1D38);
    for (int i = 0; i < 4; ++i) {
        float x = 0.30f + i * 0.085f;
        Round(cv, x, 0.14f, x + 0.05f, 0.27f, 0.008f, VGrad(0xF8DA7A, 0xC9971E, 0.14f, 0.27f));
    }
    Round(cv, 0.30f, 0.40f, 0.72f, 0.80f, 0.02f, VGrad(0xFFFFFF, 0xD6E0EC, 0.40f, 0.80f), 0x0F1D38);
    Stroke(cv, {0.36f, 0.52f}, {0.66f, 0.52f}, 0x5D7BA8, 0.04f);
    Stroke(cv, {0.36f, 0.64f}, {0.58f, 0.64f}, 0x9DB0CA, 0.03f);
}

void DrawCd(Canvas& cv) {
    Shadow(cv, 0.5f, 0.88f, 0.4f, 0.06f);
    Ellipse(cv, 0.5f, 0.48f, 0.42f, 0.42f, VGrad(0xF1F4F8, 0xB9C3CF, 0.06f, 0.9f), 0x7C8794);
    // радужные кольца
    Ellipse(cv, 0.5f, 0.48f, 0.34f, 0.34f, HGrad(0xD9C6F2, 0xBDE7D9, 0.16f, 0.84f));
    Ellipse(cv, 0.5f, 0.48f, 0.26f, 0.26f, HGrad(0xF2E6C0, 0xC5D9F5, 0.24f, 0.76f));
    Ellipse(cv, 0.5f, 0.48f, 0.13f, 0.13f, Solid(0xE6EAF0), 0x7C8794);
    Ellipse(cv, 0.5f, 0.48f, 0.055f, 0.055f, Solid(0xFFFFFF), 0x7C8794);
    Poly(cv, {{0.30f, 0.16f}, {0.44f, 0.10f}, {0.50f, 0.36f}}, Solid(0xFFFFFF));  // блик
}

void DrawRecycleBin(Canvas& cv) {
    Shadow(cv, 0.5f, 0.92f, 0.32f, 0.05f);
    Poly(cv, {{0.22f, 0.22f}, {0.78f, 0.22f}, {0.70f, 0.90f}, {0.30f, 0.90f}}, VGrad(0xE9F1F6, 0xB2C3CE, 0.22f, 0.9f), 0x5D7383);
    for (float x : {0.38f, 0.5f, 0.62f}) Stroke(cv, {x, 0.32f}, {x - (x - 0.5f) * 0.25f, 0.82f}, 0x8EA4B4);
    Round(cv, 0.16f, 0.14f, 0.84f, 0.24f, 0.03f, VGrad(0xF4F8FB, 0xA8BAC6, 0.14f, 0.24f), 0x5D7383);
    // зелёная стрелка переработки
    Ellipse(cv, 0.5f, 0.56f, 0.13f, 0.13f, Solid(0x3EA93E), 0x1E6B1E);
    Ellipse(cv, 0.5f, 0.56f, 0.06f, 0.06f, Solid(0xD9EEF4));
}

void DrawShortcut(Canvas& cv) {
    Round(cv, 0.0f, 0.62f, 0.38f, 1.0f, 0.03f, Solid(0xFFFFFF), 0x4A4A4A);
    Poly(cv, {{0.08f, 0.92f}, {0.08f, 0.78f}, {0.20f, 0.74f}, {0.18f, 0.68f}, {0.32f, 0.76f}, {0.22f, 0.86f}, {0.21f, 0.80f}, {0.13f, 0.83f}, {0.13f, 0.92f}},
         Solid(0x1747B7));
}

void Render(Canvas& cv, IconKind kind) {
    switch (kind) {
    case IconKind::Folder: DrawFolder(cv, false); break;
    case IconKind::FolderOpen: DrawFolder(cv, true); break;
    case IconKind::Documents:
        Shadow(cv, 0.52f, 0.88f, 0.44f, 0.06f);
        FolderBack(cv);
        Page(cv, 0.24f, 0.12f, 0.70f, 0.62f, true);
        FolderFront(cv, false);
        break;
    case IconKind::Pictures:
        Shadow(cv, 0.52f, 0.88f, 0.44f, 0.06f);
        FolderBack(cv);
        Landscape(cv, 0.22f, 0.14f, 0.76f, 0.56f);
        FolderFront(cv, false);
        break;
    case IconKind::Programs:
        Shadow(cv, 0.52f, 0.88f, 0.44f, 0.06f);
        FolderBack(cv);
        AppWindow(cv, 0.22f, 0.14f, 0.76f, 0.56f);
        FolderFront(cv, false);
        break;
    case IconKind::Computer: DrawComputer(cv); break;
    case IconKind::HardDrive: DrawHardDrive(cv); break;
    case IconKind::SdCard: DrawSdCard(cv); break;
    case IconKind::CdDrive: DrawCd(cv); break;
    case IconKind::RecycleBin: DrawRecycleBin(cv); break;
    case IconKind::Application:
        Shadow(cv, 0.5f, 0.9f, 0.4f, 0.05f);
        AppWindow(cv, 0.10f, 0.16f, 0.90f, 0.84f);
        break;
    case IconKind::TextFile:
        Shadow(cv, 0.5f, 0.92f, 0.32f, 0.05f);
        Page(cv, 0.20f, 0.06f, 0.80f, 0.92f, true);
        break;
    case IconKind::ImageFile:
        Shadow(cv, 0.5f, 0.92f, 0.32f, 0.05f);
        Page(cv, 0.20f, 0.06f, 0.80f, 0.92f, false);
        Landscape(cv, 0.28f, 0.34f, 0.72f, 0.74f);
        break;
    case IconKind::MusicFile:
        Shadow(cv,0.5f,0.92f,0.34f,0.05f);
        Page(cv,0.17f,0.06f,0.83f,0.92f,false);
        Round(cv,0.30f,0.28f,0.73f,0.74f,0.06f,VGrad(0x79C6F4,0x235BA7,0.28f,0.74f),0x174A80);
        Stroke(cv,{0.43f,0.60f},{0.43f,0.37f},0xFFFFFF,0.045f);
        Stroke(cv,{0.64f,0.55f},{0.64f,0.33f},0xFFFFFF,0.045f);
        Stroke(cv,{0.43f,0.37f},{0.64f,0.33f},0xFFFFFF,0.055f);
        Ellipse(cv,0.39f,0.61f,0.07f,0.045f,Solid(0xF5FDFF));
        Ellipse(cv,0.60f,0.56f,0.07f,0.045f,Solid(0xF5FDFF));
        Ellipse(cv,0.75f,0.79f,0.17f,0.17f,VGrad(0xB6E769,0x388921,0.62f,0.96f),0x256C20);
        Poly(cv,{{0.71f,0.70f},{0.71f,0.88f},{0.84f,0.79f}},Solid(0xFFFFFF));
        break;
    case IconKind::File:
        Shadow(cv, 0.5f, 0.92f, 0.32f, 0.05f);
        Page(cv, 0.20f, 0.06f, 0.80f, 0.92f, false);
        break;
    case IconKind::Shortcut: DrawShortcut(cv); break;
    case IconKind::Count: break;
    }
}

// Готовые значки 0xAARRGGBB по (вид, размер)
const std::vector<uint32_t>& Cached(IconKind kind, int size) {
    static std::map<std::pair<int, int>, std::vector<uint32_t>> cache;
    auto key = std::make_pair(static_cast<int>(kind), size);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    Canvas cv(size);
    Render(cv, kind);
    std::vector<uint32_t> px(static_cast<size_t>(size) * size);
    for (size_t i = 0; i < px.size(); ++i) {
        float a = cv.a[i];
        if (a <= 0) continue;
        auto ch = [&](float v) { return static_cast<uint32_t>(std::min(255.0f, v / a * 255.0f + 0.5f)); };
        px[i] = (static_cast<uint32_t>(a * 255.0f + 0.5f) << 24) | (ch(cv.r[i]) << 16) | (ch(cv.g[i]) << 8) | ch(cv.b[i]);
    }
    return cache.emplace(key, std::move(px)).first->second;
}

} // namespace

void DrawIcon(Surface& s, IconKind kind, int x, int y, int size, bool selected) {
    if (size < 8 || size > 128 || kind == IconKind::Count) return;
    const std::vector<uint32_t>& px = Cached(kind, size);
    for (int iy = 0; iy < size; ++iy)
        for (int ix = 0; ix < size; ++ix) {
            int sx = x + ix, sy = y + iy;
            if (sx < 0 || sy < 0 || sx >= s.width || sy >= s.height) continue;
            uint32_t c = px[static_cast<size_t>(iy) * size + ix];
            uint32_t alpha = c >> 24;
            if (!alpha) continue;
            uint32_t rgb = c & 0xFFFFFF;
            if (selected) rgb = Mix(rgb, 0x316AC5, 0.38f);  // синяя подсветка выделения
            s.Set(sx, sy, Mix(s.Get(sx, sy), rgb, alpha / 255.0f));
        }
}

uint32_t IconPixel(IconKind kind, int size, int x, int y) {
    if (size < 8 || size > 128 || kind == IconKind::Count || x < 0 || y < 0 || x >= size || y >= size) return 0;
    return Cached(kind, size)[static_cast<size_t>(y) * size + x];
}

} // namespace xp
} // namespace ui
