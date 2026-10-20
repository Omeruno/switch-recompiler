#pragma once
// gdi.h
//
// GDI: объекты (перья, кисти, шрифты, битмапы, регионы, палитры) и
// контексты устройств, рисование в поверхности. Не зависит от памяти
// гостя: HLE-обёртки (win32/gdi32_module.cpp) читают аргументы и
// вызывают эти функции.
//
// Контекст устройства рисует либо в выбранный в него битмап (контекст в
// памяти), либо в общий экран (контексты окон и экрана). Для контекстов
// окон видимая область (часть окна, не закрытая другими окнами) и смещение
// приходят от оконной подсистемы через WindowDcProvider.
//
// Координаты: логические -> устройства (окно/вьюпорт, режимы отображения
// MM_TEXT/ISOTROPIC/ANISOTROPIC) -> поверхность (смещение окна на экране).

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "font.h"
#include "region.h"
#include "surface.h"

namespace ui {

// Типы объектов — как OBJ_* в Windows (GetObjectType)
enum class ObjType : uint8_t {
    None = 0, Pen = 1, Brush = 2, DC = 3, Palette = 5, Font = 6, Bitmap = 7, Region = 8, MemDC = 10, ExtPen = 11,
};

struct Point { int x = 0, y = 0; };

struct PenObj {
    int style = 0;          // PS_*
    int width = 1;
    uint32_t color = 0;     // COLORREF
};

struct BrushObj {
    int style = 0;          // BS_SOLID 0, BS_NULL 1, BS_HATCHED 2, BS_PATTERN 3, BS_DIBPATTERN 5
    uint32_t color = 0;     // COLORREF
    int hatch = 0;
    uint32_t patternBitmap = 0;     // HBITMAP для BS_PATTERN (копия пикселей ниже)
    OwnedSurface pattern;           // для BS_PATTERN/BS_DIBPATTERN
    int sysColorIndex = -1;         // кисти GetSysColorBrush
};

struct BitmapObj {
    OwnedSurface owned;             // DDB и обычные DIB в памяти хоста
    Surface view;                   // то, во что рисуем (owned.s или память гостя)
    bool isDib = false;             // DIB-секция
    uint32_t guestBits = 0;         // адрес пикселей DIB-секции в памяти гостя
    std::vector<uint8_t> infoHeader; // BITMAPINFOHEADER DIB-секции (для GetObject)
    int selectedIn = 0;             // сколько контекстов держат этот битмап
};

struct RegionObj {
    Region rgn;
};

struct PaletteObj {
    std::vector<uint32_t> entries;  // PALETTEENTRY как 0xFFBBGGRR (peFlags в старшем байте)
};

struct FontObj {
    FontDesc desc;
    Font* font = nullptr;
};

struct DcState {
    uint32_t pen = 0, brush = 0, font = 0, bitmap = 0, palette = 0;
    uint32_t textColor = 0x000000, bkColor = 0xFFFFFF;
    int bkMode = 2;                 // OPAQUE
    int rop2 = 13;                  // R2_COPYPEN
    int textAlign = 0;
    int mapMode = 1;                // MM_TEXT
    Point windowOrg, viewportOrg;
    Point windowExt{1, 1}, viewportExt{1, 1};
    Point curPos;
    Point brushOrg;
    int stretchMode = 1;
    int polyFillMode = 1;           // ALTERNATE
    int charExtra = 0;
    int arcDirection = 1;           // AD_COUNTERCLOCKWISE
    bool hasClip = false;
    Region clip;                    // в координатах устройства
    uint32_t layout = 0;
};

enum class DcKind : uint8_t { Memory, Window, Screen, Info };

struct DcObj {
    DcKind kind = DcKind::Memory;
    uint32_t hwnd = 0;
    bool wholeWindow = false;       // GetWindowDC: с неклиентской областью
    bool paintDc = false;           // из BeginPaint: обрезан областью обновления
    Region paintClip;               // область BeginPaint (в координатах поверхности)
    DcState st;
    std::vector<DcState> saved;     // SaveDC/RestoreDC
};

// Связь с оконной подсистемой: где на экране контекст окна и что из него
// видно. Возвращает false, если окна уже нет.
struct WindowDcInfo {
    int originX = 0, originY = 0;   // координаты устройства (0,0) на экране
    Region visible;                 // видимая часть в координатах экрана
};
using WindowDcProvider = bool (*)(uint32_t hwnd, bool wholeWindow, WindowDcInfo& out);

class Gdi {
public:
    Gdi();

