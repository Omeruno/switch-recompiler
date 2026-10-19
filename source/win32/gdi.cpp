#include <vector>
#include "gdi.h"
#include <algorithm>
#include <cmath>

namespace gdi {

// ROP2 как таблица истинности: (mode-1) — 4 бита, бит номер (P*2 + D)
// задаёт результат для пары "бит пера P, бит приёмника D". Проверка:
// R2_COPYPEN=13 -> 0b1100 (=P), R2_NOT=6 -> 0b0101 (=~D), R2_XORPEN=7 ->
// 0b0110 (=P^D), R2_NOP=11 -> 0b1010 (=D). Работает над RGB-битами, альфа
// всегда 0xFF.
static inline uint32_t ApplyRop2(int mode, uint32_t penArgb, uint32_t dstArgb) {
    if (mode == 13) return penArgb; // быстрый путь R2_COPYPEN
    if (mode < 1 || mode > 16) return penArgb;
    uint32_t t = static_cast<uint32_t>(mode - 1);
    uint32_t P = penArgb & 0xFFFFFFu, D = dstArgb & 0xFFFFFFu, out = 0;
    if (t & 1) out |= ~P & ~D;
    if (t & 2) out |= ~P &  D;
    if (t & 4) out |=  P & ~D;
    if (t & 8) out |=  P &  D;
    return 0xFF000000u | (out & 0xFFFFFFu);
}

GdiSystem g_gdi;

GdiObject* GdiSystem::GetObject(uint32_t handle) {
    auto it = objects_.find(handle);
    if (it == objects_.end()) return nullptr;
    return &it->second;
}

Bitmap* GdiSystem::GetBitmapForDCMutable(uint32_t hdc) {
    auto it = dcs_.find(hdc);
    if (it == dcs_.end()) return nullptr;
    if (it->second.selectedBitmap == 0) return nullptr;
    GdiObject* obj = GetObject(it->second.selectedBitmap);
    if (!obj || obj->kind != ObjectKind::Bitmap) return nullptr;
    return &obj->bitmap;
}

Bitmap* GdiSystem::GetBitmapObjectMutable(uint32_t hBitmap) {
    GdiObject* obj = GetObject(hBitmap);
    if (!obj || obj->kind != ObjectKind::Bitmap) return nullptr;
    return &obj->bitmap;
}

const Bitmap* GdiSystem::GetBitmapForDC(uint32_t hdc) const {
    auto it = dcs_.find(hdc);
    if (it == dcs_.end()) return nullptr;
    if (it->second.selectedBitmap == 0) return nullptr;
    auto objIt = objects_.find(it->second.selectedBitmap);
    if (objIt == objects_.end() || objIt->second.kind != ObjectKind::Bitmap) return nullptr;
    return &objIt->second.bitmap;
}

bool GdiSystem::IsScreenDC(uint32_t hdc) const {
    auto it = dcs_.find(hdc);
    return it != dcs_.end() && it->second.isScreenDC;
}

void GdiSystem::FillScreenDC(uint32_t argbColor) {
    if (screenDcHandle_ == 0) return;
    Bitmap* bmp = GetBitmapForDCMutable(screenDcHandle_);
    if (bmp) {
        std::fill(bmp->pixels.begin(), bmp->pixels.end(), argbColor);
        screenDirty_ = true;
    }
}

uint32_t GdiSystem::GetOrCreateScreenDC(int screenWidth, int screenHeight) {
    if (screenDcHandle_ != 0) return screenDcHandle_;

    uint32_t bitmapHandle = CreateCompatibleBitmapObj(screenWidth, screenHeight);

    uint32_t dcHandle = nextHandle_++;
    DeviceContext dc;
    dc.isScreenDC = true;
    dc.selectedBitmap = bitmapHandle;
    dcs_[dcHandle] = dc;

    screenDcHandle_ = dcHandle;
    return dcHandle;
}

uint32_t GdiSystem::CreateCompatibleDC() {
    uint32_t handle = nextHandle_++;
    dcs_[handle] = DeviceContext{};
    return handle;
}

void GdiSystem::DeleteDC(uint32_t hdc) {
    if (hdc == screenDcHandle_) return; // экранный DC не удаляем — он один на всё время жизни
    dcs_.erase(hdc);
}

uint32_t GdiSystem::CreateCompatibleBitmapObj(int width, int height) {
    if (width <= 0) width = 1;
    if (height <= 0) height = 1;

    uint32_t handle = nextHandle_++;
    GdiObject obj;
    obj.kind = ObjectKind::Bitmap;
    obj.bitmap.width = width;
    obj.bitmap.height = height;
    obj.bitmap.pixels.assign(static_cast<size_t>(width) * height, 0xFF000000u); // чёрный фон по умолчанию
    objects_[handle] = std::move(obj);
    return handle;
}

uint32_t GdiSystem::CreatePenObj(int style, int width, uint32_t colorref) {
    uint32_t handle = nextHandle_++;
    GdiObject obj;
    obj.kind = ObjectKind::Pen;
    obj.pen.style = style;
    obj.pen.width = width;
    obj.pen.colorArgb = ColorRefToArgb(colorref);
    objects_[handle] = obj;
    return handle;
}

uint32_t GdiSystem::CreateBrushObj(uint32_t colorArgb) {
    uint32_t handle = nextHandle_++;
    GdiObject obj;
    obj.kind = ObjectKind::Brush;
    obj.brush.colorArgb = colorArgb;
    objects_[handle] = obj;
    return handle;
}

uint32_t GdiSystem::GetStockObject(int stockObjectId) {
    // Реальные значения констант Win32 (wingdi.h)
    // 0=WHITE_BRUSH,1=LTGRAY_BRUSH,2=GRAY_BRUSH,3=DKGRAY_BRUSH,4=BLACK_BRUSH,5=NULL_BRUSH
    // 6=WHITE_PEN,7=BLACK_PEN,8=NULL_PEN
    static const std::unordered_map<int, uint32_t> kBrushColors = {
        {0, 0x00FFFFFFu}, {1, 0x00C0C0C0u}, {2, 0x00808080u}, {3, 0x00404040u}, {4, 0x00000000u},
    };
    static const std::unordered_map<int, uint32_t> kPenColors = {
        {6, 0x00FFFFFFu}, {7, 0x00000000u},
    };

    // Кэшируем — повторный запрос того же stock-объекта должен вернуть ТОТ ЖЕ хендл
    static std::unordered_map<int, uint32_t> cache;
    auto cacheIt = cache.find(stockObjectId);
    if (cacheIt != cache.end()) return cacheIt->second;

    uint32_t handle = nextHandle_++;
    GdiObject obj;
    if (auto it = kBrushColors.find(stockObjectId); it != kBrushColors.end()) {
        obj.kind = ObjectKind::Brush;
        obj.brush.colorArgb = ColorRefToArgb(it->second);
    } else if (auto it2 = kPenColors.find(stockObjectId); it2 != kPenColors.end()) {
        obj.kind = ObjectKind::Pen;
        obj.pen.colorArgb = ColorRefToArgb(it2->second);
        obj.pen.width = 1;
    } else if (stockObjectId == 8) {
        // NULL_PEN — это ПЕРО (исправлено: раньше создавалось как кисть, и
        // SelectObject(NULL_PEN) подменял выбранную КИСТЬ, оставляя старое
        // перо: фигура "с заливкой без контура" рисовалась наоборот).
        obj.kind = ObjectKind::Pen;
        obj.pen.style = 5; // PS_NULL
        obj.pen.width = 0;
        obj.pen.colorArgb = 0;
    } else {
        // NULL_BRUSH(5) и всё неизвестное — прозрачная кисть
        obj.kind = ObjectKind::Brush;
        obj.brush.colorArgb = 0; // alpha=0 — "не рисовать"
    }
    objects_[handle] = obj;
    cache[stockObjectId] = handle;
    return handle;
}

void GdiSystem::DeleteObject(uint32_t hObject) {
    objects_.erase(hObject);
}

uint32_t GdiSystem::SelectObject(uint32_t hdc, uint32_t hObject) {
    auto dcIt = dcs_.find(hdc);
    if (dcIt == dcs_.end()) return 0;

    GdiObject* obj = GetObject(hObject);
    if (!obj) return 0;

    uint32_t previous = 0;
    switch (obj->kind) {
        case ObjectKind::Bitmap:
            previous = dcIt->second.selectedBitmap;
            dcIt->second.selectedBitmap = hObject;
            break;
        case ObjectKind::Pen:
            previous = dcIt->second.selectedPen;
            dcIt->second.selectedPen = hObject;
            break;
        case ObjectKind::Brush:
            previous = dcIt->second.selectedBrush;
            dcIt->second.selectedBrush = hObject;
            break;
    }
    return previous;
}

uint32_t GdiSystem::SetPixel(uint32_t hdc, int x, int y, uint32_t colorref) {
    Bitmap* bmp = GetBitmapForDCMutable(hdc);
    if (!bmp || !bmp->InBounds(x, y)) return 0xFFFFFFFFu; // CLR_INVALID по контракту

    size_t idx = static_cast<size_t>(y) * bmp->width + x;
    uint32_t oldArgb = bmp->pixels[idx];
    bmp->pixels[idx] = ColorRefToArgb(colorref);
    if (hdc == screenDcHandle_) screenDirty_ = true;
    return ArgbToColorRef(oldArgb);
}

// GetPixel(HDC, x, y) — эта сессия, для честной Пипетки. Зеркалит
// SetPixel: то же разрешение DC->Bitmap, но только читает, ничего не
// меняет. Контракт настоящего Win32 GetPixel — тоже CLR_INVALID
// (0xFFFFFFFF) при промахе мимо границ/невалидном DC.
uint32_t GdiSystem::GetPixel(uint32_t hdc, int x, int y) const {
    const Bitmap* bmp = GetBitmapForDC(hdc);
    if (!bmp || !bmp->InBounds(x, y)) return 0xFFFFFFFFu; // CLR_INVALID
    size_t idx = static_cast<size_t>(y) * bmp->width + x;
    return ArgbToColorRef(bmp->pixels[idx]);
}

void GdiSystem::MoveTo(uint32_t hdc, int x, int y) {
    auto it = dcs_.find(hdc);
    if (it == dcs_.end()) return;
    it->second.currentX = x;
    it->second.currentY = y;
}

bool GdiSystem::GetCurrentPos(uint32_t hdc, int& x, int& y) const {
    auto it = dcs_.find(hdc);
    if (it == dcs_.end()) return false;
    x = it->second.currentX;
    y = it->second.currentY;
    return true;
}

bool GdiSystem::ConsumeScreenDirtyFlag() {
    bool wasDirty = screenDirty_;
    screenDirty_ = false;
    return wasDirty;
}

void GdiSystem::LineTo(uint32_t hdc, int x, int y) {
    auto dcIt = dcs_.find(hdc);
    if (dcIt == dcs_.end()) return;
    Bitmap* bmp = GetBitmapForDCMutable(hdc);
    if (!bmp) { dcIt->second.currentX = x; dcIt->second.currentY = y; return; }

    uint32_t colorArgb = 0xFF000000u; // чёрный по умолчанию, если перо не выбрано
    if (dcIt->second.selectedPen != 0) {
        GdiObject* penObj = GetObject(dcIt->second.selectedPen);
        if (penObj && penObj->kind == ObjectKind::Pen) {
            if (penObj->pen.style == 5 /*PS_NULL*/ || (penObj->pen.colorArgb >> 24) == 0) {
                dcIt->second.currentX = x; dcIt->second.currentY = y; // перо "невидимое": только сдвиг позиции
                return;
            }
            colorArgb = penObj->pen.colorArgb;
        }
    }

    // Алгоритм Брезенхэма — рисуем от текущей позиции DC до (x,y)
    int x0 = dcIt->second.currentX, y0 = dcIt->second.currentY;
    int x1 = x, y1 = y;

    int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    // Семантика Win32: LineTo НЕ рисует конечную точку (она станет началом
    // следующего отрезка). Это важно для ROP2-инверсии: иначе стыки
    // отрезков рисуются дважды и гасят сами себя.
    const int rop = dcIt->second.rop2;
    int cx = x0, cy = y0;
    while (true) {
        if (cx == x1 && cy == y1) break;
        if (bmp->InBounds(cx, cy)) {
            uint32_t& px = bmp->pixels[static_cast<size_t>(cy) * bmp->width + cx];
            px = ApplyRop2(rop, colorArgb, px);
        }
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; cx += sx; }
        if (e2 <= dx) { err += dx; cy += sy; }
    }

