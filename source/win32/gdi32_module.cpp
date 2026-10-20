// gdi32_module.cpp — функции GDI32 для гостя (слой "ui2"): разбор
// аргументов и структур (LOGFONT, BITMAPINFO, TEXTMETRIC, ...) и вызов
// ui::g_gdi (source/ui/gdi.*).

#include <algorithm>
#include <cmath>
#include <cstring>

#include "user32.h"

namespace win32 {
namespace gdi32 {

using namespace hle;
using ui::Rect;
using ui::Region;
using ui::g_gdi;

namespace {

#define FN(name) HostCallOutcome name(cpu::CpuState& c, cpu::IGuestMemory& m)
#define ENTER (user::G().c = &c, user::G().m = &m)
inline uint32_t A(cpu::CpuState& c, cpu::IGuestMemory& m, int i) { return Arg(c, m, i); }
inline int SA(cpu::CpuState& c, cpu::IGuestMemory& m, int i) { return static_cast<int32_t>(Arg(c, m, i)); }

Rect ReadRect(cpu::IGuestMemory& m, uint32_t p) {
    return Rect(static_cast<int32_t>(m.Read32(p)), static_cast<int32_t>(m.Read32(p + 4)),
                static_cast<int32_t>(m.Read32(p + 8)), static_cast<int32_t>(m.Read32(p + 12)));
}
void WriteRect(cpu::IGuestMemory& m, uint32_t p, const Rect& r) {
    m.Write32(p, static_cast<uint32_t>(r.left));
    m.Write32(p + 4, static_cast<uint32_t>(r.top));
    m.Write32(p + 8, static_cast<uint32_t>(r.right));
    m.Write32(p + 12, static_cast<uint32_t>(r.bottom));
}
void WritePoint(cpu::IGuestMemory& m, uint32_t p, int x, int y) {
    if (!p) return;
    m.Write32(p, static_cast<uint32_t>(x));
    m.Write32(p + 4, static_cast<uint32_t>(y));
}
std::vector<ui::Point> ReadPoints(cpu::IGuestMemory& m, uint32_t p, uint32_t n) {
    std::vector<ui::Point> v;
    for (uint32_t i = 0; i < n && i < 100000; ++i)
        v.push_back(ui::Point{static_cast<int32_t>(m.Read32(p + i * 8)), static_cast<int32_t>(m.Read32(p + i * 8 + 4))});
    return v;
}
std::u16string ReadCounted(cpu::IGuestMemory& m, uint32_t p, int len, bool unicode) {
    std::u16string s;
    if (!p) return s;
    if (len < 0) return unicode ? ReadWide(m, p) : AnsiToWide(ReadAnsi(m, p));
    for (int i = 0; i < len; ++i) s.push_back(unicode ? static_cast<char16_t>(m.Read16(p + i * 2)) : static_cast<char16_t>(m.Read8(p + i)));
    return s;
}

// --- LOGFONT ---

ui::FontDesc ReadLogFont(cpu::IGuestMemory& m, uint32_t p, bool unicode) {
    ui::FontDesc d;
    d.height = static_cast<int32_t>(m.Read32(p));
    d.width = static_cast<int32_t>(m.Read32(p + 4));
    d.weight = static_cast<int32_t>(m.Read32(p + 16));
    d.italic = m.Read8(p + 20) != 0;
    d.underline = m.Read8(p + 21) != 0;
    d.strikeout = m.Read8(p + 22) != 0;
    d.charset = m.Read8(p + 23);
    d.pitchAndFamily = m.Read8(p + 27);
    d.face = WideToUtf8(unicode ? ReadWide(m, p + 28, 32) : AnsiToWide(ReadAnsi(m, p + 28, 32)));
    if (!d.weight) d.weight = 400;
    return d;
}
void WriteLogFont(cpu::IGuestMemory& m, uint32_t p, const ui::FontDesc& d, bool unicode, uint32_t cap) {
    uint32_t size = unicode ? 92 : 60;
    if (cap < size) size = cap;
    for (uint32_t i = 0; i < size; ++i) m.Write8(p + i, 0);
    if (size < 28) return;
    m.Write32(p, static_cast<uint32_t>(d.height));
    m.Write32(p + 4, static_cast<uint32_t>(d.width));
    m.Write32(p + 16, static_cast<uint32_t>(d.weight));
    m.Write8(p + 20, d.italic);
    m.Write8(p + 21, d.underline);
    m.Write8(p + 22, d.strikeout);
    m.Write8(p + 23, d.charset);
    m.Write8(p + 27, d.pitchAndFamily);
    if (size >= (unicode ? 92u : 60u)) user::WriteText(p + 28, AnsiToWide(d.face), 32, unicode);
}

// --- DIB ---

struct DibInfo {
    int width = 0, height = 0;   // height < 0 — сверху вниз
    int bpp = 0;
    uint32_t compression = 0;
    uint32_t headerSize = 40;
    std::vector<uint32_t> palette;
    bool rgb565 = false;
    bool valid = false;
};

DibInfo ReadDibInfo(cpu::IGuestMemory& m, uint32_t bmi, uint32_t usage, uint32_t hdc) {
    DibInfo d;
    if (!bmi) return d;
    d.headerSize = m.Read32(bmi);
    if (d.headerSize == 12) { // BITMAPCOREHEADER
        d.width = m.Read16(bmi + 4);
        d.height = static_cast<int16_t>(m.Read16(bmi + 6));
        d.bpp = m.Read16(bmi + 10);
    } else {
        d.width = static_cast<int32_t>(m.Read32(bmi + 4));
        d.height = static_cast<int32_t>(m.Read32(bmi + 8));
        d.bpp = m.Read16(bmi + 14);
        d.compression = m.Read32(bmi + 16);
    }
    if (d.compression != 0 && d.compression != 3) return d; // RLE/JPEG не поддерживаем
    uint32_t tbl = bmi + d.headerSize;
    if (d.compression == 3) {
        uint32_t g = m.Read32(tbl + 4);
        d.rgb565 = d.bpp == 16 && g == 0x7E0;
    }
    if (d.bpp <= 8) {
        uint32_t used = d.headerSize >= 40 ? m.Read32(bmi + 32) : 0;
        uint32_t n = used ? used : (1u << d.bpp);
        if (n > 256) n = 256;
        ui::DcObj* dc = g_gdi.Dc(hdc);
        ui::PaletteObj* pal = dc ? g_gdi.Palette(dc->st.palette) : nullptr;
        for (uint32_t i = 0; i < n; ++i) {
            if (usage == 1) { // DIB_PAL_COLORS: индексы в палитру контекста
                uint16_t idx = m.Read16(tbl + i * 2);
                uint32_t e = (pal && idx < pal->entries.size()) ? pal->entries[idx] : 0;
                d.palette.push_back(ui::ColorrefToRgb(e & 0xFFFFFF));
            } else if (d.headerSize == 12) {
                d.palette.push_back(m.Read8(tbl + i * 3) | (m.Read8(tbl + i * 3 + 1) << 8) | (m.Read8(tbl + i * 3 + 2) << 16));
            } else {
                d.palette.push_back(m.Read32(tbl + i * 4) & 0xFFFFFF);
            }
        }
    }
    d.valid = d.width > 0 && d.height != 0 && (d.bpp == 1 || d.bpp == 4 || d.bpp == 8 || d.bpp == 16 || d.bpp == 24 || d.bpp == 32);
    return d;
}

// Поверхность над пикселями DIB в памяти гостя (если блок непрерывен на
// хосте — без копирования, иначе через копию)
struct DibView {
    ui::Surface s;
    std::vector<uint8_t> copy;
    uint32_t guest = 0;
    bool copied = false;
};
bool MakeDibView(cpu::IGuestMemory& m, uint32_t bits, const DibInfo& d, DibView& v) {
    if (!d.valid || !bits) return false;
    int h = d.height < 0 ? -d.height : d.height;
    int pitch = ui::Surface::DibPitch(d.width, d.bpp);
    uint32_t size = static_cast<uint32_t>(pitch) * static_cast<uint32_t>(h);
    uint8_t* p0 = m.HostPointer(bits);
    uint8_t* p1 = size ? m.HostPointer(bits + size - 1) : p0;
    v.guest = bits;
    uint8_t* base;
    if (p0 && p1 && p1 == p0 + size - 1) {
        base = p0;
    } else {
        v.copy.resize(size);
        m.ReadBytes(bits, v.copy.data(), size);
        base = v.copy.data();
        v.copied = true;
    }
    v.s.width = d.width;
    v.s.height = h;
    v.s.bpp = d.bpp;
    v.s.rgb565 = d.rgb565;
    v.s.palette = d.palette;
    if (d.height > 0) { v.s.bits = base + static_cast<size_t>(pitch) * (h - 1); v.s.pitch = -pitch; }
    else { v.s.bits = base; v.s.pitch = pitch; }
    return true;
}
void FlushDibView(cpu::IGuestMemory& m, DibView& v) {
    if (v.copied) m.LoadBytes(v.guest, v.copy.data(), v.copy.size());
}

// --- Объекты ---

FN(CreatePen) { ENTER; return Ret(g_gdi.CreatePen(SA(c, m, 0), SA(c, m, 1), A(c, m, 2))); }
FN(CreatePenIndirect) {
    ENTER;
    uint32_t p = A(c, m, 0);
    return Ret(g_gdi.CreatePen(static_cast<int>(m.Read32(p)), static_cast<int32_t>(m.Read32(p + 4)), m.Read32(p + 12)));
}
FN(ExtCreatePen) {
    ENTER;
    uint32_t lb = A(c, m, 2);
    return Ret(g_gdi.CreatePen(static_cast<int>(A(c, m, 0) & 0xF), SA(c, m, 1), lb ? m.Read32(lb + 4) : 0));
}
FN(CreateSolidBrush) { ENTER; return Ret(g_gdi.CreateSolidBrush(A(c, m, 0))); }
FN(CreateHatchBrush) {
    ENTER;
    ui::BrushObj b;
    b.style = 2;
    b.hatch = SA(c, m, 0);
    b.color = A(c, m, 1);
    return Ret(g_gdi.CreateBrush(b));
}
uint32_t PatternBrushFromBitmap(uint32_t hbm) {
    ui::BitmapObj* bmp = g_gdi.Bitmap(hbm);
    if (!bmp) return 0;
    ui::BrushObj b;
    b.style = 3;
    b.patternBitmap = hbm;
    const ui::Surface& s = bmp->view;
    b.pattern.Allocate(s.width, s.height, s.bpp == 1 ? 1 : 32);
    b.pattern.s.palette = s.palette;
    for (int y = 0; y < s.height; ++y)
        for (int x = 0; x < s.width; ++x) {
            if (s.bpp == 1) b.pattern.s.Set(x, y, s.GetRaw(x, y) ? 0xFFFFFF : 0);
            else b.pattern.s.Set(x, y, s.Get(x, y));
        }
    if (s.bpp == 1) b.pattern.s.palette = {0, 0xFFFFFF};
    return g_gdi.CreateBrush(b);
}
FN(CreatePatternBrush) { ENTER; return Ret(PatternBrushFromBitmap(A(c, m, 0))); }
uint32_t DibPatternBrush(cpu::IGuestMemory& m, uint32_t packed, uint32_t usage) {
    DibInfo d = ReadDibInfo(m, packed, usage, 0);
    if (!d.valid) return 0;
    uint32_t colors = d.bpp <= 8 ? static_cast<uint32_t>(d.palette.size()) * (usage == 1 ? 2 : 4) : (d.compression == 3 ? 12 : 0);
    DibView v;
    if (!MakeDibView(m, packed + d.headerSize + colors, d, v)) return 0;
    ui::BrushObj b;
    b.style = 5;
    b.pattern.Allocate(v.s.width, v.s.height, 32);
    for (int y = 0; y < v.s.height; ++y)
        for (int x = 0; x < v.s.width; ++x) b.pattern.s.Set(x, y, v.s.Get(x, y));
    return g_gdi.CreateBrush(b);
}
FN(CreateDIBPatternBrushPt) { ENTER; return Ret(DibPatternBrush(m, A(c, m, 0), A(c, m, 1))); }
FN(CreateBrushIndirect) {
    ENTER;
    uint32_t p = A(c, m, 0);
    ui::BrushObj b;
    b.style = static_cast<int>(m.Read32(p));
    b.color = m.Read32(p + 4);
    b.hatch = static_cast<int>(m.Read32(p + 8));
    if (b.style == 3) return Ret(PatternBrushFromBitmap(b.hatch));
    if (b.style > 3) b.style = 0;
    return Ret(g_gdi.CreateBrush(b));
}
FN(GetStockObject) { ENTER; return Ret(g_gdi.StockObject(SA(c, m, 0))); }
FN(SelectObject) { ENTER; return Ret(g_gdi.SelectObject(A(c, m, 0), A(c, m, 1))); }
FN(DeleteObject) { ENTER; return Ret(g_gdi.DeleteObject(A(c, m, 0)) ? 1 : 0); }
FN(GetObjectType) {
    ENTER;
    uint32_t h = A(c, m, 0);
    ui::ObjType t = g_gdi.TypeOf(h);
    if (t == ui::ObjType::MemDC) return Ret(10);
    return Ret(static_cast<uint32_t>(t));
}
FN(GetCurrentObject) {
    ENTER;
    ui::DcObj* dc = g_gdi.Dc(A(c, m, 0));
    if (!dc) return Ret(0);
    switch (A(c, m, 1)) {
    case 1: return Ret(dc->st.pen);
    case 2: return Ret(dc->st.brush);
    case 5: return Ret(dc->st.palette);
    case 6: return Ret(dc->st.font);
    case 7: return Ret(dc->st.bitmap);
    }
    return Ret(0);
}

HostCallOutcome GetObjectCommon(cpu::CpuState& c, cpu::IGuestMemory& m, bool unicode) {
    uint32_t h = A(c, m, 0), size = A(c, m, 1), out = A(c, m, 2);
    switch (g_gdi.TypeOf(h)) {
    case ui::ObjType::Pen: {
        ui::PenObj* p = g_gdi.Pen(h);
        if (!out) return Ret(16);
        if (size < 16) return Ret(0);
        m.Write32(out, static_cast<uint32_t>(p->style));
        m.Write32(out + 4, static_cast<uint32_t>(p->width));
        m.Write32(out + 8, 0);
        m.Write32(out + 12, p->color);
        return Ret(16);
    }
    case ui::ObjType::Brush: {
        ui::BrushObj* b = g_gdi.Brush(h);
        if (!out) return Ret(12);
        if (size < 12) return Ret(0);
        m.Write32(out, static_cast<uint32_t>(b->style == 5 ? 3 : b->style));
        m.Write32(out + 4, b->sysColorIndex >= 0 ? ui::SysColor(b->sysColorIndex) : b->color);
        m.Write32(out + 8, b->style == 3 ? b->patternBitmap : static_cast<uint32_t>(b->hatch));
        return Ret(12);
    }
    case ui::ObjType::Font: {
        uint32_t need = unicode ? 92 : 60;
        if (!out) return Ret(need);
        WriteLogFont(m, out, g_gdi.FontO(h)->desc, unicode, size);
        return Ret(std::min(size, need));
    }
    case ui::ObjType::Bitmap: {
        ui::BitmapObj* b = g_gdi.Bitmap(h);
        uint32_t need = (b->isDib && size >= 84) ? 84 : 24;
        if (!out) return Ret(b->isDib ? 84 : 24);
        if (size < 24) return Ret(0);
        const ui::Surface& s = b->view;
        m.Write32(out, 0);
        m.Write32(out + 4, static_cast<uint32_t>(s.width));
        m.Write32(out + 8, static_cast<uint32_t>(s.height));
        m.Write32(out + 12, static_cast<uint32_t>(b->isDib ? ui::Surface::DibPitch(s.width, s.bpp) : ((s.width * s.bpp + 15) / 16) * 2));
        m.Write16(out + 16, 1);
        m.Write16(out + 18, static_cast<uint16_t>(s.bpp));
        m.Write32(out + 20, b->isDib ? b->guestBits : 0);
        if (need == 84) {
            for (uint32_t i = 0; i < 40 && i < b->infoHeader.size(); ++i) m.Write8(out + 24 + i, b->infoHeader[i]);
            for (uint32_t i = 64; i < 84; i += 4) m.Write32(out + i, 0);
            if (s.bpp == 16) { m.Write32(out + 64, s.rgb565 ? 0xF800 : 0x7C00); m.Write32(out + 68, s.rgb565 ? 0x7E0 : 0x3E0); m.Write32(out + 72, 0x1F); }
        }
        return Ret(need);
    }
    case ui::ObjType::Palette:
        if (!out) return Ret(2);
        m.Write16(out, static_cast<uint16_t>(g_gdi.Palette(h)->entries.size()));
        return Ret(2);
    default:
        return Ret(0);
    }
}
FN(GetObjectA) { ENTER; return GetObjectCommon(c, m, false); }
FN(GetObjectW) { ENTER; return GetObjectCommon(c, m, true); }

HostCallOutcome CreateFontCommon(cpu::CpuState& c, cpu::IGuestMemory& m, bool unicode) {
    ui::FontDesc d;
    d.height = SA(c, m, 0);
    d.width = SA(c, m, 1);
    d.weight = SA(c, m, 4) ? SA(c, m, 4) : 400;
    d.italic = A(c, m, 5) != 0;
    d.underline = A(c, m, 6) != 0;
    d.strikeout = A(c, m, 7) != 0;
    d.charset = static_cast<uint8_t>(A(c, m, 8));
    d.pitchAndFamily = static_cast<uint8_t>(A(c, m, 12));
    uint32_t face = A(c, m, 13);
    d.face = face ? WideToUtf8(unicode ? ReadWide(m, face, 32) : AnsiToWide(ReadAnsi(m, face, 32))) : "";
    return Ret(g_gdi.CreateFont(d));
}
FN(CreateFontA) { ENTER; return CreateFontCommon(c, m, false); }
FN(CreateFontW) { ENTER; return CreateFontCommon(c, m, true); }
FN(CreateFontIndirectA) { ENTER; return Ret(g_gdi.CreateFont(ReadLogFont(m, A(c, m, 0), false))); }
FN(CreateFontIndirectW) { ENTER; return Ret(g_gdi.CreateFont(ReadLogFont(m, A(c, m, 0), true))); }

// --- Контексты ---

FN(CreateCompatibleDC) { ENTER; return Ret(g_gdi.CreateCompatibleDc(A(c, m, 0))); }
HostCallOutcome CreateDcCommon(cpu::CpuState& c, cpu::IGuestMemory& m, bool unicode, bool info) {
    std::u16string drv = user::ReadText(A(c, m, 0), unicode), dev = user::ReadText(A(c, m, 1), unicode);
    for (char16_t& ch : drv) if (ch >= 'a' && ch <= 'z') ch = static_cast<char16_t>(ch - 32);
    if (drv == u"DISPLAY" || dev == u"\\\\.\\DISPLAY1" || (drv.empty() && dev.empty()))
        return Ret(g_gdi.CreateDc(info ? ui::DcKind::Info : ui::DcKind::Screen));
    return Ret(0); // принтеров нет
}
FN(CreateDCA) { ENTER; return CreateDcCommon(c, m, false, false); }
FN(CreateDCW) { ENTER; return CreateDcCommon(c, m, true, false); }
FN(CreateICA) { ENTER; return CreateDcCommon(c, m, false, true); }
FN(CreateICW) { ENTER; return CreateDcCommon(c, m, true, true); }
FN(DeleteDC) { ENTER; return Ret(g_gdi.DeleteDc(A(c, m, 0)) ? 1 : 0); }
FN(SaveDC) { ENTER; return Ret(static_cast<uint32_t>(g_gdi.SaveDc(A(c, m, 0)))); }
FN(RestoreDC) { ENTER; return Ret(g_gdi.RestoreDc(A(c, m, 0), SA(c, m, 1)) ? 1 : 0); }
FN(GetDeviceCaps) {
    ENTER;
    ui::DcObj* dc = g_gdi.Dc(A(c, m, 0));
    int W = user::S().screenW, H = user::S().screenH;
    int bpp = 32;
    if (dc && dc->kind == ui::DcKind::Memory) if (ui::BitmapObj* b = g_gdi.Bitmap(dc->st.bitmap)) if (b->view.bpp == 1) bpp = 1;
    switch (SA(c, m, 1)) {
    case 0: return Ret(0x400);     // DRIVERVERSION
    case 2: return Ret(1);         // TECHNOLOGY: DT_RASDISPLAY
    case 4: return Ret(static_cast<uint32_t>(W * 254 / 960));  // HORZSIZE, мм
    case 6: return Ret(static_cast<uint32_t>(H * 254 / 960));
    case 8: case 118: return Ret(static_cast<uint32_t>(W));
    case 10: case 117: return Ret(static_cast<uint32_t>(H));
    case 12: return Ret(static_cast<uint32_t>(bpp));
    case 14: return Ret(1);
    case 16: case 18: case 24: return Ret(0xFFFFFFFFu);
    case 22: return Ret(0);
    case 26: return Ret(0);
    case 28: return Ret(0xFF);     // CURVECAPS
    case 30: return Ret(0xFE);     // LINECAPS
    case 32: return Ret(0xFF);     // POLYGONALCAPS
    case 34: return Ret(0x7807);   // TEXTCAPS
    case 36: return Ret(1);        // CLIPCAPS
    case 38: return Ret(0x7E99);   // RASTERCAPS
    case 40: case 42: return Ret(36);
    case 44: return Ret(51);
    case 88: case 90: return Ret(96); // LOGPIXELSX/Y
    case 104: return Ret(0);       // SIZEPALETTE
    case 106: return Ret(20);
    case 108: return Ret(24);
    case 116: return Ret(60);      // VREFRESH
    case 119: return Ret(1);       // BLTALIGNMENT
    }
    return Ret(0);
}

// --- Атрибуты контекста ---

#define DC_OR_FAIL ui::DcObj* dc = g_gdi.Dc(A(c, m, 0)); if (!dc) return Ret(0)
FN(SetTextColor) { ENTER; ui::DcObj* dc = g_gdi.Dc(A(c, m, 0)); if (!dc) return Ret(0xFFFFFFFFu); uint32_t o = dc->st.textColor; dc->st.textColor = A(c, m, 1) & 0xFFFFFF; return Ret(o); }
FN(GetTextColor) { ENTER; DC_OR_FAIL; return Ret(dc->st.textColor); }
FN(SetBkColor) { ENTER; ui::DcObj* dc = g_gdi.Dc(A(c, m, 0)); if (!dc) return Ret(0xFFFFFFFFu); uint32_t o = dc->st.bkColor; dc->st.bkColor = A(c, m, 1) & 0xFFFFFF; return Ret(o); }
FN(GetBkColor) { ENTER; DC_OR_FAIL; return Ret(dc->st.bkColor); }
FN(SetBkMode) { ENTER; DC_OR_FAIL; uint32_t o = static_cast<uint32_t>(dc->st.bkMode); dc->st.bkMode = SA(c, m, 1); return Ret(o); }
FN(GetBkMode) { ENTER; DC_OR_FAIL; return Ret(static_cast<uint32_t>(dc->st.bkMode)); }
FN(SetROP2) { ENTER; DC_OR_FAIL; uint32_t o = static_cast<uint32_t>(dc->st.rop2); dc->st.rop2 = SA(c, m, 1); return Ret(o); }
FN(GetROP2) { ENTER; DC_OR_FAIL; return Ret(static_cast<uint32_t>(dc->st.rop2)); }
FN(SetTextAlign) { ENTER; ui::DcObj* dc = g_gdi.Dc(A(c, m, 0)); if (!dc) return Ret(0xFFFFFFFFu); uint32_t o = static_cast<uint32_t>(dc->st.textAlign); dc->st.textAlign = SA(c, m, 1); return Ret(o); }
FN(GetTextAlign) { ENTER; DC_OR_FAIL; return Ret(static_cast<uint32_t>(dc->st.textAlign)); }
FN(SetStretchBltMode) { ENTER; DC_OR_FAIL; uint32_t o = static_cast<uint32_t>(dc->st.stretchMode); dc->st.stretchMode = SA(c, m, 1); return Ret(o); }
FN(GetStretchBltMode) { ENTER; DC_OR_FAIL; return Ret(static_cast<uint32_t>(dc->st.stretchMode)); }
FN(SetPolyFillMode) { ENTER; DC_OR_FAIL; uint32_t o = static_cast<uint32_t>(dc->st.polyFillMode); dc->st.polyFillMode = SA(c, m, 1); return Ret(o); }
FN(GetPolyFillMode) { ENTER; DC_OR_FAIL; return Ret(static_cast<uint32_t>(dc->st.polyFillMode)); }
FN(SetTextCharacterExtra) { ENTER; DC_OR_FAIL; uint32_t o = static_cast<uint32_t>(dc->st.charExtra); dc->st.charExtra = SA(c, m, 1); return Ret(o); }
FN(GetTextCharacterExtra) { ENTER; DC_OR_FAIL; return Ret(static_cast<uint32_t>(dc->st.charExtra)); }
FN(SetArcDirection) { ENTER; DC_OR_FAIL; uint32_t o = static_cast<uint32_t>(dc->st.arcDirection); dc->st.arcDirection = SA(c, m, 1); return Ret(o); }
FN(GetArcDirection) { ENTER; DC_OR_FAIL; return Ret(static_cast<uint32_t>(dc->st.arcDirection)); }
FN(SetMapMode) {
    ENTER;
    DC_OR_FAIL;
    uint32_t o = static_cast<uint32_t>(dc->st.mapMode);
    int mode = SA(c, m, 1);
    dc->st.mapMode = mode;
    // фиксированные метрические режимы: логические единицы в пикселях при 96 dpi
    switch (mode) {
    case 1: dc->st.windowExt = dc->st.viewportExt = {1, 1}; break;
    case 2: dc->st.windowExt = {254, 254}; dc->st.viewportExt = {96, -96}; break;      // MM_LOMETRIC 0.1 мм
    case 3: dc->st.windowExt = {2540, 2540}; dc->st.viewportExt = {96, -96}; break;    // MM_HIMETRIC
    case 4: dc->st.windowExt = {100, 100}; dc->st.viewportExt = {96, -96}; break;      // MM_LOENGLISH
    case 5: dc->st.windowExt = {1000, 1000}; dc->st.viewportExt = {96, -96}; break;    // MM_HIENGLISH
    case 6: dc->st.windowExt = {1440, 1440}; dc->st.viewportExt = {96, -96}; break;    // MM_TWIPS
    }
    return Ret(o);
}
FN(GetMapMode) { ENTER; DC_OR_FAIL; return Ret(static_cast<uint32_t>(dc->st.mapMode)); }
FN(SetViewportOrgEx) { ENTER; DC_OR_FAIL; WritePoint(m, A(c, m, 3), dc->st.viewportOrg.x, dc->st.viewportOrg.y); dc->st.viewportOrg = {SA(c, m, 1), SA(c, m, 2)}; return Ret(1); }
FN(GetViewportOrgEx) { ENTER; DC_OR_FAIL; WritePoint(m, A(c, m, 1), dc->st.viewportOrg.x, dc->st.viewportOrg.y); return Ret(1); }
FN(SetWindowOrgEx) { ENTER; DC_OR_FAIL; WritePoint(m, A(c, m, 3), dc->st.windowOrg.x, dc->st.windowOrg.y); dc->st.windowOrg = {SA(c, m, 1), SA(c, m, 2)}; return Ret(1); }
FN(GetWindowOrgEx) { ENTER; DC_OR_FAIL; WritePoint(m, A(c, m, 1), dc->st.windowOrg.x, dc->st.windowOrg.y); return Ret(1); }
FN(OffsetViewportOrgEx) {
    ENTER; DC_OR_FAIL;
    WritePoint(m, A(c, m, 3), dc->st.viewportOrg.x, dc->st.viewportOrg.y);
    dc->st.viewportOrg.x += SA(c, m, 1);
    dc->st.viewportOrg.y += SA(c, m, 2);
    return Ret(1);
}
FN(OffsetWindowOrgEx) {
    ENTER; DC_OR_FAIL;
    WritePoint(m, A(c, m, 3), dc->st.windowOrg.x, dc->st.windowOrg.y);
    dc->st.windowOrg.x += SA(c, m, 1);
    dc->st.windowOrg.y += SA(c, m, 2);
    return Ret(1);
}
void FixIsotropic(ui::DcObj* dc) {
    if (dc->st.mapMode != 7 || !dc->st.windowExt.x || !dc->st.windowExt.y) return;
    // MM_ISOTROPIC: одинаковый масштаб по осям — по меньшему
    double sx = static_cast<double>(dc->st.viewportExt.x) / dc->st.windowExt.x;
    double sy = static_cast<double>(dc->st.viewportExt.y) / dc->st.windowExt.y;
    if (std::abs(sx) > std::abs(sy)) dc->st.viewportExt.x = static_cast<int>(dc->st.windowExt.x * std::abs(sy) * (sx < 0 ? -1 : 1));
    else dc->st.viewportExt.y = static_cast<int>(dc->st.windowExt.y * std::abs(sx) * (sy < 0 ? -1 : 1));
}
FN(SetViewportExtEx) {
    ENTER; DC_OR_FAIL;
    WritePoint(m, A(c, m, 3), dc->st.viewportExt.x, dc->st.viewportExt.y);
    if (dc->st.mapMode < 7) return Ret(1);
    dc->st.viewportExt = {SA(c, m, 1), SA(c, m, 2)};
    FixIsotropic(dc);
    return Ret(1);
}
FN(SetWindowExtEx) {
    ENTER; DC_OR_FAIL;
    WritePoint(m, A(c, m, 3), dc->st.windowExt.x, dc->st.windowExt.y);
    if (dc->st.mapMode < 7) return Ret(1);
    dc->st.windowExt = {SA(c, m, 1), SA(c, m, 2)};
    FixIsotropic(dc);
    return Ret(1);
}
FN(GetViewportExtEx) { ENTER; DC_OR_FAIL; WritePoint(m, A(c, m, 1), dc->st.viewportExt.x, dc->st.viewportExt.y); return Ret(1); }
FN(GetWindowExtEx) { ENTER; DC_OR_FAIL; WritePoint(m, A(c, m, 1), dc->st.windowExt.x, dc->st.windowExt.y); return Ret(1); }
FN(DPtoLP) {
    ENTER; DC_OR_FAIL;
    uint32_t p = A(c, m, 1);
    for (int i = 0; i < SA(c, m, 2); ++i) {
        ui::Point q = g_gdi.ToLogical(*dc, static_cast<int32_t>(m.Read32(p + i * 8)), static_cast<int32_t>(m.Read32(p + i * 8 + 4)));
        WritePoint(m, p + i * 8, q.x, q.y);
    }
    return Ret(1);
}
FN(LPtoDP) {
    ENTER; DC_OR_FAIL;
    uint32_t p = A(c, m, 1);
    for (int i = 0; i < SA(c, m, 2); ++i) {
        ui::Point q = g_gdi.ToDevice(*dc, static_cast<int32_t>(m.Read32(p + i * 8)), static_cast<int32_t>(m.Read32(p + i * 8 + 4)));
        WritePoint(m, p + i * 8, q.x, q.y);
    }
    return Ret(1);
}
FN(SetBrushOrgEx) { ENTER; DC_OR_FAIL; WritePoint(m, A(c, m, 3), dc->st.brushOrg.x, dc->st.brushOrg.y); dc->st.brushOrg = {SA(c, m, 1), SA(c, m, 2)}; return Ret(1); }
FN(GetBrushOrgEx) { ENTER; DC_OR_FAIL; WritePoint(m, A(c, m, 1), dc->st.brushOrg.x, dc->st.brushOrg.y); return Ret(1); }
FN(GetCurrentPositionEx) { ENTER; DC_OR_FAIL; WritePoint(m, A(c, m, 1), dc->st.curPos.x, dc->st.curPos.y); return Ret(1); }
FN(GetDCOrgEx) {
    ENTER; DC_OR_FAIL;
    int x = 0, y = 0;
    if (dc->kind == ui::DcKind::Window) {
        if (user::Window* w = user::Win(dc->hwnd)) {
            Rect r = dc->wholeWindow ? user::ScreenRect(w) : user::ScreenClientRect(w);
            x = r.left; y = r.top;
        }
    }
    WritePoint(m, A(c, m, 1), x, y);
    return Ret(1);
}
FN(SetGraphicsMode) { ENTER; return Ret(1); }
FN(GetGraphicsMode) { ENTER; return Ret(1); }
FN(SetLayout) { ENTER; DC_OR_FAIL; uint32_t o = dc->st.layout; dc->st.layout = A(c, m, 1); return Ret(o); }
FN(GetLayout) { ENTER; DC_OR_FAIL; return Ret(dc->st.layout); }
FN(SetMapperFlags) { ENTER; return Ret(0); }
FN(SetDCBrushColor) {
    ENTER;
    ui::BrushObj* b = g_gdi.Brush(g_gdi.StockObject(18));
    uint32_t o = b->color;
    b->color = A(c, m, 1) & 0xFFFFFF;
    return Ret(o);
}
FN(SetDCPenColor) {
    ENTER;
    ui::PenObj* p = g_gdi.Pen(g_gdi.StockObject(19));
    uint32_t o = p->color;
    p->color = A(c, m, 1) & 0xFFFFFF;
    return Ret(o);
}
FN(GdiFlush) { ENTER; return Ret(1); }
FN(GdiSetBatchLimit) { ENTER; return Ret(20); }

// --- Рисование ---

FN(SetPixel) {
    ENTER;
    uint32_t col = A(c, m, 3) & 0xFFFFFF;
    return Ret(g_gdi.SetPixel(A(c, m, 0), SA(c, m, 1), SA(c, m, 2), col) ? col : 0xFFFFFFFFu);
}
FN(SetPixelV) { ENTER; return Ret(g_gdi.SetPixel(A(c, m, 0), SA(c, m, 1), SA(c, m, 2), A(c, m, 3)) ? 1 : 0); }
FN(GetPixel) { ENTER; return Ret(g_gdi.GetPixel(A(c, m, 0), SA(c, m, 1), SA(c, m, 2))); }
FN(MoveToEx) {
    ENTER;
    ui::Point old;
    if (!g_gdi.MoveTo(A(c, m, 0), SA(c, m, 1), SA(c, m, 2), &old)) return Ret(0);
    WritePoint(m, A(c, m, 3), old.x, old.y);
    return Ret(1);
}
FN(LineTo) { ENTER; return Ret(g_gdi.LineTo(A(c, m, 0), SA(c, m, 1), SA(c, m, 2)) ? 1 : 0); }
FN(Polyline) { ENTER; return Ret(g_gdi.Polyline(A(c, m, 0), ReadPoints(m, A(c, m, 1), A(c, m, 2))) ? 1 : 0); }
FN(PolylineTo) {
    ENTER;
    ui::DcObj* dc = g_gdi.Dc(A(c, m, 0));
    if (!dc) return Ret(0);
    std::vector<ui::Point> pts = ReadPoints(m, A(c, m, 1), A(c, m, 2));
    pts.insert(pts.begin(), dc->st.curPos);
    g_gdi.Polyline(A(c, m, 0), pts);
    if (pts.size() > 1) dc->st.curPos = pts.back();
    return Ret(1);
}
FN(PolyPolyline) {
    ENTER;
    uint32_t counts = A(c, m, 2), n = A(c, m, 3), p = A(c, m, 1);
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t k = m.Read32(counts + i * 4);
        g_gdi.Polyline(A(c, m, 0), ReadPoints(m, p, k));
        p += k * 8;
    }
    return Ret(1);
}
FN(Polygon) { ENTER; return Ret(g_gdi.Polygon(A(c, m, 0), ReadPoints(m, A(c, m, 1), A(c, m, 2))) ? 1 : 0); }
FN(PolyPolygon) {
    ENTER;
    uint32_t n = A(c, m, 3), total = 0;
    std::vector<int> counts;
    for (uint32_t i = 0; i < n && i < 10000; ++i) { counts.push_back(static_cast<int32_t>(m.Read32(A(c, m, 2) + i * 4))); total += counts.back(); }
    return Ret(g_gdi.PolyPolygon(A(c, m, 0), ReadPoints(m, A(c, m, 1), total), counts) ? 1 : 0);
}
FN(Rectangle) { ENTER; return Ret(g_gdi.Rectangle(A(c, m, 0), SA(c, m, 1), SA(c, m, 2), SA(c, m, 3), SA(c, m, 4)) ? 1 : 0); }
FN(RoundRect) { ENTER; return Ret(g_gdi.RoundRect(A(c, m, 0), SA(c, m, 1), SA(c, m, 2), SA(c, m, 3), SA(c, m, 4), SA(c, m, 5), SA(c, m, 6)) ? 1 : 0); }
FN(Ellipse) { ENTER; return Ret(g_gdi.Ellipse(A(c, m, 0), SA(c, m, 1), SA(c, m, 2), SA(c, m, 3), SA(c, m, 4)) ? 1 : 0); }
HostCallOutcome ArcCommon(cpu::CpuState& c, cpu::IGuestMemory& m, int kind) {
    return Ret(g_gdi.ArcLike(A(c, m, 0), kind, SA(c, m, 1), SA(c, m, 2), SA(c, m, 3), SA(c, m, 4), SA(c, m, 5), SA(c, m, 6),
                             SA(c, m, 7), SA(c, m, 8)) ? 1 : 0);
}
FN(Arc) { ENTER; return ArcCommon(c, m, 0); }
FN(Chord) { ENTER; return ArcCommon(c, m, 1); }
FN(Pie) { ENTER; return ArcCommon(c, m, 2); }
FN(PatBlt) { ENTER; return Ret(g_gdi.PatBlt(A(c, m, 0), SA(c, m, 1), SA(c, m, 2), SA(c, m, 3), SA(c, m, 4), A(c, m, 5)) ? 1 : 0); }
FN(BitBlt) {
    ENTER;
    return Ret(g_gdi.BitBlt(A(c, m, 0), SA(c, m, 1), SA(c, m, 2), SA(c, m, 3), SA(c, m, 4), A(c, m, 5), SA(c, m, 6), SA(c, m, 7), A(c, m, 8)) ? 1 : 0);
}
FN(StretchBlt) {
    ENTER;
    return Ret(g_gdi.StretchBlt(A(c, m, 0), SA(c, m, 1), SA(c, m, 2), SA(c, m, 3), SA(c, m, 4), A(c, m, 5), SA(c, m, 6), SA(c, m, 7),
                                SA(c, m, 8), SA(c, m, 9), A(c, m, 10)) ? 1 : 0);
}
FN(TransparentBlt) {
    ENTER;
    return Ret(g_gdi.TransparentBlt(A(c, m, 0), SA(c, m, 1), SA(c, m, 2), SA(c, m, 3), SA(c, m, 4), A(c, m, 5), SA(c, m, 6), SA(c, m, 7),
                                    SA(c, m, 8), SA(c, m, 9), A(c, m, 10)) ? 1 : 0);
}
FN(MaskBlt) {
    ENTER;
    // маска не поддерживается: обычный BitBlt с «передним» ROP
    uint32_t rop = A(c, m, 11);
    return Ret(g_gdi.BitBlt(A(c, m, 0), SA(c, m, 1), SA(c, m, 2), SA(c, m, 3), SA(c, m, 4), A(c, m, 5), SA(c, m, 6), SA(c, m, 7),
                            (rop & 0x00FFFFFF) | ((rop >> 24) << 16)) ? 1 : 0);
}
FN(FillRgn) { ENTER; return Ret(g_gdi.FillRgn(A(c, m, 0), A(c, m, 1), A(c, m, 2)) ? 1 : 0); }
FN(PaintRgn) { ENTER; ui::DcObj* dc = g_gdi.Dc(A(c, m, 0)); return Ret(dc && g_gdi.FillRgn(A(c, m, 0), A(c, m, 1), dc->st.brush) ? 1 : 0); }
FN(FrameRgn) {
    ENTER;
    ui::RegionObj* r = g_gdi.Rgn(A(c, m, 1));
    if (!r) return Ret(0);
    int w = SA(c, m, 3), h = SA(c, m, 4);
    for (const Rect& q : r->rgn.Rects()) {
        g_gdi.FillRect(A(c, m, 0), Rect(q.left, q.top, q.right, q.top + h), A(c, m, 2));
        g_gdi.FillRect(A(c, m, 0), Rect(q.left, q.bottom - h, q.right, q.bottom), A(c, m, 2));
        g_gdi.FillRect(A(c, m, 0), Rect(q.left, q.top, q.left + w, q.bottom), A(c, m, 2));
        g_gdi.FillRect(A(c, m, 0), Rect(q.right - w, q.top, q.right, q.bottom), A(c, m, 2));
    }
    return Ret(1);
}
FN(InvertRgn) {
    ENTER;
    ui::RegionObj* r = g_gdi.Rgn(A(c, m, 1));
    if (!r) return Ret(0);
    for (const Rect& q : r->rgn.Rects()) g_gdi.InvertRect(A(c, m, 0), q);
    return Ret(1);
}
FN(ExtFloodFill) { ENTER; return Ret(g_gdi.ExtFloodFill(A(c, m, 0), SA(c, m, 1), SA(c, m, 2), A(c, m, 3), SA(c, m, 4)) ? 1 : 0); }
FN(FloodFill) { ENTER; return Ret(g_gdi.ExtFloodFill(A(c, m, 0), SA(c, m, 1), SA(c, m, 2), A(c, m, 3), 0) ? 1 : 0); }

// --- Текст ---

HostCallOutcome TextOutCommon(cpu::CpuState& c, cpu::IGuestMemory& m, bool unicode) {
    return Ret(g_gdi.TextOut(A(c, m, 0), SA(c, m, 1), SA(c, m, 2), ReadCounted(m, A(c, m, 3), SA(c, m, 4), unicode)) ? 1 : 0);
}
FN(TextOutA) { ENTER; return TextOutCommon(c, m, false); }
FN(TextOutW) { ENTER; return TextOutCommon(c, m, true); }
HostCallOutcome ExtTextOutCommon(cpu::CpuState& c, cpu::IGuestMemory& m, bool unicode) {
    uint32_t opts = A(c, m, 3), rp = A(c, m, 4), dxp = A(c, m, 7);
    int len = static_cast<int>(A(c, m, 6));
    Rect r;
    if (rp) r = ReadRect(m, rp);
    std::u16string text = (opts & 0x10) ? std::u16string() : ReadCounted(m, A(c, m, 5), len, unicode); // ETO_GLYPH_INDEX: без текста
    std::vector<int> dx;
    if (dxp) for (int i = 0; i < len; ++i) dx.push_back(static_cast<int32_t>(m.Read32(dxp + i * 4 * ((opts & 0x2000) ? 2 : 1))));
    return Ret(g_gdi.TextOut(A(c, m, 0), SA(c, m, 1), SA(c, m, 2), text, static_cast<int>(opts), rp ? &r : nullptr, dxp ? &dx : nullptr) ? 1 : 0);
}
FN(ExtTextOutA) { ENTER; return ExtTextOutCommon(c, m, false); }
FN(ExtTextOutW) { ENTER; return ExtTextOutCommon(c, m, true); }
HostCallOutcome ExtentCommon(cpu::CpuState& c, cpu::IGuestMemory& m, bool unicode) {
    int w, h;
    g_gdi.TextExtent(A(c, m, 0), ReadCounted(m, A(c, m, 1), SA(c, m, 2), unicode), w, h);
    WritePoint(m, A(c, m, 3), w, h);
    return Ret(1);
}
FN(GetTextExtentPoint32A) { ENTER; return ExtentCommon(c, m, false); }
FN(GetTextExtentPoint32W) { ENTER; return ExtentCommon(c, m, true); }
HostCallOutcome ExtentExCommon(cpu::CpuState& c, cpu::IGuestMemory& m, bool unicode) {
    std::u16string s = ReadCounted(m, A(c, m, 1), SA(c, m, 2), unicode);
    int maxExt = SA(c, m, 3);
    uint32_t fitP = A(c, m, 4), dxP = A(c, m, 5), sizeP = A(c, m, 6);
    ui::Font* f = g_gdi.CurrentFont(A(c, m, 0));
    int x = 0, fit = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        x += f->Advance(s[i]);
        if (dxP) m.Write32(dxP + static_cast<uint32_t>(i) * 4, static_cast<uint32_t>(x));
        if (x <= maxExt) fit = static_cast<int>(i) + 1;
    }
    if (fitP) m.Write32(fitP, static_cast<uint32_t>(fit));
    WritePoint(m, sizeP, x, f->Metrics().height);
    return Ret(1);
}
FN(GetTextExtentExPointA) { ENTER; return ExtentExCommon(c, m, false); }
FN(GetTextExtentExPointW) { ENTER; return ExtentExCommon(c, m, true); }
HostCallOutcome TextMetricsCommon(cpu::CpuState& c, cpu::IGuestMemory& m, bool unicode) {
    ui::Font* f = g_gdi.CurrentFont(A(c, m, 0));
    const ui::TextMetrics& t = f->Metrics();
    uint32_t p = A(c, m, 1);
    for (uint32_t i = 0; i < (unicode ? 60u : 56u); i += 4) m.Write32(p + i, 0);
    m.Write32(p, static_cast<uint32_t>(t.height));
    m.Write32(p + 4, static_cast<uint32_t>(t.ascent));
    m.Write32(p + 8, static_cast<uint32_t>(t.descent));
    m.Write32(p + 12, static_cast<uint32_t>(t.internalLeading));
    m.Write32(p + 16, static_cast<uint32_t>(t.externalLeading));
    m.Write32(p + 20, static_cast<uint32_t>(t.aveCharWidth));
    m.Write32(p + 24, static_cast<uint32_t>(t.maxCharWidth));
    m.Write32(p + 28, static_cast<uint32_t>(t.weight));
    m.Write32(p + 36, 96);
    m.Write32(p + 40, 96);
    uint8_t pitch = static_cast<uint8_t>((t.pitchAndFamily & 0xF0) | (t.fixedPitch ? 0 : 1) | 0x06);
    if (unicode) {
        m.Write16(p + 44, 0x20); m.Write16(p + 46, 0xFFFC); m.Write16(p + 48, 0x1F); m.Write16(p + 50, 0x20);
        m.Write8(p + 52, t.italic); m.Write8(p + 53, t.underline); m.Write8(p + 54, t.strikeout);
        m.Write8(p + 55, pitch); m.Write8(p + 56, t.charset);
    } else {
        m.Write8(p + 44, 0x20); m.Write8(p + 45, 0xFF); m.Write8(p + 46, 0x1F); m.Write8(p + 47, 0x20);
        m.Write8(p + 48, t.italic); m.Write8(p + 49, t.underline); m.Write8(p + 50, t.strikeout);
        m.Write8(p + 51, pitch); m.Write8(p + 52, t.charset);
    }
    return Ret(1);
}
FN(GetTextMetricsA) { ENTER; return TextMetricsCommon(c, m, false); }
FN(GetTextMetricsW) { ENTER; return TextMetricsCommon(c, m, true); }
FN(GetCharWidthA) {
    ENTER;
    ui::Font* f = g_gdi.CurrentFont(A(c, m, 0));
    uint32_t first = A(c, m, 1), last = A(c, m, 2), p = A(c, m, 3);
    for (uint32_t ch = first; ch <= last && ch - first < 65536; ++ch) m.Write32(p + (ch - first) * 4, static_cast<uint32_t>(f->Advance(ch)));
    return Ret(1);
}
FN(GetCharABCWidthsA) {
    ENTER;
    ui::Font* f = g_gdi.CurrentFont(A(c, m, 0));
    uint32_t first = A(c, m, 1), last = A(c, m, 2), p = A(c, m, 3);
    for (uint32_t ch = first; ch <= last && ch - first < 65536; ++ch) {
        m.Write32(p + (ch - first) * 12, 0);
        m.Write32(p + (ch - first) * 12 + 4, static_cast<uint32_t>(f->Advance(ch)));
        m.Write32(p + (ch - first) * 12 + 8, 0);
    }
    return Ret(1);
}
HostCallOutcome TextFaceCommon(cpu::CpuState& c, cpu::IGuestMemory& m, bool unicode) {
    ui::Font* f = g_gdi.CurrentFont(A(c, m, 0));
    std::u16string name = AnsiToWide(f->Metrics().faceName);
    if (!A(c, m, 2)) return Ret(static_cast<uint32_t>(name.size() + 1));
    return Ret(user::WriteText(A(c, m, 2), name, A(c, m, 1), unicode));
}
FN(GetTextFaceA) { ENTER; return TextFaceCommon(c, m, false); }
FN(GetTextFaceW) { ENTER; return TextFaceCommon(c, m, true); }
// EnumFontFamilies: несколько стандартных семейств
HostCallOutcome EnumFontsCommon(cpu::CpuState& c, cpu::IGuestMemory& m, bool unicode, bool ex) {
    uint32_t proc = ex ? A(c, m, 2) : A(c, m, 2), lp = ex ? A(c, m, 3) : A(c, m, 3);
    std::u16string filter;
    if (ex) { if (A(c, m, 1)) filter = user::ReadText(A(c, m, 1) + 28, unicode); }
    else if (A(c, m, 1)) filter = user::ReadText(A(c, m, 1), unicode);
    static const char* const kFamilies[] = {"Arial", "Times New Roman", "Courier New", "MS Sans Serif", "Tahoma", "Verdana"};
    uint32_t r = 1;
    for (const char* fam : kFamilies) {
        std::u16string f = AnsiToWide(fam);
        if (!filter.empty() && filter != f) continue;
        ui::FontDesc d;
        d.face = fam;
        d.height = 16;
        d.pitchAndFamily = std::strcmp(fam, "Courier New") == 0 ? 0x31 : 0x22;
        user::StackBuffer lf(256), tm(128);
        WriteLogFont(m, lf.addr(), d, unicode, 1000);
        uint32_t lfSize = unicode ? 92 : 60;
        for (uint32_t i = lfSize; i < 256; ++i) m.Write8(lf.addr() + i, 0);
        user::WriteText(lf.addr() + lfSize, f, 64, unicode); // ENUMLOGFONT: полное имя
        for (uint32_t i = 0; i < 128; i += 4) m.Write32(tm.addr() + i, 0);
        m.Write32(tm.addr(), 16);
        r = CallStdcall(c, m, proc, {lf.addr(), tm.addr(), 4 /* TRUETYPE_FONTTYPE */, lp});
        if (!r) break;
    }
    return Ret(r);
}
FN(EnumFontFamiliesA) { ENTER; return EnumFontsCommon(c, m, false, false); }
FN(EnumFontFamiliesW) { ENTER; return EnumFontsCommon(c, m, true, false); }
FN(EnumFontFamiliesExA) { ENTER; return EnumFontsCommon(c, m, false, true); }
FN(EnumFontFamiliesExW) { ENTER; return EnumFontsCommon(c, m, true, true); }
FN(AddFontResourceA) { ENTER; return Ret(1); }

// --- Регионы ---

uint32_t RgnType(const Region& r) { return r.Empty() ? 1 : r.Rects().size() == 1 ? 2 : 3; }
Region Normalized(int l, int t, int r, int b) { return Region(Rect(std::min(l, r), std::min(t, b), std::max(l, r), std::max(t, b))); }
FN(CreateRectRgn) { ENTER; return Ret(g_gdi.CreateRegion(Normalized(SA(c, m, 0), SA(c, m, 1), SA(c, m, 2), SA(c, m, 3)))); }
FN(CreateRectRgnIndirect) { ENTER; Rect r = ReadRect(m, A(c, m, 0)); return Ret(g_gdi.CreateRegion(Normalized(r.left, r.top, r.right, r.bottom))); }
// Эллипс/скруглённый прямоугольник/многоугольник — построчно
Region RowsRegion(int l, int t, int r, int b, double rx, double ry) {
    Region out;
    for (int y = t; y < b; ++y) {
        double cy = y + 0.5, inset = 0;
        if (rx > 0 && ry > 0) {
            double dy = 0;
            if (cy < t + ry) dy = t + ry - cy;
            else if (cy > b - ry) dy = cy - (b - ry);
            if (dy > ry) continue;
            double q = dy / ry;
            inset = rx * (1 - std::sqrt(std::max(0.0, 1 - q * q)));
        }
        int x0 = static_cast<int>(std::lround(l + inset)), x1 = static_cast<int>(std::lround(r - inset));
        if (x1 > x0) out.UnionRect(Rect(x0, y, x1, y + 1));
    }
    return out;
}
FN(CreateEllipticRgn) {
    ENTER;
    int l = std::min(SA(c, m, 0), SA(c, m, 2)), r = std::max(SA(c, m, 0), SA(c, m, 2));
    int t = std::min(SA(c, m, 1), SA(c, m, 3)), b = std::max(SA(c, m, 1), SA(c, m, 3));
    return Ret(g_gdi.CreateRegion(RowsRegion(l, t, r, b, (r - l) / 2.0, (b - t) / 2.0)));
}
FN(CreateEllipticRgnIndirect) {
    ENTER;
    Rect q = ReadRect(m, A(c, m, 0));
    return Ret(g_gdi.CreateRegion(RowsRegion(q.left, q.top, q.right, q.bottom, q.Width() / 2.0, q.Height() / 2.0)));
}
FN(CreateRoundRectRgn) {
    ENTER;
    int l = std::min(SA(c, m, 0), SA(c, m, 2)), r = std::max(SA(c, m, 0), SA(c, m, 2));
    int t = std::min(SA(c, m, 1), SA(c, m, 3)), b = std::max(SA(c, m, 1), SA(c, m, 3));
    return Ret(g_gdi.CreateRegion(RowsRegion(l, t, r, b, std::min(SA(c, m, 4) / 2.0, (r - l) / 2.0), std::min(SA(c, m, 5) / 2.0, (b - t) / 2.0))));
}
FN(CreatePolygonRgn) {
    ENTER;
    std::vector<ui::Point> pts = ReadPoints(m, A(c, m, 0), A(c, m, 1));
    bool winding = A(c, m, 2) == 2;
    Region out;
    if (pts.size() < 3) return Ret(g_gdi.CreateRegion(out));
    int minY = pts[0].y, maxY = pts[0].y;
    for (auto& p : pts) { minY = std::min(minY, p.y); maxY = std::max(maxY, p.y); }
    for (int y = minY; y < maxY; ++y) {
        double sy = y + 0.5;
        std::vector<std::pair<double, int>> xs;
        for (size_t i = 0; i < pts.size(); ++i) {
            ui::Point a = pts[i], e = pts[(i + 1) % pts.size()];
            if (a.y == e.y) continue;
            int dir = e.y > a.y ? 1 : -1;
            if (a.y > e.y) std::swap(a, e);
            if (sy < a.y || sy >= e.y) continue;
            xs.push_back({a.x + (sy - a.y) * (e.x - a.x) / (e.y - a.y), dir});
        }
        std::sort(xs.begin(), xs.end());
        int wind = 0;
        for (size_t i = 0; i + 1 < xs.size(); ++i) {
            bool in = winding ? (wind += xs[i].second) != 0 : (i % 2) == 0;
            if (!in) continue;
            int x0 = static_cast<int>(std::ceil(xs[i].first - 0.5)), x1 = static_cast<int>(std::ceil(xs[i + 1].first - 0.5));
            if (x1 > x0) out.UnionRect(Rect(x0, y, x1, y + 1));
        }
    }
    return Ret(g_gdi.CreateRegion(out));
}
FN(CombineRgn) {
    ENTER;
    ui::RegionObj* d = g_gdi.Rgn(A(c, m, 0));
    ui::RegionObj* s1 = g_gdi.Rgn(A(c, m, 1));
    ui::RegionObj* s2 = g_gdi.Rgn(A(c, m, 2));
    if (!d || !s1) return Ret(0);
    Region r = s1->rgn;
    switch (A(c, m, 3)) {
    case 1: if (!s2) return Ret(0); r.Intersect(s2->rgn); break; // RGN_AND
    case 2: if (!s2) return Ret(0); r.Union(s2->rgn); break;     // RGN_OR
    case 3: if (!s2) return Ret(0); r.Xor(s2->rgn); break;       // RGN_XOR
    case 4: if (!s2) return Ret(0); r.Subtract(s2->rgn); break;  // RGN_DIFF
    case 5: break;                                                // RGN_COPY
    default: return Ret(0);
    }
    d->rgn = r;
    return Ret(RgnType(r));
}
FN(EqualRgn) {
    ENTER;
    ui::RegionObj* a = g_gdi.Rgn(A(c, m, 0));
    ui::RegionObj* b = g_gdi.Rgn(A(c, m, 1));
    return Ret(a && b && a->rgn == b->rgn ? 1 : 0);
}
FN(OffsetRgn) { ENTER; ui::RegionObj* r = g_gdi.Rgn(A(c, m, 0)); if (!r) return Ret(0); r->rgn.Offset(SA(c, m, 1), SA(c, m, 2)); return Ret(RgnType(r->rgn)); }
FN(GetRgnBox) { ENTER; ui::RegionObj* r = g_gdi.Rgn(A(c, m, 0)); if (!r) return Ret(0); WriteRect(m, A(c, m, 1), r->rgn.Bounds()); return Ret(RgnType(r->rgn)); }
FN(PtInRegion) { ENTER; ui::RegionObj* r = g_gdi.Rgn(A(c, m, 0)); return Ret(r && r->rgn.Contains(SA(c, m, 1), SA(c, m, 2)) ? 1 : 0); }
FN(RectInRegion) {
    ENTER;
    ui::RegionObj* r = g_gdi.Rgn(A(c, m, 0));
    if (!r) return Ret(0);
    Region t = r->rgn;
    t.IntersectRect(ReadRect(m, A(c, m, 1)));
    return Ret(t.Empty() ? 0 : 1);
}
FN(SetRectRgn) { ENTER; ui::RegionObj* r = g_gdi.Rgn(A(c, m, 0)); if (!r) return Ret(0); r->rgn = Normalized(SA(c, m, 1), SA(c, m, 2), SA(c, m, 3), SA(c, m, 4)); return Ret(1); }
FN(GetRegionData) {
    ENTER;
    ui::RegionObj* r = g_gdi.Rgn(A(c, m, 0));
    if (!r) return Ret(0);
    uint32_t n = static_cast<uint32_t>(r->rgn.Rects().size());
    uint32_t need = 32 + n * 16;
    uint32_t p = A(c, m, 2);
    if (!p || A(c, m, 1) < need) return Ret(need);
    m.Write32(p, 32);
    m.Write32(p + 4, 1);
    m.Write32(p + 8, n);
    m.Write32(p + 12, n * 16);
    WriteRect(m, p + 16, r->rgn.Bounds());
    for (uint32_t i = 0; i < n; ++i) WriteRect(m, p + 32 + i * 16, r->rgn.Rects()[i]);
    return Ret(need);
}

// Область отсечения контекста — в координатах устройства
Region DeviceRect(ui::DcObj* dc, int l, int t, int r, int b) {
    ui::Point a = g_gdi.ToDevice(*dc, l, t), e = g_gdi.ToDevice(*dc, r, b);
    return Normalized(a.x, a.y, e.x, e.y);
}
Region CurrentClip(uint32_t hdc, ui::DcObj* dc) {
    if (dc->st.hasClip) return dc->st.clip;
    int w, h;
    g_gdi.DeviceSize(hdc, w, h);
    if (dc->kind == ui::DcKind::Window) {
        if (user::Window* win = user::Win(dc->hwnd))
            return Region(Rect(0, 0, dc->wholeWindow ? win->rect.Width() : win->client.Width(), dc->wholeWindow ? win->rect.Height() : win->client.Height()));
    }
    return Region(Rect(0, 0, w, h));
}
FN(SelectClipRgn) {
    ENTER;
    ui::DcObj* dc = g_gdi.Dc(A(c, m, 0));
    if (!dc) return Ret(0);
    ui::RegionObj* r = g_gdi.Rgn(A(c, m, 1));
    if (!r) { dc->st.hasClip = false; dc->st.clip.Clear(); return Ret(2); }
    dc->st.hasClip = true;
    dc->st.clip = r->rgn;
    return Ret(RgnType(r->rgn));
}
FN(ExtSelectClipRgn) {
    ENTER;
    uint32_t hdc = A(c, m, 0);
    ui::DcObj* dc = g_gdi.Dc(hdc);
    if (!dc) return Ret(0);
    ui::RegionObj* r = g_gdi.Rgn(A(c, m, 1));
    uint32_t mode = A(c, m, 2);
    if (!r) {
        if (mode == 5) { dc->st.hasClip = false; dc->st.clip.Clear(); return Ret(2); }
        return Ret(0);
    }
    Region cur = CurrentClip(hdc, dc);
    switch (mode) {
    case 1: cur.Intersect(r->rgn); break;
    case 2: cur.Union(r->rgn); break;
    case 3: cur.Xor(r->rgn); break;
    case 4: cur.Subtract(r->rgn); break;
    default: cur = r->rgn; break;
    }
    dc->st.hasClip = true;
    dc->st.clip = cur;
    return Ret(RgnType(cur));
}
FN(IntersectClipRect) {
    ENTER;
    uint32_t hdc = A(c, m, 0);
    ui::DcObj* dc = g_gdi.Dc(hdc);
    if (!dc) return Ret(0);
    Region cur = CurrentClip(hdc, dc);
    cur.Intersect(DeviceRect(dc, SA(c, m, 1), SA(c, m, 2), SA(c, m, 3), SA(c, m, 4)));
    dc->st.hasClip = true;
    dc->st.clip = cur;
    return Ret(RgnType(cur));
}
FN(ExcludeClipRect) {
    ENTER;
    uint32_t hdc = A(c, m, 0);
    ui::DcObj* dc = g_gdi.Dc(hdc);
    if (!dc) return Ret(0);
    Region cur = CurrentClip(hdc, dc);
    cur.Subtract(DeviceRect(dc, SA(c, m, 1), SA(c, m, 2), SA(c, m, 3), SA(c, m, 4)));
    dc->st.hasClip = true;
    dc->st.clip = cur;
    return Ret(RgnType(cur));
}
FN(OffsetClipRgn) {
    ENTER;
    ui::DcObj* dc = g_gdi.Dc(A(c, m, 0));
    if (!dc) return Ret(0);
    if (dc->st.hasClip) dc->st.clip.Offset(SA(c, m, 1), SA(c, m, 2));
    return Ret(dc->st.hasClip ? RgnType(dc->st.clip) : 2);
}
FN(GetClipBox) {
    ENTER;
    uint32_t hdc = A(c, m, 0);
    ui::DcObj* dc = g_gdi.Dc(hdc);
    if (!dc) return Ret(0);
    Region cur = CurrentClip(hdc, dc);
    if (dc->paintDc) {
        // видимая часть области рисования BeginPaint, в координатах клиента
        Region pc = dc->paintClip;
        if (user::Window* w = user::Win(dc->hwnd)) pc.Offset(-user::ClientOriginX(w), -user::ClientOriginY(w));
        cur.Intersect(pc);
    }
    Rect b = cur.Bounds();
    ui::Point a = g_gdi.ToLogical(*dc, b.left, b.top), e = g_gdi.ToLogical(*dc, b.right, b.bottom);
    WriteRect(m, A(c, m, 1), Rect(std::min(a.x, e.x), std::min(a.y, e.y), std::max(a.x, e.x), std::max(a.y, e.y)));
    return Ret(RgnType(cur));
}
FN(GetClipRgn) {
    ENTER;
    ui::DcObj* dc = g_gdi.Dc(A(c, m, 0));
    ui::RegionObj* r = g_gdi.Rgn(A(c, m, 1));
    if (!dc || !r) return Ret(0xFFFFFFFFu);
    if (!dc->st.hasClip) return Ret(0);
    r->rgn = dc->st.clip;
    return Ret(1);
}
FN(PtVisible) {
    ENTER;
    uint32_t hdc = A(c, m, 0);
    ui::DcObj* dc = g_gdi.Dc(hdc);
    if (!dc) return Ret(0);
    ui::Point p = g_gdi.ToDevice(*dc, SA(c, m, 1), SA(c, m, 2));
    return Ret(CurrentClip(hdc, dc).Contains(p.x, p.y) ? 1 : 0);
}
FN(RectVisible) {
    ENTER;
    uint32_t hdc = A(c, m, 0);
    ui::DcObj* dc = g_gdi.Dc(hdc);
    if (!dc) return Ret(0);
    Rect r = ReadRect(m, A(c, m, 1));
    Region cur = CurrentClip(hdc, dc);
    cur.Intersect(DeviceRect(dc, r.left, r.top, r.right, r.bottom));
    return Ret(cur.Empty() ? 0 : 1);
}

// --- Палитры ---

FN(CreatePalette) {
    ENTER;
    uint32_t p = A(c, m, 0);
    uint32_t n = m.Read16(p + 2);
    std::vector<uint32_t> e;
    for (uint32_t i = 0; i < n && i < 1024; ++i) e.push_back(m.Read32(p + 4 + i * 4));
    return Ret(g_gdi.CreatePalette(e));
}
FN(SelectPalette) {
    ENTER;
    ui::DcObj* dc = g_gdi.Dc(A(c, m, 0));
    if (!dc || !g_gdi.Palette(A(c, m, 1))) return Ret(0);
    uint32_t old = dc->st.palette;
    dc->st.palette = A(c, m, 1);
    return Ret(old);
}
FN(RealizePalette) { ENTER; return Ret(0); }
FN(UnrealizeObject) { ENTER; return Ret(1); }
FN(GetPaletteEntries) {
    ENTER;
    ui::PaletteObj* p = g_gdi.Palette(A(c, m, 0));
    if (!p) return Ret(0);
    uint32_t start = A(c, m, 1), n = A(c, m, 2), out = A(c, m, 3);
    if (!out) return Ret(static_cast<uint32_t>(p->entries.size()));
    uint32_t k = 0;
    for (; k < n && start + k < p->entries.size(); ++k) m.Write32(out + k * 4, p->entries[start + k]);
    return Ret(k);
}
FN(SetPaletteEntries) {
    ENTER;
    ui::PaletteObj* p = g_gdi.Palette(A(c, m, 0));
    if (!p) return Ret(0);
    uint32_t start = A(c, m, 1), n = A(c, m, 2), in = A(c, m, 3);
    uint32_t k = 0;
    for (; k < n && start + k < p->entries.size(); ++k) p->entries[start + k] = m.Read32(in + k * 4);
    return Ret(k);
}
FN(GetSystemPaletteEntries) {
    ENTER;
    ui::PaletteObj* p = g_gdi.Palette(g_gdi.StockObject(15));
    uint32_t start = A(c, m, 1), n = A(c, m, 2), out = A(c, m, 3);
    if (!out) return Ret(256);
    for (uint32_t k = 0; k < n; ++k) {
        uint32_t i = start + k, v = 0;
        if (i < 10) v = p->entries[i];
        else if (i >= 246 && i - 236 < p->entries.size()) v = p->entries[i - 236];
        m.Write32(out + k * 4, v & 0xFFFFFF);
    }
    return Ret(n);
}
FN(GetNearestColor) { ENTER; return Ret(A(c, m, 1) & 0xFFFFFF); }
FN(GetNearestPaletteIndex) {
    ENTER;
    ui::PaletteObj* p = g_gdi.Palette(A(c, m, 0));
    if (!p || p->entries.empty()) return Ret(0xFFFFFFFFu);
    uint32_t col = A(c, m, 1);
    uint32_t best = 0, bestD = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < p->entries.size(); ++i) {
        uint32_t e = p->entries[i];
        int dr = static_cast<int>(e & 0xFF) - static_cast<int>(col & 0xFF);
        int dg = static_cast<int>((e >> 8) & 0xFF) - static_cast<int>((col >> 8) & 0xFF);
        int db = static_cast<int>((e >> 16) & 0xFF) - static_cast<int>((col >> 16) & 0xFF);
        uint32_t d = static_cast<uint32_t>(dr * dr + dg * dg + db * db);
        if (d < bestD) { bestD = d; best = i; }
    }
    return Ret(best);
}
FN(AnimatePalette) { ENTER; return Ret(1); }
FN(ResizePalette) {
    ENTER;
    ui::PaletteObj* p = g_gdi.Palette(A(c, m, 0));
    if (!p) return Ret(0);
    p->entries.resize(A(c, m, 1), 0);
    return Ret(1);
}