    // Экран: все окна рисуют сюда. Создаётся оконной подсистемой.
    void SetScreen(Surface* screen) { screen_ = screen; }
    Surface* Screen() { return screen_; }
    void SetWindowDcProvider(WindowDcProvider p) { provider_ = p; }
    // Вызывается при любом изменении положения/видимости окон
    void InvalidateLayout() { ++layoutGeneration_; }
    // Заметка «экран изменился» для вывода на дисплей
    uint64_t ScreenChangeCounter() const { return screenChanges_; }

    // --- объекты ---
    ObjType TypeOf(uint32_t h) const;
    PenObj* Pen(uint32_t h);
    BrushObj* Brush(uint32_t h);
    BitmapObj* Bitmap(uint32_t h);
    RegionObj* Rgn(uint32_t h);
    PaletteObj* Palette(uint32_t h);
    FontObj* FontO(uint32_t h);
    DcObj* Dc(uint32_t h);

    uint32_t CreatePen(int style, int width, uint32_t color);
    uint32_t CreateBrush(const BrushObj& b);
    uint32_t CreateSolidBrush(uint32_t color) { BrushObj b; b.color = color; return CreateBrush(b); }
    uint32_t CreateFont(const FontDesc& d);
    uint32_t CreateBitmap(int w, int h, int bpp, const void* initBits = nullptr);
    // DIB-секция над памятью гостя (bits — указатель хоста на guestBits)
    uint32_t CreateDibSection(const Surface& view, uint32_t guestBits, const std::vector<uint8_t>& header);
    uint32_t CreateRegion(const Region& r);
    uint32_t CreatePalette(const std::vector<uint32_t>& entries);
    bool DeleteObject(uint32_t h);
    uint32_t StockObject(int index) const;
    bool IsStock(uint32_t h) const;
    // Объект, который нельзя удалить (кисти GetSysColorBrush и т.п.)
    void MarkStock(uint32_t h) { extraStock_.push_back(h); }

    uint32_t CreateDc(DcKind kind, uint32_t hwnd = 0, bool wholeWindow = false);
    uint32_t CreateCompatibleDc(uint32_t hdc);
    bool DeleteDc(uint32_t hdc);
    uint32_t SelectObject(uint32_t hdc, uint32_t obj); // старый объект, для региона — тип
    int SaveDc(uint32_t hdc);
    bool RestoreDc(uint32_t hdc, int level);

    // --- координаты ---
    Point ToDevice(const DcObj& dc, int x, int y) const;
    Point ToLogical(const DcObj& dc, int x, int y) const;
    int ScaleX(const DcObj& dc, int len) const;
    int ScaleY(const DcObj& dc, int len) const;
    // Поверхность, смещение устройства на ней и область отсечения (в
    // координатах поверхности). false — рисовать некуда.
    bool Target(uint32_t hdc, Surface*& s, int& ox, int& oy, Region& clip);
    // Размер поверхности контекста (GetDeviceCaps HORZRES и т.п.)
    void DeviceSize(uint32_t hdc, int& w, int& h);