    dcIt->second.currentX = x1;
    dcIt->second.currentY = y1;
    if (hdc == screenDcHandle_) screenDirty_ = true;
}

void GdiSystem::BitBlt(uint32_t hdcDst, int xDst, int yDst, int w, int h,
                        uint32_t hdcSrc, int xSrc, int ySrc) {
    Bitmap* dst = GetBitmapForDCMutable(hdcDst);
    const Bitmap* src = GetBitmapForDC(hdcSrc);
    if (!dst || !src) return;

    for (int row = 0; row < h; row++) {
        int sy = ySrc + row, dy = yDst + row;
        if (sy < 0 || sy >= src->height || dy < 0 || dy >= dst->height) continue;
        for (int col = 0; col < w; col++) {
            int sx = xSrc + col, dx = xDst + col;
            if (sx < 0 || sx >= src->width || dx < 0 || dx >= dst->width) continue;
            dst->pixels[static_cast<size_t>(dy) * dst->width + dx] =
                src->pixels[static_cast<size_t>(sy) * src->width + sx];
        }
    }
    if (hdcDst == screenDcHandle_) screenDirty_ = true;
}



// ===================== Фигуры и состояние DC (эта сессия) =====================
namespace {
// Цвет выбранного пера; false, если перо невидимое (NULL_PEN / PS_NULL)
bool PenColor(GdiSystem& g, const DeviceContext& dc, uint32_t& outArgb) {
    outArgb = 0xFF000000u;
    if (dc.selectedPen == 0) return true;
    GdiObject* o = g.GetObject(dc.selectedPen);
    if (!o || o->kind != ObjectKind::Pen) return true;
    if (o->pen.style == 5 || (o->pen.colorArgb >> 24) == 0) return false;
    outArgb = o->pen.colorArgb;
    return true;
}
// Цвет кисти; false, если кисть прозрачная (NULL_BRUSH)
bool BrushColor(GdiSystem& g, uint32_t hBrush, uint32_t& outArgb) {
    outArgb = 0xFFFFFFFFu;
    if (hBrush == 0) return true; // Win32: DC по умолчанию имеет WHITE_BRUSH
    GdiObject* o = g.GetObject(hBrush);
    if (!o || o->kind != ObjectKind::Brush) return true;
    if ((o->brush.colorArgb >> 24) == 0) return false;
    outArgb = o->brush.colorArgb;
    return true;
}
void Plot(Bitmap* bmp, int x, int y, uint32_t argb, int rop = 13) {
    if (!bmp->InBounds(x, y)) return;
    uint32_t& px = bmp->pixels[static_cast<size_t>(y) * bmp->width + x];
    px = ApplyRop2(rop, argb, px);
}
} // namespace