// --- Битмапы ---

FN(CreateCompatibleBitmap) {
    ENTER;
    int w = SA(c, m, 1), h = SA(c, m, 2);
    if (w <= 0 || h <= 0) return Ret(g_gdi.CreateBitmap(1, 1, 1));
    ui::DcObj* dc = g_gdi.Dc(A(c, m, 0));
    int bpp = 32;
    // совместимый с контекстом в памяти — формата выбранного в нём битмапа
    if (dc && dc->kind == ui::DcKind::Memory) if (ui::BitmapObj* b = g_gdi.Bitmap(dc->st.bitmap)) if (b->view.bpp == 1) bpp = 1;
    return Ret(g_gdi.CreateBitmap(w, h, bpp));
}
FN(CreateBitmap) {
    ENTER;
    int w = SA(c, m, 0), h = SA(c, m, 1);
    int bpp = static_cast<int>(A(c, m, 2) * A(c, m, 3));
    uint32_t bits = A(c, m, 4);
    std::vector<uint8_t> init;
    if (bits && w > 0 && h > 0) {
        init.resize(static_cast<size_t>(((w * bpp + 15) / 16) * 2) * h);
        m.ReadBytes(bits, init.data(), init.size());
    }
    return Ret(g_gdi.CreateBitmap(w, h, bpp, init.empty() ? nullptr : init.data()));
}
FN(CreateBitmapIndirect) {
    ENTER;
    uint32_t p = A(c, m, 0);
    int w = static_cast<int32_t>(m.Read32(p + 4)), h = static_cast<int32_t>(m.Read32(p + 8));
    int bpp = m.Read16(p + 16) * m.Read16(p + 18);
    uint32_t bits = m.Read32(p + 20);
    std::vector<uint8_t> init;
    if (bits && w > 0 && h > 0) {
        init.resize(static_cast<size_t>(((w * bpp + 15) / 16) * 2) * h);
        m.ReadBytes(bits, init.data(), init.size());
    }
    return Ret(g_gdi.CreateBitmap(w, h, bpp, init.empty() ? nullptr : init.data()));
}
FN(CreateDIBSection) {
    ENTER;
    uint32_t hdc = A(c, m, 0), bmi = A(c, m, 1), usage = A(c, m, 2), ppv = A(c, m, 3);
    DibInfo d = ReadDibInfo(m, bmi, usage, hdc);
    if (ppv) m.Write32(ppv, 0);
    if (!d.valid) return Ret(0);
    int h = d.height < 0 ? -d.height : d.height;
    uint32_t size = static_cast<uint32_t>(ui::Surface::DibPitch(d.width, d.bpp)) * static_cast<uint32_t>(h);
    uint32_t bits = m.Allocate(size, cpu::kProtRW);
    if (!bits) return Ret(0);
    DibView v;
    MakeDibView(m, bits, d, v);
    std::vector<uint8_t> header(40);
    for (uint32_t i = 0; i < 40; ++i) header[i] = d.headerSize >= 40 ? m.Read8(bmi + i) : 0;
    uint32_t hb = g_gdi.CreateDibSection(v.s, bits, header);
    if (ppv) m.Write32(ppv, bits);
    return Ret(hb);
}