    // --- рисование (логические координаты) ---
    bool SetPixel(uint32_t hdc, int x, int y, uint32_t colorref);
    uint32_t GetPixel(uint32_t hdc, int x, int y); // CLR_INVALID = 0xFFFFFFFF
    bool MoveTo(uint32_t hdc, int x, int y, Point* old);
    bool LineTo(uint32_t hdc, int x, int y);
    bool Polyline(uint32_t hdc, const std::vector<Point>& pts);
    bool Polygon(uint32_t hdc, const std::vector<Point>& pts);
    bool PolyPolygon(uint32_t hdc, const std::vector<Point>& pts, const std::vector<int>& counts);
    bool Rectangle(uint32_t hdc, int l, int t, int r, int b);
    bool RoundRect(uint32_t hdc, int l, int t, int r, int b, int w, int h);
    bool Ellipse(uint32_t hdc, int l, int t, int r, int b);
    // kind: 0 Arc, 1 Chord, 2 Pie
    bool ArcLike(uint32_t hdc, int kind, int l, int t, int r, int b, int x1, int y1, int x2, int y2);
    bool FillRect(uint32_t hdc, const Rect& r, uint32_t brush); // USER32 FillRect: без пера
    bool FrameRect(uint32_t hdc, const Rect& r, uint32_t brush);
    bool InvertRect(uint32_t hdc, const Rect& r);
    bool FillRgn(uint32_t hdc, uint32_t rgn, uint32_t brush);
    bool PatBlt(uint32_t hdc, int x, int y, int w, int h, uint32_t rop);
    bool BitBlt(uint32_t dst, int x, int y, int w, int h, uint32_t src, int sx, int sy, uint32_t rop);
    bool StretchBlt(uint32_t dst, int x, int y, int w, int h, uint32_t src, int sx, int sy, int sw, int sh, uint32_t rop);
    // TransparentBlt: пиксели цвета key не копируются
    bool TransparentBlt(uint32_t dst, int x, int y, int w, int h, uint32_t src, int sx, int sy, int sw, int sh, uint32_t key);
    // Вывод картинки из произвольной поверхности (StretchDIBits/SetDIBitsToDevice)
    bool BlitFromSurface(uint32_t dst, int x, int y, int w, int h, const Surface& src, int sx, int sy, int sw, int sh, uint32_t rop);
    bool ExtFloodFill(uint32_t hdc, int x, int y, uint32_t colorref, int type);

    // --- текст ---
    Font* CurrentFont(uint32_t hdc);
    // ExtTextOut: options ETO_OPAQUE 2, ETO_CLIPPED 4
    bool TextOut(uint32_t hdc, int x, int y, const std::u16string& text, int options = 0, const Rect* rect = nullptr,
                 const std::vector<int>* dx = nullptr);
    void TextExtent(uint32_t hdc, const std::u16string& text, int& w, int& h);
    // DrawText USER32; возвращает высоту текста
    int DrawText(uint32_t hdc, const std::u16string& text, Rect& rect, uint32_t format);

private:
    friend struct GdiShapeAccess;
    struct Slot {
        ObjType type = ObjType::None;
        uint16_t seq = 0;
        std::unique_ptr<PenObj> pen;
        std::unique_ptr<BrushObj> brush;
        std::unique_ptr<BitmapObj> bitmap;
        std::unique_ptr<RegionObj> region;
        std::unique_ptr<PaletteObj> palette;
        std::unique_ptr<FontObj> font;
        std::unique_ptr<DcObj> dc;
    };
    uint32_t Alloc(ObjType t, Slot*& slot);
    Slot* Find(uint32_t h);
    const Slot* Find(uint32_t h) const;

    // Растеризация в координатах устройства
    struct Ctx;
    bool MakeCtx(uint32_t hdc, Ctx& c);
    void PixelRop2(Ctx& c, int x, int y, uint32_t rgb);
    void SpanBrush(Ctx& c, int y, int x0, int x1, const BrushObj* b);
    void FillPolygonDevice(Ctx& c, const std::vector<std::vector<Point>>& polys, const BrushObj* b, bool winding);
    void StrokeDevice(Ctx& c, const std::vector<Point>& pts, bool closed, const PenObj* pen);
    void LineDevice(Ctx& c, int x0, int y0, int x1, int y1, const PenObj* pen, bool lastPoint, int& dashPos);
    void EllipsePoints(int l, int t, int r, int b, std::vector<Point>& out);
    uint32_t BrushColorAt(const Ctx& c, const BrushObj* b, int x, int y) const;
    bool BlitCore(uint32_t dst, int x, int y, int w, int h, const Surface* src, bool srcMono, int sx, int sy, int sw, int sh,
                  uint32_t rop, bool useKey, uint32_t key);

    std::vector<Slot> slots_;
    std::vector<uint32_t> free_;
    uint32_t stock_[20] = {};
    uint32_t defaultBitmap_ = 0;
    std::vector<uint32_t> extraStock_;
    Surface* screen_ = nullptr;
    WindowDcProvider provider_ = nullptr;
    uint64_t layoutGeneration_ = 1;
    uint64_t screenChanges_ = 0;
};

extern Gdi g_gdi;

// Заливка прямоугольника поверхности цветом 0x00RRGGBB в пределах clip
void FillSurface(Surface& s, const Region& clip, const Rect& r, uint32_t rgb);

// Системные цвета (GetSysColor), COLORREF; классическая схема Windows
uint32_t SysColor(int index);
void SetSysColor(int index, uint32_t colorref);

} // namespace ui