void GdiSystem::FillRectWithBrush(uint32_t hdc, int l, int t, int r, int b, uint32_t hBrush) {
    Bitmap* bmp = GetBitmapForDCMutable(hdc);
    if (!bmp) return;
    uint32_t argb;
    if (!BrushColor(*this, hBrush, argb)) return;
    // FillRect в Win32 НЕ зависит от ROP2 (это PatBlt с PATCOPY)
    for (int y = t; y < b; y++) for (int x = l; x < r; x++) Plot(bmp, x, y, argb);
    if (hdc == screenDcHandle_) screenDirty_ = true;
}

void GdiSystem::Polyline(uint32_t hdc, const GdiPoint* pts, int count) {
    if (count < 2) return;
    auto dcIt = dcs_.find(hdc);
    if (dcIt == dcs_.end()) return;
    int savedX = dcIt->second.currentX, savedY = dcIt->second.currentY; // Polyline не меняет текущую позицию
    MoveTo(hdc, pts[0].x, pts[0].y);
    for (int i = 1; i < count; i++) LineTo(hdc, pts[i].x, pts[i].y);
    dcIt->second.currentX = savedX; dcIt->second.currentY = savedY;
}

void GdiSystem::Polygon(uint32_t hdc, const GdiPoint* pts, int count) {
    if (count < 2) return;
    auto dcIt = dcs_.find(hdc);
    if (dcIt == dcs_.end()) return;
    Bitmap* bmp = GetBitmapForDCMutable(hdc);
    if (!bmp) return;
    uint32_t fill;
    if (BrushColor(*this, dcIt->second.selectedBrush, fill)) {
        // Заливка по сканлиниям, правило чётности (ALTERNATE, умолчание Win32),
        // пересечения берутся в центрах пикселей.
        int minY = pts[0].y, maxY = pts[0].y;
        for (int i = 1; i < count; i++) { minY = std::min(minY, pts[i].y); maxY = std::max(maxY, pts[i].y); }
        std::vector<int> xs;
        for (int y = minY; y < maxY; y++) {
            xs.clear();
            double cy = y + 0.5;
            for (int i = 0; i < count; i++) {
                const GdiPoint& a = pts[i];
                const GdiPoint& c = pts[(i + 1) % count];
                if ((a.y <= cy && c.y > cy) || (c.y <= cy && a.y > cy)) {
                    double x = a.x + (cy - a.y) * (c.x - a.x) / static_cast<double>(c.y - a.y);
                    xs.push_back(static_cast<int>(std::lround(x)));
                }
            }
            std::sort(xs.begin(), xs.end());
            for (size_t k = 0; k + 1 < xs.size(); k += 2)
                for (int x = xs[k]; x < xs[k + 1]; x++) Plot(bmp, x, y, fill, dcIt->second.rop2);
        }
    }
    uint32_t pen;
    if (PenColor(*this, dcIt->second, pen)) {
        int savedX = dcIt->second.currentX, savedY = dcIt->second.currentY;
        MoveTo(hdc, pts[count - 1].x, pts[count - 1].y);
        for (int i = 0; i < count; i++) LineTo(hdc, pts[i].x, pts[i].y);
        dcIt->second.currentX = savedX; dcIt->second.currentY = savedY;
    }
    if (hdc == screenDcHandle_) screenDirty_ = true;
}