// Копирование строк между битмапом и DIB гостя
uint32_t TransferDib(cpu::IGuestMemory& m, uint32_t hdc, uint32_t hbm, uint32_t start, uint32_t lines, uint32_t bits, uint32_t bmi,
                     uint32_t usage, bool toGuest) {
    ui::BitmapObj* b = g_gdi.Bitmap(hbm);
    if (!b || !bmi) return 0;
    const ui::Surface& s = b->view;
    if (toGuest && !bits) { // только заголовок
        if (m.Read16(bmi + 14) == 0) {
            m.Write32(bmi + 4, static_cast<uint32_t>(s.width));
            m.Write32(bmi + 8, static_cast<uint32_t>(s.height));
            m.Write16(bmi + 12, 1);
            m.Write16(bmi + 14, static_cast<uint16_t>(s.bpp));
            m.Write32(bmi + 16, 0);
        }
        int bpp = m.Read16(bmi + 14);
        m.Write32(bmi + 20, static_cast<uint32_t>(ui::Surface::DibPitch(s.width, bpp) * s.height));
        return static_cast<uint32_t>(s.height);
    }
    DibInfo d = ReadDibInfo(m, bmi, usage, hdc);
    if (toGuest && d.bpp <= 8) {
        // таблица цветов: палитра битмапа или стандартная
        uint32_t n = 1u << d.bpp;
        d.palette.clear();
        for (uint32_t i = 0; i < n; ++i) {
            uint32_t col = i < s.palette.size() ? s.palette[i] : (d.bpp == 1 ? (i ? 0xFFFFFF : 0) : ui::ColorrefToRgb(g_gdi.Palette(g_gdi.StockObject(15))->entries[i % 20] & 0xFFFFFF));
            d.palette.push_back(col);
            m.Write32(bmi + d.headerSize + i * 4, col);
        }
    }
    if (!d.valid) return 0;
    DibView v;
    if (!MakeDibView(m, bits, d, v)) return 0;
    int h = std::abs(d.height);
    uint32_t done = 0;
    for (uint32_t i = 0; i < lines; ++i) {
        // строка start+i считается снизу для DIB снизу вверх
        int dibRow = d.height > 0 ? h - 1 - static_cast<int>(start + i) : static_cast<int>(start + i);
        int bmpRow = d.height > 0 ? s.height - 1 - static_cast<int>(start + i) : static_cast<int>(start + i);
        if (dibRow < 0 || dibRow >= h || bmpRow < 0 || bmpRow >= s.height) continue;
        // в v.s строки уже сверху вниз
        int vr = d.height > 0 ? h - 1 - (h - 1 - dibRow) : dibRow;
        for (int x = 0; x < std::min(s.width, d.width); ++x) {
            if (toGuest) v.s.Set(x, vr, s.Get(x, bmpRow));
            else const_cast<ui::Surface&>(s).Set(x, bmpRow, v.s.Get(x, vr));
        }
        ++done;
    }
    if (toGuest) FlushDibView(m, v);
    return done;
}
FN(GetDIBits) { ENTER; return Ret(TransferDib(m, A(c, m, 0), A(c, m, 1), A(c, m, 2), A(c, m, 3), A(c, m, 4), A(c, m, 5), A(c, m, 6), true)); }
FN(SetDIBits) { ENTER; return Ret(TransferDib(m, A(c, m, 0), A(c, m, 1), A(c, m, 2), A(c, m, 3), A(c, m, 4), A(c, m, 5), A(c, m, 6), false)); }
FN(CreateDIBitmap) {
    ENTER;
    uint32_t hdc = A(c, m, 0), hdr = A(c, m, 1), init = A(c, m, 2), bits = A(c, m, 3), bmi = A(c, m, 4), usage = A(c, m, 5);
    if (!hdr) return Ret(0);
    int w = static_cast<int32_t>(m.Read32(hdr + 4)), h = std::abs(static_cast<int32_t>(m.Read32(hdr + 8)));
    int bpp = m.Read16(hdr + 14) == 1 ? 1 : 32;
    uint32_t hb = g_gdi.CreateBitmap(w, h, bpp);
    if (hb && (init & 4) && bits && bmi) TransferDib(m, hdc, hb, 0, static_cast<uint32_t>(h), bits, bmi, usage, false);
    return Ret(hb);
}
FN(GetBitmapBits) {
    ENTER;
    ui::BitmapObj* b = g_gdi.Bitmap(A(c, m, 0));
    if (!b) return Ret(0);
    const ui::Surface& s = b->view;
    int pitch = ((s.width * s.bpp + 15) / 16) * 2;
    uint32_t total = std::min<uint32_t>(A(c, m, 1), static_cast<uint32_t>(pitch * s.height));
    uint32_t out = A(c, m, 2);
    for (uint32_t i = 0; i < total; ++i) {
        int y = static_cast<int>(i / static_cast<uint32_t>(pitch)), xb = static_cast<int>(i % static_cast<uint32_t>(pitch));
        uint8_t v = (xb < std::abs(s.pitch)) ? s.Row(y)[xb] : 0;
        m.Write8(out + i, v);
    }
    return Ret(total);
}
FN(SetBitmapBits) {
    ENTER;
    ui::BitmapObj* b = g_gdi.Bitmap(A(c, m, 0));
    if (!b) return Ret(0);
    ui::Surface& s = b->view;
    int pitch = ((s.width * s.bpp + 15) / 16) * 2;
    uint32_t total = std::min<uint32_t>(A(c, m, 1), static_cast<uint32_t>(pitch * s.height));
    uint32_t in = A(c, m, 2);
    for (uint32_t i = 0; i < total; ++i) {
        int y = static_cast<int>(i / static_cast<uint32_t>(pitch)), xb = static_cast<int>(i % static_cast<uint32_t>(pitch));
        if (xb < std::abs(s.pitch)) s.Row(y)[xb] = m.Read8(in + i);
    }
    return Ret(total);
}
FN(SetDIBitsToDevice) {
    ENTER;
    uint32_t hdc = A(c, m, 0);
    int xd = SA(c, m, 1), yd = SA(c, m, 2), w = SA(c, m, 3), h = SA(c, m, 4), xs = SA(c, m, 5), ys = SA(c, m, 6);
    uint32_t lines = A(c, m, 8), bits = A(c, m, 9), bmi = A(c, m, 10), usage = A(c, m, 11);
    DibInfo d = ReadDibInfo(m, bmi, usage, hdc);
    DibView v;
    if (!MakeDibView(m, bits, d, v)) return Ret(0);
    int H = std::abs(d.height);
    int top = d.height > 0 ? H - ys - h : ys; // для DIB снизу вверх ys отсчитывается снизу
    g_gdi.BlitFromSurface(hdc, xd, yd, w, h, v.s, xs, top, w, h, 0xCC0020);
    return Ret(lines);
}
FN(StretchDIBits) {
    ENTER;
    uint32_t hdc = A(c, m, 0);
    int xd = SA(c, m, 1), yd = SA(c, m, 2), wd = SA(c, m, 3), hd = SA(c, m, 4);
    int xs = SA(c, m, 5), ys = SA(c, m, 6), ws = SA(c, m, 7), hs = SA(c, m, 8);
    uint32_t bits = A(c, m, 9), bmi = A(c, m, 10), usage = A(c, m, 11), rop = A(c, m, 12);
    DibInfo d = ReadDibInfo(m, bmi, usage, hdc);
    DibView v;
    if (!MakeDibView(m, bits, d, v)) return Ret(0);
    int H = std::abs(d.height);
    int top = d.height > 0 ? H - ys - hs : ys;
    if (d.height > 0 && hs < 0) top = H - ys;
    g_gdi.BlitFromSurface(hdc, xd, yd, wd, hd, v.s, xs, top, ws, hs, rop);
    return Ret(static_cast<uint32_t>(std::abs(hs)));
}
FN(GetDIBColorTable) {
    ENTER;
    ui::DcObj* dc = g_gdi.Dc(A(c, m, 0));
    ui::BitmapObj* b = dc ? g_gdi.Bitmap(dc->st.bitmap) : nullptr;
    if (!b) return Ret(0);
    uint32_t start = A(c, m, 1), n = A(c, m, 2), out = A(c, m, 3), k = 0;
    for (; k < n && start + k < b->view.palette.size(); ++k) m.Write32(out + k * 4, b->view.palette[start + k]);
    return Ret(k);
}
FN(SetDIBColorTable) {
    ENTER;
    ui::DcObj* dc = g_gdi.Dc(A(c, m, 0));
    ui::BitmapObj* b = dc ? g_gdi.Bitmap(dc->st.bitmap) : nullptr;
    if (!b || b->view.bpp > 8) return Ret(0);
    uint32_t start = A(c, m, 1), n = A(c, m, 2), in = A(c, m, 3), k = 0;
    if (b->view.palette.size() < (1u << b->view.bpp)) b->view.palette.resize(1u << b->view.bpp, 0);
    for (; k < n && start + k < b->view.palette.size(); ++k) b->view.palette[start + k] = m.Read32(in + k * 4) & 0xFFFFFF;
    return Ret(k);
}
FN(GetBitmapDimensionEx) { ENTER; WritePoint(m, A(c, m, 1), 0, 0); return Ret(1); }