void GdiSystem::Rectangle(uint32_t hdc, int l, int t, int r, int b) {
    if (r < l) std::swap(l, r);
    if (b < t) std::swap(t, b);
    auto dcIt = dcs_.find(hdc);
    if (dcIt == dcs_.end()) return;
    Bitmap* bmp = GetBitmapForDCMutable(hdc);
    if (!bmp) return;
    uint32_t pen; bool hasPen = PenColor(*this, dcIt->second, pen);
    const int rop = dcIt->second.rop2;
    uint32_t fill;
    if (BrushColor(*this, dcIt->second.selectedBrush, fill)) {
        int inset = hasPen ? 1 : 0;
        for (int y = t + inset; y < b - inset; y++)
            for (int x = l + inset; x < r - inset; x++) Plot(bmp, x, y, fill, rop);
    }
    if (hasPen && r > l && b > t) {
        // каждый пиксель контура ровно один раз (важно для ROP2-инверсии)
        for (int y = t; y < b; y++)
            for (int x = l; x < r; x++)
                if (y == t || y == b - 1 || x == l || x == r - 1) Plot(bmp, x, y, pen, rop);
    }
    if (hdc == screenDcHandle_) screenDirty_ = true;
}

void GdiSystem::Ellipse(uint32_t hdc, int l, int t, int r, int b) {
    if (r < l) std::swap(l, r);
    if (b < t) std::swap(t, b);
    auto dcIt = dcs_.find(hdc);
    if (dcIt == dcs_.end()) return;
    Bitmap* bmp = GetBitmapForDCMutable(hdc);
    if (!bmp || r - l < 1 || b - t < 1) return;
    uint32_t pen; bool hasPen = PenColor(*this, dcIt->second, pen);
    uint32_t fill; bool hasFill = BrushColor(*this, dcIt->second.selectedBrush, fill);
    // Эллипс, вписанный в [l, r) x [t, b): центр и полуоси в координатах центров пикселей
    double cx = (l + r - 1) / 2.0, cy = (t + b - 1) / 2.0;
    double rx = (r - l) / 2.0, ry = (b - t) / 2.0;
    auto inside = [&](int x, int y) {
        double dx = (x - cx) / rx, dy = (y - cy) / ry;
        return dx * dx + dy * dy <= 1.0;
    };
    for (int y = t; y < b; y++) {
        for (int x = l; x < r; x++) {
            if (!inside(x, y)) continue;
            bool edge = !inside(x - 1, y) || !inside(x + 1, y) || !inside(x, y - 1) || !inside(x, y + 1);
            if (edge && hasPen) Plot(bmp, x, y, pen, dcIt->second.rop2);
            else if (hasFill) Plot(bmp, x, y, fill, dcIt->second.rop2);
        }
    }
    if (hdc == screenDcHandle_) screenDirty_ = true;
}