// Печать не поддерживается
FN(StartDocA) { ENTER; return Ret(0); }
FN(GdiFailZero) { ENTER; return Ret(0); }

#define E(name, bytes) {"GDI32.dll", #name, bytes, CallConv::Stdcall, &name}
const HostFunctionEntry kEntries[] = {
    E(CreatePen, 12), E(CreatePenIndirect, 4), E(ExtCreatePen, 20), E(CreateSolidBrush, 4), E(CreateHatchBrush, 8),
    E(CreatePatternBrush, 4), E(CreateDIBPatternBrushPt, 8), E(CreateBrushIndirect, 4), E(GetStockObject, 4), E(SelectObject, 8),
    E(DeleteObject, 4), E(GetObjectType, 4), E(GetCurrentObject, 8), E(GetObjectA, 12), E(GetObjectW, 12),
    E(CreateFontA, 56), E(CreateFontW, 56), E(CreateFontIndirectA, 4), E(CreateFontIndirectW, 4),
    E(CreateCompatibleDC, 4), E(CreateDCA, 16), E(CreateDCW, 16), E(CreateICA, 16), E(CreateICW, 16), E(DeleteDC, 4),
    E(SaveDC, 4), E(RestoreDC, 8), E(GetDeviceCaps, 8),
    E(SetTextColor, 8), E(GetTextColor, 4), E(SetBkColor, 8), E(GetBkColor, 4), E(SetBkMode, 8), E(GetBkMode, 4),
    E(SetROP2, 8), E(GetROP2, 4), E(SetTextAlign, 8), E(GetTextAlign, 4), E(SetStretchBltMode, 8), E(GetStretchBltMode, 4),
    E(SetPolyFillMode, 8), E(GetPolyFillMode, 4), E(SetTextCharacterExtra, 8), E(GetTextCharacterExtra, 4),
    E(SetArcDirection, 8), E(GetArcDirection, 4), E(SetMapMode, 8), E(GetMapMode, 4), E(SetViewportOrgEx, 16),
    E(GetViewportOrgEx, 8), E(SetWindowOrgEx, 16), E(GetWindowOrgEx, 8), E(OffsetViewportOrgEx, 16), E(OffsetWindowOrgEx, 16),
    E(SetViewportExtEx, 16), E(SetWindowExtEx, 16), E(GetViewportExtEx, 8), E(GetWindowExtEx, 8), E(DPtoLP, 12), E(LPtoDP, 12),
    E(SetBrushOrgEx, 16), E(GetBrushOrgEx, 8), E(GetCurrentPositionEx, 8), E(GetDCOrgEx, 8), E(SetGraphicsMode, 8),
    E(GetGraphicsMode, 4), E(SetLayout, 8), E(GetLayout, 4), E(SetMapperFlags, 8), E(SetDCBrushColor, 8), E(SetDCPenColor, 8),
    E(GdiFlush, 0), E(GdiSetBatchLimit, 4),
    E(SetPixel, 16), E(SetPixelV, 16), E(GetPixel, 12), E(MoveToEx, 16), E(LineTo, 12), E(Polyline, 12), E(PolylineTo, 12),
    E(PolyPolyline, 16), E(Polygon, 12), E(PolyPolygon, 16), E(Rectangle, 20), E(RoundRect, 28), E(Ellipse, 20), E(Arc, 36),
    E(Chord, 36), E(Pie, 36), E(PatBlt, 24), E(BitBlt, 36), E(StretchBlt, 44), E(MaskBlt, 48), E(FillRgn, 12), E(PaintRgn, 8),
    E(FrameRgn, 20), E(InvertRgn, 8), E(ExtFloodFill, 20), E(FloodFill, 16),
    {"GDI32.dll", "GdiTransparentBlt", 44, CallConv::Stdcall, &TransparentBlt},
    {"MSIMG32.dll", "TransparentBlt", 44, CallConv::Stdcall, &TransparentBlt},
    E(TextOutA, 20), E(TextOutW, 20), E(ExtTextOutA, 32), E(ExtTextOutW, 32), E(GetTextExtentPoint32A, 16),
    E(GetTextExtentPoint32W, 16), {"GDI32.dll", "GetTextExtentPointA", 16, CallConv::Stdcall, &GetTextExtentPoint32A},
    {"GDI32.dll", "GetTextExtentPointW", 16, CallConv::Stdcall, &GetTextExtentPoint32W}, E(GetTextExtentExPointA, 28),
    E(GetTextExtentExPointW, 28), E(GetTextMetricsA, 8), E(GetTextMetricsW, 8), E(GetCharWidthA, 16),
    {"GDI32.dll", "GetCharWidthW", 16, CallConv::Stdcall, &GetCharWidthA},
    {"GDI32.dll", "GetCharWidth32A", 16, CallConv::Stdcall, &GetCharWidthA},
    {"GDI32.dll", "GetCharWidth32W", 16, CallConv::Stdcall, &GetCharWidthA}, E(GetCharABCWidthsA, 16),
    {"GDI32.dll", "GetCharABCWidthsW", 16, CallConv::Stdcall, &GetCharABCWidthsA}, E(GetTextFaceA, 12), E(GetTextFaceW, 12),
    E(EnumFontFamiliesA, 16), E(EnumFontFamiliesW, 16), E(EnumFontFamiliesExA, 20), E(EnumFontFamiliesExW, 20),
    {"GDI32.dll", "EnumFontsA", 16, CallConv::Stdcall, &EnumFontFamiliesA},
    {"GDI32.dll", "EnumFontsW", 16, CallConv::Stdcall, &EnumFontFamiliesW}, E(AddFontResourceA, 4),
    {"GDI32.dll", "AddFontResourceW", 4, CallConv::Stdcall, &AddFontResourceA},
    E(CreateRectRgn, 16), E(CreateRectRgnIndirect, 4), E(CreateEllipticRgn, 16), E(CreateEllipticRgnIndirect, 4),
    E(CreateRoundRectRgn, 24), E(CreatePolygonRgn, 12), E(CombineRgn, 16), E(EqualRgn, 8), E(OffsetRgn, 12), E(GetRgnBox, 8),
    E(PtInRegion, 12), E(RectInRegion, 8), E(SetRectRgn, 20), E(GetRegionData, 12), E(SelectClipRgn, 8), E(ExtSelectClipRgn, 12),
    E(IntersectClipRect, 20), E(ExcludeClipRect, 20), E(OffsetClipRgn, 12), E(GetClipBox, 8), E(GetClipRgn, 8),
    E(PtVisible, 12), E(RectVisible, 8),
    E(CreatePalette, 4), E(SelectPalette, 12), E(RealizePalette, 4), E(UnrealizeObject, 4), E(GetPaletteEntries, 16),
    E(SetPaletteEntries, 16), E(GetSystemPaletteEntries, 16), E(GetNearestColor, 8), E(GetNearestPaletteIndex, 8),
    E(AnimatePalette, 16), E(ResizePalette, 8),
    {"USER32.dll", "SelectPalette", 12, CallConv::Stdcall, &SelectPalette},
    {"USER32.dll", "RealizePalette", 4, CallConv::Stdcall, &RealizePalette},
    E(CreateCompatibleBitmap, 12), E(CreateBitmap, 20), E(CreateBitmapIndirect, 4), E(CreateDIBSection, 24),
    E(GetDIBits, 28), E(SetDIBits, 28), E(CreateDIBitmap, 24), E(GetBitmapBits, 12), E(SetBitmapBits, 12),
    E(SetDIBitsToDevice, 48), E(StretchDIBits, 52), E(GetDIBColorTable, 16), E(SetDIBColorTable, 16),
    E(GetBitmapDimensionEx, 8),
    E(StartDocA, 8), {"GDI32.dll", "StartDocW", 8, CallConv::Stdcall, &StartDocA},
    {"GDI32.dll", "StartPage", 4, CallConv::Stdcall, &GdiFailZero}, {"GDI32.dll", "EndPage", 4, CallConv::Stdcall, &GdiFailZero},
    {"GDI32.dll", "EndDoc", 4, CallConv::Stdcall, &GdiFailZero}, {"GDI32.dll", "AbortDoc", 4, CallConv::Stdcall, &GdiFailZero},
    {"GDI32.dll", "Escape", 20, CallConv::Stdcall, &GdiFailZero}, {"GDI32.dll", "ExtEscape", 24, CallConv::Stdcall, &GdiFailZero},
    {"GDI32.dll", "SetAbortProc", 8, CallConv::Stdcall, &GdiFailZero},
};
const bool kRegistered = RegisterHostFunctionsLayer(kEntries, sizeof(kEntries) / sizeof(kEntries[0]), kUiLayer);

} // namespace
} // namespace gdi32
} // namespace win32