uint32_t GdiSystem::SetTextColor(uint32_t hdc, uint32_t c) {
    auto it = dcs_.find(hdc); if (it == dcs_.end()) return 0xFFFFFFFFu; // CLR_INVALID
    uint32_t old = it->second.textColorRef; it->second.textColorRef = c & 0x00FFFFFFu; return old;
}
uint32_t GdiSystem::SetBkColor(uint32_t hdc, uint32_t c) {
    auto it = dcs_.find(hdc); if (it == dcs_.end()) return 0xFFFFFFFFu;
    uint32_t old = it->second.bkColorRef; it->second.bkColorRef = c & 0x00FFFFFFu; return old;
}
int GdiSystem::SetBkMode(uint32_t hdc, int mode) {
    auto it = dcs_.find(hdc); if (it == dcs_.end()) return 0;
    int old = it->second.bkMode; it->second.bkMode = mode; return old;
}
uint32_t GdiSystem::GetTextColor(uint32_t hdc) const {
    auto it = dcs_.find(hdc); return it == dcs_.end() ? 0xFFFFFFFFu : it->second.textColorRef;
}
uint32_t GdiSystem::GetBkColor(uint32_t hdc) const {
    auto it = dcs_.find(hdc); return it == dcs_.end() ? 0xFFFFFFFFu : it->second.bkColorRef;
}
int GdiSystem::GetBkMode(uint32_t hdc) const {
    auto it = dcs_.find(hdc); return it == dcs_.end() ? 0 : it->second.bkMode;
}
int GdiSystem::SetROP2(uint32_t hdc, int mode) {
    auto it = dcs_.find(hdc); if (it == dcs_.end()) return 0;
    int old = it->second.rop2; if (mode >= 1 && mode <= 16) it->second.rop2 = mode; return old;
}
int GdiSystem::GetROP2(uint32_t hdc) const {
    auto it = dcs_.find(hdc); return it == dcs_.end() ? 0 : it->second.rop2;
}

} // namespace gdi
