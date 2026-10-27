// gdi.cpp — объекты GDI, контексты устройств и растеризация (см. gdi.h)

#include "gdi.h"
#include "raster_identity.h"
#include "performance_metrics.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ui {

constexpr double kPi = 3.14159265358979323846;

Gdi g_gdi;

namespace {

// Биты типа в хендле — как в Windows (старшие биты HGDIOBJ)
uint32_t TypeBits(ObjType t) {
    switch (t) {
    case ObjType::DC: case ObjType::MemDC: return 0x01;
    case ObjType::Region: return 0x04;
    case ObjType::Bitmap: return 0x05;
    case ObjType::Palette: return 0x08;
    case ObjType::Font: return 0x0A;
    case ObjType::Brush: return 0x10;
    case ObjType::Pen: return 0x30;
    case ObjType::ExtPen: return 0x50;
    default: return 0;
    }
}

uint32_t Rop2(uint32_t p, uint32_t d, int code) {
    uint32_t t = static_cast<uint32_t>(code - 1) & 15, r = 0;
    if (t & 1) r |= ~p & ~d;
    if (t & 2) r |= ~p & d;
    if (t & 4) r |= p & ~d;
    if (t & 8) r |= p & d;
    return r & 0xFFFFFFu;
}

uint32_t Rop3(uint32_t p, uint32_t s, uint32_t d, uint32_t code) {
    switch (code) {
    case 0xCC: return s;
    case 0xF0: return p;
    case 0x00: return 0;
    case 0xFF: return 0xFFFFFF;
    case 0x66: return s ^ d;
    case 0x88: return s & d;
    case 0xEE: return s | d;
    case 0x55: return ~d & 0xFFFFFF;
    case 0x33: return ~s & 0xFFFFFF;
    case 0x5A: return p ^ d;
    }
    uint32_t r = 0;
    for (int i = 0; i < 8; ++i) {
        if (!(code & (1u << i))) continue;
        r |= ((i & 4) ? p : ~p) & ((i & 2) ? s : ~s) & ((i & 1) ? d : ~d);
    }
    return r & 0xFFFFFFu;
}

bool RopUsesSource(uint32_t code) { return ((code >> 2) & 0x33) != (code & 0x33); }
bool RopUsesPattern(uint32_t code) { return ((code >> 4) & 0x0F) != (code & 0x0F); }

bool Hatch(int style, int x, int y) {
    x &= 7;
    y &= 7;
    switch (style) {
    case 0: return y == 4;                         // HS_HORIZONTAL
    case 1: return x == 4;                         // HS_VERTICAL
    case 2: return x == y;                         // HS_FDIAGONAL
    case 3: return x == 7 - y;                     // HS_BDIAGONAL
    case 4: return x == 4 || y == 4;               // HS_CROSS
    case 5: return x == y || x == 7 - y;           // HS_DIAGCROSS
    }
    return false;
}

const uint32_t kSysColorsDefault[31] = {
    0xC8D0D4, 0xA56E3A, 0x6A240A, 0x808080, 0xC8D0D4, 0xFFFFFF, 0x000000, 0x000000,
    0x000000, 0xFFFFFF, 0xC8D0D4, 0xC8D0D4, 0x808080, 0x6A240A, 0xFFFFFF, 0xC8D0D4,
    0x808080, 0x808080, 0x000000, 0xC8D0D4, 0xFFFFFF, 0x404040, 0xC8D0D4, 0x000000,
    0xE1FFFF, 0xB5B5B5, 0x800000, 0xF0CAA6, 0xC0C0C0, 0xC56A31, 0xD8E9EC,
}; // COLORREF (0x00BBGGRR)


} // namespace

uint32_t SysColor(int index) {
    return (index >= 0 && index < 31) ? g_gdi.sysColors_[index] : 0;
}
void SetSysColor(int index, uint32_t c) {
    if (index >= 0 && index < 31) g_gdi.sysColors_[index] = c & 0xFFFFFF;
}

// ---------------------------------------------------------------------------
// Объекты

Gdi::Gdi() {
    std::memcpy(sysColors_,kSysColorsDefault,sizeof(sysColors_));
    slots_.resize(1); // индекс 0 не используется
    auto brush = [&](uint32_t c, int style = 0) { BrushObj b; b.color = c; b.style = style; return CreateBrush(b); };
    stock_[0] = brush(0xFFFFFF);
    stock_[1] = brush(0xC0C0C0);
    stock_[2] = brush(0x808080);
    stock_[3] = brush(0x404040);
    stock_[4] = brush(0x000000);
    stock_[5] = brush(0, 1);
    stock_[6] = CreatePen(0, 1, 0xFFFFFF);
    stock_[7] = CreatePen(0, 1, 0x000000);
    stock_[8] = CreatePen(5, 1, 0);
    FontDesc f;
    f.face = "Courier New"; f.height = 12; f.pitchAndFamily = 0x31;
    stock_[10] = CreateFont(f);              // OEM_FIXED_FONT
    stock_[11] = CreateFont(f);              // ANSI_FIXED_FONT
    f.face = "MS Sans Serif"; f.pitchAndFamily = 0x22; f.height = 13;
    stock_[12] = CreateFont(f);              // ANSI_VAR_FONT
    f.face = "System"; f.height = 16; f.weight = 700;
    stock_[13] = CreateFont(f);              // SYSTEM_FONT
    stock_[14] = CreateFont(f);              // DEVICE_DEFAULT_FONT
    stock_[15] = CreatePalette({0xFF000000u, 0xFF000080u, 0xFF008000u, 0xFF008080u, 0xFF800000u, 0xFF800080u,
                                0xFF808000u, 0xFFC0C0C0u, 0xFFC0DCC0u, 0xFFF0CAA6u, 0xFFF0FBFFu, 0xFFA4A0A0u,
                                0xFF808080u, 0xFF0000FFu, 0xFF00FF00u, 0xFF00FFFFu, 0xFFFF0000u, 0xFFFF00FFu,
                                0xFFFFFF00u, 0xFFFFFFFFu});
    f.face = "Courier New"; f.weight = 700; f.height = 16; f.pitchAndFamily = 0x31;
    stock_[16] = CreateFont(f);              // SYSTEM_FIXED_FONT
    f.face = "MS Shell Dlg"; f.weight = 400; f.height = -11; f.pitchAndFamily = 0;
    stock_[17] = CreateFont(f);              // DEFAULT_GUI_FONT
    stock_[18] = brush(0xFFFFFF);            // DC_BRUSH
    stock_[19] = CreatePen(0, 1, 0);         // DC_PEN
    defaultBitmap_ = CreateBitmap(1, 1, 1);
}

uint32_t Gdi::Alloc(ObjType t, Slot*& slot) {
    uint32_t idx;
    if (!free_.empty()) { idx = free_.back(); free_.pop_back(); }
    else { idx = static_cast<uint32_t>(slots_.size()); slots_.emplace_back(); }
    if (idx > 0xFFFF) { slot = nullptr; return 0; }
    slot = &slots_[idx];
    slot->type = t;
    slot->seq = static_cast<uint16_t>((slot->seq + 1) & 0x7F);
    if (!slot->seq) slot->seq = 1;
    return (static_cast<uint32_t>(slot->seq) << 24) | (TypeBits(t) << 16) | idx;
}

Gdi::Slot* Gdi::Find(uint32_t h) {
    uint32_t idx = h & 0xFFFF;
    if (!idx || idx >= slots_.size()) return nullptr;
    Slot& s = slots_[idx];
    if (s.type == ObjType::None || (h >> 24) != s.seq || ((h >> 16) & 0xFF) != TypeBits(s.type)) return nullptr;
    return &s;
}
const Gdi::Slot* Gdi::Find(uint32_t h) const { return const_cast<Gdi*>(this)->Find(h); }

ObjType Gdi::TypeOf(uint32_t h) const {
    const Slot* s = Find(h);
    return s ? s->type : ObjType::None;
}
PenObj* Gdi::Pen(uint32_t h) { Slot* s = Find(h); return s && (s->type == ObjType::Pen || s->type == ObjType::ExtPen) ? s->pen.get() : nullptr; }
BrushObj* Gdi::Brush(uint32_t h) { Slot* s = Find(h); return s && s->type == ObjType::Brush ? s->brush.get() : nullptr; }
BitmapObj* Gdi::Bitmap(uint32_t h) { Slot* s = Find(h); return s && s->type == ObjType::Bitmap ? s->bitmap.get() : nullptr; }
RegionObj* Gdi::Rgn(uint32_t h) { Slot* s = Find(h); return s && s->type == ObjType::Region ? s->region.get() : nullptr; }
PaletteObj* Gdi::Palette(uint32_t h) { Slot* s = Find(h); return s && s->type == ObjType::Palette ? s->palette.get() : nullptr; }
FontObj* Gdi::FontO(uint32_t h) { Slot* s = Find(h); return s && s->type == ObjType::Font ? s->font.get() : nullptr; }
DcObj* Gdi::Dc(uint32_t h) { Slot* s = Find(h); return s && (s->type == ObjType::DC || s->type == ObjType::MemDC) ? s->dc.get() : nullptr; }

uint32_t Gdi::CreatePen(int style, int width, uint32_t color) {
    Slot* s;
    uint32_t h = Alloc(ObjType::Pen, s);
    if (!h) return 0;
    s->pen.reset(new PenObj{style & 0xF, width < 1 ? 1 : width, color & 0xFFFFFF});
    return h;
}

uint32_t Gdi::CreateBrush(const BrushObj& b) {
    Slot* s;
    uint32_t h = Alloc(ObjType::Brush, s);
    if (!h) return 0;
    s->brush.reset(new BrushObj);
    BrushObj& d = *s->brush;
    d.style = b.style;
    d.color = b.color & 0xFFFFFF;
    d.hatch = b.hatch;
    d.patternBitmap = b.patternBitmap;
    d.sysColorIndex = b.sysColorIndex;
    d.pattern.s.width = b.pattern.s.width;
    d.pattern.s.height = b.pattern.s.height;
    d.pattern.s.bpp = b.pattern.s.bpp;
    d.pattern.s.pitch = b.pattern.s.pitch;
    d.pattern.s.palette = b.pattern.s.palette;
    d.pattern.storage = b.pattern.storage;
    d.pattern.s.bits = d.pattern.storage.empty() ? nullptr : d.pattern.storage.data();
    return h;
}

uint32_t Gdi::CreateFont(const FontDesc& desc) {
    Slot* s;
    uint32_t h = Alloc(ObjType::Font, s);
    if (!h) return 0;
    s->font.reset(new FontObj{desc, nullptr});
    return h;
}

uint32_t Gdi::CreateBitmap(int w, int h, int bpp, const void* initBits) {
    if (w <= 0 || h <= 0 || w > 16384 || h > 16384) return 0;
    if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 16 && bpp != 24) bpp = 32;
    Slot* s;
    uint32_t hb = Alloc(ObjType::Bitmap, s);
    if (!hb) return 0;
    s->bitmap.reset(new BitmapObj);
    BitmapObj& b = *s->bitmap;
    b.owned.Allocate(w, h, bpp);
    if (bpp == 1) b.owned.s.palette = {0x000000, 0xFFFFFF};
    if (initBits) {
        // CreateBitmap: строки выровнены на 2 байта
        int srcPitch = ((w * bpp + 15) / 16) * 2;
        const uint8_t* src = static_cast<const uint8_t*>(initBits);
        int rowBytes = std::min(srcPitch, b.owned.s.pitch);
        for (int y = 0; y < h; ++y) std::memcpy(b.owned.s.Row(y), src + static_cast<size_t>(y) * srcPitch, rowBytes);
    }
    b.view = b.owned.s;
    return hb;
}

uint32_t Gdi::CreateDibSection(const Surface& view, uint32_t guestBits, const std::vector<uint8_t>& header) {
    Slot* s;
    uint32_t hb = Alloc(ObjType::Bitmap, s);
    if (!hb) return 0;
    s->bitmap.reset(new BitmapObj);
    s->bitmap->view = view;
    s->bitmap->isDib = true;
    s->bitmap->guestBits = guestBits;
    s->bitmap->infoHeader = header;
    return hb;
}

uint32_t Gdi::CreateRegion(const Region& r) {
    Slot* s;
    uint32_t h = Alloc(ObjType::Region, s);
    if (!h) return 0;
    s->region.reset(new RegionObj{r});
    return h;
}

uint32_t Gdi::CreatePalette(const std::vector<uint32_t>& entries) {
    Slot* s;
    uint32_t h = Alloc(ObjType::Palette, s);
    if (!h) return 0;
    s->palette.reset(new PaletteObj{entries});
    return h;
}

uint32_t Gdi::StockObject(int index) const { return (index >= 0 && index < 20) ? stock_[index] : 0; }

bool Gdi::IsStock(uint32_t h) const {
    if (h == defaultBitmap_) return true;
    for (uint32_t s : stock_) if (s && s == h) return true;
    for (uint32_t s : extraStock_) if (s == h) return true;
    return false;
}

bool Gdi::DeleteObject(uint32_t h) {
    Slot* s = Find(h);
    if (!s || s->type == ObjType::DC || s->type == ObjType::MemDC) return false;
    if (IsStock(h)) return true;
    if (s->type == ObjType::Bitmap && s->bitmap->selectedIn > 0) return false; // как в Windows: выбранный битмап не удаляется
    { uint16_t seq = s->seq; *s = Slot(); s->seq = seq; }
    free_.push_back(h & 0xFFFF);
    return true;
}

uint32_t Gdi::CreateDc(DcKind kind, uint32_t hwnd, bool wholeWindow) {
    Slot* s;
    uint32_t h = Alloc(kind == DcKind::Memory ? ObjType::MemDC : ObjType::DC, s);
    if (!h) return 0;
    s->dc.reset(new DcObj);
    DcObj& d = *s->dc;
    d.identity=detail::NextRasterIdentity();
    if(kind==DcKind::Window && provider_) {
        WindowDcInfo info;info.identityOnly=true;
        if(provider_(hwnd,wholeWindow,info))d.windowIdentity=info.windowIdentity;
    }
    d.kind = kind;
    d.hwnd = hwnd;
    d.wholeWindow = wholeWindow;
    d.st.pen = stock_[7];
    d.st.brush = stock_[0];
    d.st.font = stock_[13];
    d.st.palette = stock_[15];
    if (kind == DcKind::Memory) {
        d.st.bitmap = defaultBitmap_;
        if (BitmapObj* b = Bitmap(defaultBitmap_)) b->selectedIn++;
    }
    return h;
}

uint32_t Gdi::CreateCompatibleDc(uint32_t) { return CreateDc(DcKind::Memory); }

bool Gdi::DescribeWindowTarget(uint32_t hdc,WindowRasterTarget& out) {
    out={};
    const auto* dc=Dc(hdc);
    if(!dc || dc->kind!=DcKind::Window || !dc->identity || !dc->windowIdentity ||
       dc->st.mapMode!=1 || dc->st.layout!=0 || !provider_)return false;
    WindowDcInfo info;info.identityOnly=true;info.expectedWindowIdentity=dc->windowIdentity;
    if(!provider_(dc->hwnd,dc->wholeWindow,info) || info.windowIdentity!=dc->windowIdentity)return false;
    Surface* surface=nullptr;int x=0,y=0;Region clip;
    if(!ResolveTarget(hdc,surface,x,y,clip,nullptr))return false; // geometry only, no CPU pixel access
    out.windowIdentity=info.windowIdentity;out.dcIdentity=dc->identity;
    out.layoutGeneration=layoutGeneration_;out.hwnd=dc->hwnd;out.hdc=hdc;
    out.frame=info.frame;out.originX=x;out.originY=y;out.clip=std::move(clip);
    out.windowOrg=dc->st.windowOrg;out.viewportOrg=dc->st.viewportOrg;
    out.wholeWindow=dc->wholeWindow;return true;
}


bool Gdi::DeleteDc(uint32_t hdc) {
    if(!PrepareCpuAccess())return false;
    Slot* s = Find(hdc);
    if (!s || (s->type != ObjType::DC && s->type != ObjType::MemDC)) return false;
    if (BitmapObj* b = Bitmap(s->dc->st.bitmap)) if (b->selectedIn > 0) b->selectedIn--;
    { uint16_t seq = s->seq; *s = Slot(); s->seq = seq; }
    free_.push_back(hdc & 0xFFFF);
    return true;
}

uint32_t Gdi::SelectObject(uint32_t hdc, uint32_t obj) {
    DcObj* dc = Dc(hdc);
    if (!dc) return 0;
    switch (TypeOf(obj)) {
    case ObjType::Pen: case ObjType::ExtPen: { uint32_t old = dc->st.pen; dc->st.pen = obj; return old; }
    case ObjType::Brush: { uint32_t old = dc->st.brush; dc->st.brush = obj; return old; }
    case ObjType::Font: { uint32_t old = dc->st.font; dc->st.font = obj; return old; }
    case ObjType::Bitmap: {
        if (dc->kind != DcKind::Memory) return 0;
        uint32_t old = dc->st.bitmap;
        if (BitmapObj* b = Bitmap(old)) if (b->selectedIn > 0) b->selectedIn--;
        dc->st.bitmap = obj;
        Bitmap(obj)->selectedIn++;
        return old;
    }
    case ObjType::Region: {
        RegionObj* r = Rgn(obj);
        dc->st.hasClip = true;
        dc->st.clip = r->rgn;
        return r->rgn.Empty() ? 1 : r->rgn.Rects().size() == 1 ? 2 : 3;
    }
    default: return 0;
    }
}

int Gdi::SaveDc(uint32_t hdc) {
    DcObj* dc = Dc(hdc);
    if (!dc) return 0;
    dc->saved.push_back(dc->st);
    return static_cast<int>(dc->saved.size());
}

bool Gdi::RestoreDc(uint32_t hdc, int level) {
    DcObj* dc = Dc(hdc);
    if (!dc) return false;
    int n = static_cast<int>(dc->saved.size());
    if (level < 0) level = n + 1 + level;
    if (level < 1 || level > n) return false;
    uint32_t oldBmp = dc->st.bitmap;
    dc->st = dc->saved[level - 1];
    dc->saved.resize(level - 1);
    if (oldBmp != dc->st.bitmap) {
        if (BitmapObj* b = Bitmap(oldBmp)) if (b->selectedIn > 0) b->selectedIn--;
        if (BitmapObj* b = Bitmap(dc->st.bitmap)) b->selectedIn++;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Координаты

Point Gdi::ToDevice(const DcObj& dc, int x, int y) const {
    const DcState& s = dc.st;
    if (s.mapMode == 1) return Point{x - s.windowOrg.x + s.viewportOrg.x, y - s.windowOrg.y + s.viewportOrg.y};
    auto map = [](int v, int wo, int we, int vo, int ve) {
        if (!we) return vo;
        return static_cast<int>(std::lround(static_cast<double>(v - wo) * ve / we)) + vo;
    };
    return Point{map(x, s.windowOrg.x, s.windowExt.x, s.viewportOrg.x, s.viewportExt.x),
                 map(y, s.windowOrg.y, s.windowExt.y, s.viewportOrg.y, s.viewportExt.y)};
}

Point Gdi::ToLogical(const DcObj& dc, int x, int y) const {
    const DcState& s = dc.st;
    if (s.mapMode == 1) return Point{x - s.viewportOrg.x + s.windowOrg.x, y - s.viewportOrg.y + s.windowOrg.y};
    auto map = [](int v, int vo, int ve, int wo, int we) {
        if (!ve) return wo;
        return static_cast<int>(std::lround(static_cast<double>(v - vo) * we / ve)) + wo;
    };
    return Point{map(x, s.viewportOrg.x, s.viewportExt.x, s.windowOrg.x, s.windowExt.x),
                 map(y, s.viewportOrg.y, s.viewportExt.y, s.windowOrg.y, s.windowExt.y)};
}

int Gdi::ScaleX(const DcObj& dc, int len) const {
    if (dc.st.mapMode == 1 || !dc.st.windowExt.x) return len;
    return static_cast<int>(std::lround(std::fabs(static_cast<double>(len) * dc.st.viewportExt.x / dc.st.windowExt.x)));
}
int Gdi::ScaleY(const DcObj& dc, int len) const {
    if (dc.st.mapMode == 1 || !dc.st.windowExt.y) return len;
    return static_cast<int>(std::lround(std::fabs(static_cast<double>(len) * dc.st.viewportExt.y / dc.st.windowExt.y)));
}

bool Gdi::Target(uint32_t hdc,Surface*& s,int& ox,int& oy,Region& clip,const SurfaceWriteCapture** capture) {
    if(!PrepareCpuAccess())return false;
    return ResolveTarget(hdc,s,ox,oy,clip,capture);
}
bool Gdi::ResolveTarget(uint32_t hdc,Surface*& s,int& ox,int& oy,Region& clip,const SurfaceWriteCapture** capture) {
    if(capture)*capture=nullptr;
    DcObj* dc = Dc(hdc);
    if (!dc) return false;
    ox = oy = 0;
    if (dc->kind == DcKind::Memory) {
        BitmapObj* b = Bitmap(dc->st.bitmap);
        if (!b || !b->view.Valid()) return false;
        s = &b->view;
        clip = Region(Rect(0, 0, s->width, s->height));
    } else if (dc->kind == DcKind::Info) {
        return false;
    } else {
        if (!screen_ || !screen_->Valid()) return false;
        s = screen_;
        if (dc->kind == DcKind::Window) {
            WindowDcInfo info;info.expectedWindowIdentity=dc->windowIdentity;
            info.captureWrites=capture!=nullptr;
            if (!provider_ || !provider_(dc->hwnd, dc->wholeWindow, info)) return false;
            if(dc->windowIdentity!=info.windowIdentity)return false;
            if(capture)*capture=info.capture;
            ox = info.originX;
            oy = info.originY;
            clip = info.visible;
            clip.IntersectRect(Rect(0, 0, s->width, s->height));
        } else {
            clip = Region(Rect(0, 0, s->width, s->height));
        }
        if (dc->paintDc) clip.Intersect(dc->paintClip);
    }
    if (dc->st.hasClip) {
        Region c = dc->st.clip;
        c.Offset(ox, oy);
        clip.Intersect(c);
    }
    return true;
}

bool Gdi::InstallCpuGate(const std::shared_ptr<CpuRasterGate>& gate) {
    if(cpuGateInstalled_ || cpuAccessFault_ || !gate)return false;
    cpuGate_=gate;cpuGateInstalled_=true;return true;
}
bool Gdi::RemoveCpuGate(const std::shared_ptr<CpuRasterGate>& gate) {
    if(solidInstalled_ || !cpuGateInstalled_ || !gate || cpuGate_.lock()!=gate)return false;
    cpuGate_.reset();cpuGateInstalled_=false;return true;
}
bool Gdi::InstallSolidBackend(const std::shared_ptr<SolidRasterBackend>& backend) {
    if(solidInstalled_ || CpuAccessFault() || !cpuGateInstalled_ || !backend)return false;
    solidBackend_=backend;solidInstalled_=true;return true;
}
bool Gdi::RemoveSolidBackend(const std::shared_ptr<SolidRasterBackend>& backend) {
    if(!solidInstalled_ || !backend || solidBackend_.lock()!=backend)return false;
    solidBackend_.reset();solidInstalled_=false;return true;
}
RasterDispatch Gdi::DispatchSolid(uint32_t hdc,const Rect& r,uint32_t brush) {
    if(CpuAccessFault()){cpuAccessFault_=true;return RasterDispatch::Failed;}
    if(!solidInstalled_)return RasterDispatch::NotHandled;
    const auto* dc=Dc(hdc);const auto* b=Brush(brush);
    if(!dc || dc->kind!=DcKind::Window || dc->st.mapMode!=1 || dc->st.layout || !b || b->style!=0)
        return RasterDispatch::NotHandled;
    // Bound native transform arithmetic; other coordinates retain CPU reference.
    for(int value:{r.left,r.top,r.right,r.bottom,dc->st.windowOrg.x,dc->st.windowOrg.y,
                   dc->st.viewportOrg.x,dc->st.viewportOrg.y})
        if(value < -16384 || value > 16384)return RasterDispatch::NotHandled;
    const Rect normalized{std::min(r.left,r.right),std::min(r.top,r.bottom),
                          std::max(r.left,r.right),std::max(r.top,r.bottom)};
    if(normalized.Empty())return RasterDispatch::NotHandled;
    const auto backend=solidBackend_.lock();
    const auto result=backend?backend->Fill(hdc,normalized,ColorrefToRgb(b->sysColorIndex>=0?SysColor(b->sysColorIndex):b->color)):RasterDispatch::Failed;
    if(result==RasterDispatch::Failed)cpuAccessFault_=true;
    return result;
}
bool Gdi::PrepareCpuAccess() {
    if(CpuAccessFault()){cpuAccessFault_=true;return false;}
    if(!cpuGateInstalled_)return true;
    if(preparingCpu_){cpuAccessFault_=true;return false;}
    const auto gate=cpuGate_.lock();
    preparingCpu_=true;const bool ok=gate && gate->Prepare();preparingCpu_=false;
    if(!ok)cpuAccessFault_=true;
    return !cpuAccessFault_; // a reentrant failure cannot be masked by outer success
}

bool Gdi::WindowImageInfo(const WindowRasterTarget& r,WindowDcInfo& info) {
    if(!r.windowIdentity || r.layoutGeneration!=layoutGeneration_ || !provider_ ||
       !screen_ || !screen_->Valid() || screen_->bpp!=32)return false;
    info.expectedWindowIdentity=r.windowIdentity;
    if(!provider_(r.hwnd,true,info) || info.windowIdentity!=r.windowIdentity || info.frame!=r.frame ||
       info.visible.Rects().size()>128)return false;
    for(const auto& q:info.visible.Rects())if(q.Empty() || q.Intersect(r.frame)!=q ||
        q.Intersect({0,0,screen_->width,screen_->height})!=q)return false;
    const int64_t w=int64_t(r.frame.right)-r.frame.left,h=int64_t(r.frame.bottom)-r.frame.top;
    return w>0 && h>0 && w<=8192 && h<=8192;
}
bool Gdi::SnapshotWindow(const WindowRasterTarget& r,uint32_t* rgb,size_t count) {
    WindowDcInfo info;
    if(!rgb || !WindowImageInfo(r,info) || uint64_t(r.frame.Width())*r.frame.Height()!=count)return false;
    std::fill(rgb,rgb+count,0);
    for(const auto& q:info.visible.Rects())for(int y=q.top;y<q.bottom;++y)for(int x=q.left;x<q.right;++x)
        rgb[size_t(y-r.frame.top)*r.frame.Width()+x-r.frame.left]=screen_->Get(x,y);
    return true;
}
bool Gdi::CommitWindow(const WindowRasterTarget& r,const Surface& image,const Region& dirty,size_t& pixels) {
    pixels=0;WindowDcInfo info;
    if(!WindowImageInfo(r,info) || !image.Valid() || image.bpp!=32 || image.width!=r.frame.Width() ||
       image.height!=r.frame.Height() || image.pitch<int64_t(image.width)*4 || dirty.Rects().size()>128)return false;
    for(const auto& q:dirty.Rects()) {
        if(q.Empty() || q.Intersect({0,0,image.width,image.height})!=q)return false;
        Region missing(q.Offset(r.frame.left,r.frame.top));
        for(const auto& visible:info.visible.Rects())missing.SubtractRect(visible);
        if(!missing.Empty())return false;
    }
    // Validate the entire commit before requesting optional retained capture or
    // writing pixels. Only accepted dirty areas change screen/layer ownership.
    info={};info.expectedWindowIdentity=r.windowIdentity;info.captureWrites=true;
    if(!provider_(r.hwnd,true,info) || info.windowIdentity!=r.windowIdentity)return false;
    Surface dst=*screen_;dst.writeCapture=info.capture;
    for(const auto& q:dirty.Rects())for(int y=q.top;y<q.bottom;++y)for(int x=q.left;x<q.right;++x) {
        dst.Set(x+r.frame.left,y+r.frame.top,image.Get(x,y));++pixels;
    }
    if(pixels)MarkScreenChanged();
    return true;
}

bool Gdi::CommitWindowMask(const WindowRasterTarget& r,const Surface& image,const uint8_t* mask,size_t bytes,size_t& pixels) {
    pixels=0;WindowDcInfo info;
    if(!mask || !WindowImageInfo(r,info) || !image.Valid() || image.bpp!=32 ||
       image.width!=r.frame.Width() || image.height!=r.frame.Height() || image.width>MaxWindowMaskSide || image.height>MaxWindowMaskSide ||
       image.pitch<int64_t(image.width)*4 || image.pitch%4 || reinterpret_cast<uintptr_t>(image.bits)%alignof(uint32_t))return false;
    const size_t count=size_t(image.width)*image.height;
    if(bytes!=(count+7)/8 || (count%8 && (mask[bytes-1] & uint8_t(0xffu<<(count%8)))))return false;
    // Validate every dirty pixel BEFORE capture setup or any mutation. Expanding
    // to a bounding box would steal untouched CPU/child-window ownership.
    for(size_t n=0;n<count;++n)if(mask[n/8] & uint8_t(1u<<(n%8))) {
        if(!info.visible.Contains(r.frame.left+int(n%image.width),r.frame.top+int(n/image.width)))return false;
    }
    info={};info.expectedWindowIdentity=r.windowIdentity;info.captureWrites=true;
    if(!provider_(r.hwnd,true,info) || info.windowIdentity!=r.windowIdentity)return false;
    Surface dst=*screen_;dst.writeCapture=info.capture;
    for(size_t n=0;n<count;++n)if(mask[n/8] & uint8_t(1u<<(n%8))) {
        const int x=int(n%image.width),y=int(n/image.width);
        dst.Set(x+r.frame.left,y+r.frame.top,image.Get(x,y));++pixels;
    }
    if(pixels)MarkScreenChanged();
    return true;
}

void Gdi::DeviceSize(uint32_t hdc, int& w, int& h) {
    w = h = 0;
    DcObj* dc = Dc(hdc);
    if (!dc) return;
    if (dc->kind == DcKind::Memory) {
        if (BitmapObj* b = Bitmap(dc->st.bitmap)) { w = b->view.width; h = b->view.height; }
    } else if (screen_) {
        w = screen_->width;
        h = screen_->height;
    }
}

// ---------------------------------------------------------------------------
// Растеризация

struct Gdi::Ctx {
    DcObj* dc = nullptr;
    Surface* s = nullptr;
    int ox = 0, oy = 0;
    Region clip;
    Rect bounds;
    bool simple = true;
    int rop2 = 13;
    bool onScreen = false;
    Surface rasterView;
};

bool Gdi::MakeCtx(uint32_t hdc, Ctx& c) {
    const SurfaceWriteCapture* capture=nullptr;
    c.dc = Dc(hdc);
    if (!c.dc || !Target(hdc, c.s, c.ox, c.oy, c.clip,&capture)) return false;
    c.bounds = c.clip.Bounds();
    c.simple = c.clip.Rects().size() <= 1;
    c.rop2 = c.dc->st.rop2;
    c.onScreen = c.s == screen_;
    if(capture && c.onScreen) {c.rasterView=*c.s;c.rasterView.writeCapture=capture;c.s=&c.rasterView;}
    if (c.onScreen) ++screenChanges_;
    return !c.clip.Empty();
}

void Gdi::PixelRop2(Ctx& c, int x, int y, uint32_t rgb) {
    if (!c.bounds.Contains(x, y) || (!c.simple && !c.clip.Contains(x, y))) return;
    if (c.rop2 == 13) { c.s->Set(x, y, rgb); return; }
    c.s->Set(x, y, Rop2(rgb, c.s->Get(x, y), c.rop2));
}

uint32_t Gdi::BrushColorAt(const Ctx& c, const BrushObj* b, int x, int y) const {
    // x, y — координаты поверхности; узор привязан к началу кисти
    int px = x - c.ox - c.dc->st.brushOrg.x, py = y - c.oy - c.dc->st.brushOrg.y;
    switch (b->style) {
    case 2: return Hatch(b->hatch, px, py) ? ColorrefToRgb(b->color) : ColorrefToRgb(c.dc->st.bkColor);
    case 3: case 5: {
        const Surface& p = b->pattern.s;
        if (!p.Valid()) return ColorrefToRgb(b->color);
        int u = ((px % p.width) + p.width) % p.width, v = ((py % p.height) + p.height) % p.height;
        if (p.bpp == 1 && b->style == 3) // монохромный узор: 0 — цвет текста, 1 — фон
            return p.GetRaw(u, v) ? ColorrefToRgb(c.dc->st.bkColor) : ColorrefToRgb(c.dc->st.textColor);
        return p.Get(u, v);
    }
    default:
        return ColorrefToRgb(b->sysColorIndex >= 0 ? SysColor(b->sysColorIndex) : b->color);
    }
}

void Gdi::SpanBrush(Ctx& c, int y, int x0, int x1, const BrushObj* b) {
    if (!b || b->style == 1 || y < c.bounds.top || y >= c.bounds.bottom) return;
    bool transparentHatch = b->style == 2 && c.dc->st.bkMode == 1;
    for (const Rect& r : c.clip.Rects()) {
        if (y < r.top || y >= r.bottom) continue;
        int a = std::max(x0, r.left), e = std::min(x1, r.right);
        for (int x = a; x < e; ++x) {
            if (transparentHatch && !Hatch(b->hatch, x - c.ox - c.dc->st.brushOrg.x, y - c.oy - c.dc->st.brushOrg.y)) continue;
            uint32_t col = BrushColorAt(c, b, x, y);
            c.s->Set(x, y, c.rop2 == 13 ? col : Rop2(col, c.s->Get(x, y), c.rop2));
        }
    }
}

void Gdi::FillPolygonDevice(Ctx& c, const std::vector<std::vector<Point>>& polys, const BrushObj* b, bool winding) {
    if (!b || b->style == 1) return;
    int minY = 1 << 30, maxY = -(1 << 30);
    for (const auto& poly : polys)
        for (const Point& p : poly) { minY = std::min(minY, p.y + c.oy); maxY = std::max(maxY, p.y + c.oy); }
    minY = std::max(minY, c.bounds.top);
    maxY = std::min(maxY, c.bounds.bottom - 1);
    std::vector<std::pair<double, int>> xs;
    for (int y = minY; y <= maxY; ++y) {
        double sy = y + 0.5;
        xs.clear();
        for (const auto& poly : polys) {
            size_t n = poly.size();
            for (size_t i = 0; i < n; ++i) {
                Point a = poly[i], e = poly[(i + 1) % n];
                double ay = a.y + c.oy, ey = e.y + c.oy;
                if (ay == ey) continue;
                int dir = ey > ay ? 1 : -1;
                if (ay > ey) { std::swap(a, e); std::swap(ay, ey); }
                if (sy < ay || sy >= ey) continue;
                double x = a.x + c.ox + (sy - ay) * (e.x - a.x) / (ey - ay);
                xs.push_back({x, dir});
            }
        }
        std::sort(xs.begin(), xs.end());
        int wind = 0;
        for (size_t i = 0; i + 1 < xs.size(); ++i) {
            bool inside;
            if (winding) { wind += xs[i].second; inside = wind != 0; }
            else inside = (i % 2) == 0;
            if (!inside) continue;
            int x0 = static_cast<int>(std::ceil(xs[i].first - 0.5));
            int x1 = static_cast<int>(std::ceil(xs[i + 1].first - 0.5));
            if (x1 > x0) SpanBrush(c, y, x0, x1, b);
        }
    }
}

void Gdi::LineDevice(Ctx& c, int x0, int y0, int x1, int y1, const PenObj* pen, bool lastPoint, int& dashPos) {
    if (!pen || pen->style == 5) return;
    uint32_t col = ColorrefToRgb(pen->color);
    uint32_t bk = ColorrefToRgb(c.dc->st.bkColor);
    int width = pen->width;
    if (width > 1) width = std::max(1, ScaleX(*c.dc, width));
    // штрихи для косметических перьев: длины «вкл/выкл»
    static const int kDash[] = {18, 6}, kDot[] = {3, 3}, kDashDot[] = {9, 6, 3, 6}, kDashDotDot[] = {9, 3, 3, 3, 3, 3};
    const int* pattern = nullptr;
    int plen = 0;
    if (width == 1) {
        switch (pen->style) {
        case 1: pattern = kDash; plen = 2; break;
        case 2: pattern = kDot; plen = 2; break;
        case 3: pattern = kDashDot; plen = 4; break;
        case 4: pattern = kDashDotDot; plen = 6; break;
        }
    }
    int total = 0;
    for (int i = 0; i < plen; ++i) total += pattern[i];
    x0 += c.ox; y0 += c.oy; x1 += c.ox; y1 += c.oy;
    int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    int x = x0, y = y0;
    for (;;) {
        bool end = x == x1 && y == y1;
        if (end && !lastPoint) break;
        bool on = true;
        if (pattern) {
            int p = dashPos % total, k = 0;
            while (p >= pattern[k]) { p -= pattern[k]; ++k; }
            on = (k % 2) == 0;
            ++dashPos;
        }
        if (on) {
            if (width == 1) PixelRop2(c, x, y, col);
            else {
                int r0 = -(width / 2), r1 = r0 + width;
                for (int yy = r0; yy < r1; ++yy)
                    for (int xx = r0; xx < r1; ++xx) {
                        if (width > 2 && (xx * 2 + 1 - (r0 + r1 - 1)) * (xx * 2 + 1 - (r0 + r1 - 1)) +
                                (yy * 2 + 1 - (r0 + r1 - 1)) * (yy * 2 + 1 - (r0 + r1 - 1)) > width * width + 1)
                            continue; // круглое перо
                        PixelRop2(c, x + xx, y + yy, col);
                    }
            }
        } else if (c.dc->st.bkMode == 2) {
            PixelRop2(c, x, y, bk);
        }
        if (end) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x += sx; }
        if (e2 <= dx) { err += dx; y += sy; }
    }
}

void Gdi::StrokeDevice(Ctx& c, const std::vector<Point>& pts, bool closed, const PenObj* pen) {
    if (!pen || pen->style == 5 || pts.empty()) return;
    int dash = 0;
    size_t n = pts.size();
    for (size_t i = 0; i + 1 < n; ++i) LineDevice(c, pts[i].x, pts[i].y, pts[i + 1].x, pts[i + 1].y, pen, false, dash);
    if (closed && n > 1) LineDevice(c, pts[n - 1].x, pts[n - 1].y, pts[0].x, pts[0].y, pen, false, dash);
    if (n == 1) LineDevice(c, pts[0].x, pts[0].y, pts[0].x, pts[0].y, pen, true, dash);
}

bool Gdi::SetPixel(uint32_t hdc, int x, int y, uint32_t colorref) {
    RasterMetrics::Scope timing(g_rasterMetrics,RasterStage::Pixel);
    Ctx c;
    if (!MakeCtx(hdc, c)) return false;
    Point p = ToDevice(*c.dc, x, y);
    int sx = p.x + c.ox, sy = p.y + c.oy;
    if (!c.bounds.Contains(sx, sy) || (!c.simple && !c.clip.Contains(sx, sy))) return false;
    c.s->Set(sx, sy, ColorrefToRgb(colorref));
    return true;
}

uint32_t Gdi::GetPixel(uint32_t hdc, int x, int y) {
    RasterMetrics::Scope timing(g_rasterMetrics,RasterStage::Readback);
    DcObj* dc = Dc(hdc);
    Surface* s;
    int ox, oy;
    Region clip;
    if (!dc || !Target(hdc, s, ox, oy, clip)) return 0xFFFFFFFFu;
    Point p = ToDevice(*dc, x, y);
    int sx = p.x + ox, sy = p.y + oy;
    if (!clip.Contains(sx, sy)) return 0xFFFFFFFFu;
    return RgbToColorref(s->Get(sx, sy));
}

bool Gdi::MoveTo(uint32_t hdc, int x, int y, Point* old) {
    DcObj* dc = Dc(hdc);
    if (!dc) return false;
    if (old) *old = dc->st.curPos;
    dc->st.curPos = Point{x, y};
    return true;
}

bool Gdi::LineTo(uint32_t hdc, int x, int y) {
    RasterMetrics::Scope timing(g_rasterMetrics,RasterStage::Stroke);
    DcObj* dc = Dc(hdc);
    if (!dc) return false;
    Ctx c;
    if (MakeCtx(hdc, c)) {
        Point a = ToDevice(*dc, dc->st.curPos.x, dc->st.curPos.y), b = ToDevice(*dc, x, y);
        int dash = 0;
        LineDevice(c, a.x, a.y, b.x, b.y, Pen(dc->st.pen), false, dash);
    }
    dc->st.curPos = Point{x, y};
    return true;
}

bool Gdi::Polyline(uint32_t hdc, const std::vector<Point>& pts) {
    RasterMetrics::Scope timing(g_rasterMetrics,RasterStage::Stroke);
    Ctx c;
    if (!MakeCtx(hdc, c)) return Dc(hdc) != nullptr;
    std::vector<Point> d;
    for (const Point& p : pts) d.push_back(ToDevice(*c.dc, p.x, p.y));
    if (d.size() >= 2) StrokeDevice(c, d, false, Pen(c.dc->st.pen));
    return true;
}

bool Gdi::PolyPolygon(uint32_t hdc, const std::vector<Point>& pts, const std::vector<int>& counts) {
    RasterMetrics::Scope timing(g_rasterMetrics,RasterStage::Shape);
    Ctx c;
    if (!MakeCtx(hdc, c)) return Dc(hdc) != nullptr;
    std::vector<std::vector<Point>> polys;
    size_t k = 0;
    for (int n : counts) {
        std::vector<Point> poly;
        for (int i = 0; i < n && k < pts.size(); ++i, ++k) poly.push_back(ToDevice(*c.dc, pts[k].x, pts[k].y));
        if (poly.size() >= 2) polys.push_back(poly);
    }
    FillPolygonDevice(c, polys, Brush(c.dc->st.brush), c.dc->st.polyFillMode == 2);
    for (const auto& poly : polys) StrokeDevice(c, poly, true, Pen(c.dc->st.pen));
    return true;
}

bool Gdi::Polygon(uint32_t hdc, const std::vector<Point>& pts) {
    RasterMetrics::Scope timing(g_rasterMetrics,RasterStage::Shape);
    return PolyPolygon(hdc, pts, {static_cast<int>(pts.size())});
}

namespace {
// Строки фигуры «скруглённый прямоугольник» в непрерывных координатах
// [l, r) x [t, b): для строки y — отрезок [x0, x1). Эллипс — частный случай.
bool ShapeRow(double l, double t, double r, double b, double rx, double ry, int y, int& x0, int& x1) {
    double cy = y + 0.5;
    if (cy < t || cy >= b || r <= l) return false;
    double inset = 0;
    if (rx > 0 && ry > 0) {
        double dy = 0;
        if (cy < t + ry) dy = t + ry - cy;
        else if (cy > b - ry) dy = cy - (b - ry);
        if (dy > ry) return false;
        double q = dy / ry;
        inset = rx * (1.0 - std::sqrt(std::max(0.0, 1.0 - q * q)));
    }
    x0 = static_cast<int>(std::lround(l + inset));
    x1 = static_cast<int>(std::lround(r - inset));
    return x1 > x0;
}
} // namespace

// Общая отрисовка прямоугольника/скруглённого/эллипса: заливка кистью
// внутри контура и контур пером (ширина пера внутрь фигуры).
static void ShapeImpl(Gdi& g, uint32_t hdc, int l, int t, int r, int b, int ew, int eh, bool ellipse);

bool Gdi::Rectangle(uint32_t hdc, int l, int t, int r, int b) { RasterMetrics::Scope timing(g_rasterMetrics,RasterStage::Shape); ShapeImpl(*this, hdc, l, t, r, b, 0, 0, false); return Dc(hdc) != nullptr; }
bool Gdi::RoundRect(uint32_t hdc, int l, int t, int r, int b, int w, int h) { RasterMetrics::Scope timing(g_rasterMetrics,RasterStage::Shape); ShapeImpl(*this, hdc, l, t, r, b, w, h, false); return Dc(hdc) != nullptr; }
bool Gdi::Ellipse(uint32_t hdc, int l, int t, int r, int b) { RasterMetrics::Scope timing(g_rasterMetrics,RasterStage::Shape); ShapeImpl(*this, hdc, l, t, r, b, 0, 0, true); return Dc(hdc) != nullptr; }

// Доступ ShapeImpl к закрытым помощникам — через тонкую обёртку-друга
struct GdiShapeAccess {
    static void Run(Gdi& g, uint32_t hdc, int l, int t, int r, int b, int ew, int eh, bool ellipse) {
        Gdi::Ctx c;
        if (!g.MakeCtx(hdc, c)) return;
        Point p0 = g.ToDevice(*c.dc, l, t), p1 = g.ToDevice(*c.dc, r, b);
        int L = std::min(p0.x, p1.x) + c.ox, R = std::max(p0.x, p1.x) + c.ox;
        int T = std::min(p0.y, p1.y) + c.oy, B = std::max(p0.y, p1.y) + c.oy;
        if (R - L < 1 || B - T < 1) return;
        PenObj* pen = g.Pen(c.dc->st.pen);
        BrushObj* brush = g.Brush(c.dc->st.brush);
        bool hasPen = pen && pen->style != 5;
        int pw = hasPen ? std::max(1, pen->width > 1 ? g.ScaleX(*c.dc, pen->width) : 1) : 0;
        double rx = ellipse ? (R - L) / 2.0 : std::min(std::abs(g.ScaleX(*c.dc, ew)) / 2.0, (R - L) / 2.0);
        double ry = ellipse ? (B - T) / 2.0 : std::min(std::abs(g.ScaleY(*c.dc, eh)) / 2.0, (B - T) / 2.0);
        if (!hasPen) { R -= 1; B -= 1; if (ellipse) { rx = (R - L) / 2.0; ry = (B - T) / 2.0; } } // без пера фигура на пиксель меньше
        double il = L + pw, it = T + pw, ir = R - pw, ib = B - pw;
        double irx = std::max(0.0, rx - pw), iry = std::max(0.0, ry - pw);
        uint32_t penColor = hasPen ? ColorrefToRgb(pen->color) : 0;
        for (int y = std::max(T, c.bounds.top); y < std::min(B, c.bounds.bottom); ++y) {
            int ox0, ox1, ix0 = 0, ix1 = 0;
            if (!ShapeRow(L, T, R, B, rx, ry, y, ox0, ox1)) continue;
            bool inner = ShapeRow(il, it, ir, ib, irx, iry, y, ix0, ix1);
            if (inner) g.SpanBrush(c, y, ix0, ix1, brush);
            if (!hasPen) continue;
            if (pen->style != 0 && pen->style != 6 && pw == 1 && !ellipse && rx == 0) continue; // штриховой контур — ниже
            auto put = [&](int a, int e) { for (int x = a; x < e; ++x) g.PixelRop2(c, x, y, penColor); };
            if (!inner) put(ox0, ox1);
            else { put(ox0, ix0); put(ix1, ox1); }
        }
        if (hasPen && pen->style != 0 && pen->style != 6 && pw == 1 && !ellipse && rx == 0) {
            std::vector<Point> pts = {{L - c.ox, T - c.oy}, {R - 1 - c.ox, T - c.oy}, {R - 1 - c.ox, B - 1 - c.oy}, {L - c.ox, B - 1 - c.oy}};
            g.StrokeDevice(c, pts, true, pen);
        }
    }
};

static void ShapeImpl(Gdi& g, uint32_t hdc, int l, int t, int r, int b, int ew, int eh, bool ellipse) {
    GdiShapeAccess::Run(g, hdc, l, t, r, b, ew, eh, ellipse);
}

void Gdi::EllipsePoints(int l, int t, int r, int b, std::vector<Point>& out) {
    double cx = (l + r) / 2.0, cy = (t + b) / 2.0, rx = (r - l) / 2.0, ry = (b - t) / 2.0;
    int n = std::max(16, static_cast<int>((rx + ry) * 2));
    for (int i = 0; i < n; ++i) {
        double a = 2 * kPi * i / n;
        out.push_back(Point{static_cast<int>(std::lround(cx + rx * std::cos(a))), static_cast<int>(std::lround(cy - ry * std::sin(a)))});
    }
}

bool Gdi::ArcLike(uint32_t hdc, int kind, int l, int t, int r, int b, int x1, int y1, int x2, int y2) {
    RasterMetrics::Scope timing(g_rasterMetrics,RasterStage::Shape);
    Ctx c;
    if (!MakeCtx(hdc, c)) return Dc(hdc) != nullptr;
    Point p0 = ToDevice(*c.dc, l, t), p1 = ToDevice(*c.dc, r, b);
    Point s = ToDevice(*c.dc, x1, y1), e = ToDevice(*c.dc, x2, y2);
    double cx = (p0.x + p1.x) / 2.0, cy = (p0.y + p1.y) / 2.0;
    double rx = std::abs(p1.x - p0.x) / 2.0, ry = std::abs(p1.y - p0.y) / 2.0;
    if (rx < 0.5 || ry < 0.5) return true;
    double a0 = std::atan2(-(s.y - cy) / ry, (s.x - cx) / rx), a1 = std::atan2(-(e.y - cy) / ry, (e.x - cx) / rx);
    bool ccw = c.dc->st.arcDirection == 1;
    double sweep = ccw ? a1 - a0 : a0 - a1;
    while (sweep <= 0) sweep += 2 * kPi;
    int n = std::max(8, static_cast<int>((rx + ry) * sweep));
    std::vector<Point> pts;
    for (int i = 0; i <= n; ++i) {
        double a = a0 + (ccw ? 1 : -1) * sweep * i / n;
        pts.push_back(Point{static_cast<int>(std::lround(cx + (rx - 0.5) * std::cos(a))),
                            static_cast<int>(std::lround(cy - (ry - 0.5) * std::sin(a)))});
    }
    if (kind == 2) pts.push_back(Point{static_cast<int>(std::lround(cx)), static_cast<int>(std::lround(cy))});
    if (kind != 0) FillPolygonDevice(c, {pts}, Brush(c.dc->st.brush), false);
    StrokeDevice(c, pts, kind != 0, Pen(c.dc->st.pen));
    return true;
}

bool Gdi::FillRect(uint32_t hdc, const Rect& r, uint32_t brush) {
    RasterMetrics::Scope timing(g_rasterMetrics,RasterStage::Fill);
    const auto dispatched=DispatchSolid(hdc,r,brush);
    if(dispatched!=RasterDispatch::NotHandled)return dispatched==RasterDispatch::Accepted;
    Ctx c;
    if (!MakeCtx(hdc, c)) return !CpuAccessFault() && Dc(hdc) != nullptr;
    BrushObj* b = Brush(brush);
    if (!b) return false;
    c.rop2 = 13;
    Point p0 = ToDevice(*c.dc, r.left, r.top), p1 = ToDevice(*c.dc, r.right, r.bottom);
    int L = std::min(p0.x, p1.x) + c.ox, R = std::max(p0.x, p1.x) + c.ox;
    int T = std::min(p0.y, p1.y) + c.oy, B = std::max(p0.y, p1.y) + c.oy;
    for (int y = std::max(T, c.bounds.top); y < std::min(B, c.bounds.bottom); ++y) SpanBrush(c, y, L, R, b);
    return true;
}

bool Gdi::FrameRect(uint32_t hdc, const Rect& r, uint32_t brush) {
    RasterMetrics::Scope timing(g_rasterMetrics,RasterStage::Fill);
    if (r.Empty()) return true;
    return FillRect(hdc, Rect(r.left, r.top, r.right, r.top + 1), brush) &&
        FillRect(hdc, Rect(r.left, r.bottom - 1, r.right, r.bottom), brush) &&
        FillRect(hdc, Rect(r.left, r.top, r.left + 1, r.bottom), brush) &&
        FillRect(hdc, Rect(r.right - 1, r.top, r.right, r.bottom), brush);
}

bool Gdi::InvertRect(uint32_t hdc, const Rect& r) {
    RasterMetrics::Scope timing(g_rasterMetrics,RasterStage::Fill);
    return PatBlt(hdc, r.left, r.top, r.Width(), r.Height(), 0x550009);
}

bool Gdi::FillRgn(uint32_t hdc, uint32_t rgn, uint32_t brush) {
    RasterMetrics::Scope timing(g_rasterMetrics,RasterStage::Fill);
    RegionObj* ro = Rgn(rgn);
    if (!ro) return false;
    for (const Rect& r : ro->rgn.Rects()) if(!FillRect(hdc, r, brush))return false;
    return true;
}

bool Gdi::PatBlt(uint32_t hdc, int x, int y, int w, int h, uint32_t rop) {
    RasterMetrics::Scope timing(g_rasterMetrics,RasterStage::Blit);
    if(rop==0x00f00021u && w>0 && h>0 && int64_t(x)+w<=16384 && int64_t(y)+h<=16384) {
        const auto* dc=Dc(hdc);
        const auto dispatched=DispatchSolid(hdc,{x,y,int(int64_t(x)+w),int(int64_t(y)+h)},dc?dc->st.brush:0);
        if(dispatched!=RasterDispatch::NotHandled)return dispatched==RasterDispatch::Accepted;
    }
    if(CpuAccessFault())return false;
    return BlitCore(hdc, x, y, w, h, nullptr, false, 0, 0, 0, 0, rop, false, 0);
}

bool Gdi::BlitCore(uint32_t dst, int x, int y, int w, int h, const Surface* src, bool srcMono, int sx, int sy, int sw, int sh,
                   uint32_t rop, bool useKey, uint32_t key, uint32_t srcBkRgb) {
    // srcBkRgb — цвет фона контекста-источника: при копировании цветного в
    // монохромное пиксели этого цвета становятся белыми, остальные чёрными
    Ctx c;
    if (!MakeCtx(dst, c)) return Dc(dst) != nullptr;
    uint32_t code = (rop >> 16) & 0xFF;
    bool needSrc = RopUsesSource(code);
    if (needSrc && !src) return false;
    Point p0 = ToDevice(*c.dc, x, y), p1 = ToDevice(*c.dc, x + w, y + h);
    int W = std::abs(p1.x - p0.x), H = std::abs(p1.y - p0.y);
    if (!W || !H) return true;
    int dx0 = std::min(p0.x, p1.x) + c.ox, dy0 = std::min(p0.y, p1.y) + c.oy;
    bool flipX = (p1.x < p0.x) != (sw < 0), flipY = (p1.y < p0.y) != (sh < 0);
    int SW = std::abs(sw), SH = std::abs(sh);
    int sx0 = std::min(sx, sx + sw), sy0 = std::min(sy, sy + sh);
    BrushObj* brush = RopUsesPattern(code) ? Brush(c.dc->st.brush) : nullptr;
    uint32_t solidPat = brush ? ColorrefToRgb(brush->sysColorIndex >= 0 ? SysColor(brush->sysColorIndex) : brush->color) : 0;
    bool patIsSolid = !brush || brush->style == 0;
    uint32_t textRgb = ColorrefToRgb(c.dc->st.textColor), bkRgb = ColorrefToRgb(c.dc->st.bkColor);
    bool dstMono = c.s->bpp == 1;

    // Копия источника, если он совпадает с приёмником (прокрутка)
    OwnedSurface tmp;
    if (needSrc && src->bits == c.s->bits && SW > 0 && SH > 0) {
        tmp.Allocate(SW, SH, 32);
        for (int yy = 0; yy < SH; ++yy)
            for (int xx = 0; xx < SW; ++xx) {
                int u = sx0 + xx, v = sy0 + yy;
                tmp.s.Set(xx, yy, (u >= 0 && v >= 0 && u < src->width && v < src->height) ? src->Get(u, v) : 0);
            }
        src = &tmp.s;
        sx0 = 0;
        sy0 = 0;
        srcMono = false;
    }

    // Common 1:1 RGB copy: skip general scaling division, palette conversion
    // and ROP dispatch. Keep the reference alpha normalization and write capture.
    if(blitFastPath_ && code==0xCC && !useKey && src && src->bpp==32 && c.s->bpp==32 &&
       !flipX && !flipY && SW==W && SH==H && sx0>=0 && sy0>=0 &&
       int64_t(sx0)+W<=src->width && int64_t(sy0)+H<=src->height) {
        for(const Rect& cr:c.clip.Rects()) {
            const int ya=std::max(dy0,cr.top),yb=std::min(dy0+H,cr.bottom);
            const int xa=std::max(dx0,cr.left),xb=std::min(dx0+W,cr.right);
            if(xa>=xb)continue;
            for(int yy=ya;yy<yb;++yy) {
                const auto* in=reinterpret_cast<const uint32_t*>(src->Row(sy0+yy-dy0))+sx0+xa-dx0;
                auto* out=reinterpret_cast<uint32_t*>(c.s->Row(yy))+xa;
                if(c.s->writeCapture) {
                    for(int xx=xa;xx<xb;++xx)c.s->Set(xx,yy,*in++);
                } else {
                    for(int xx=xa;xx<xb;++xx)*out++=*in++ & 0xFFFFFFu;
                }
            }
        }
        return true;
    }

    for (const Rect& cr : c.clip.Rects()) {
        int ya = std::max(dy0, cr.top), yb = std::min(dy0 + H, cr.bottom);
        int xa = std::max(dx0, cr.left), xb = std::min(dx0 + W, cr.right);
        for (int yy = ya; yy < yb; ++yy) {
            int sv = 0;
            if (needSrc) {
                int j = static_cast<int>(static_cast<int64_t>(yy - dy0) * SH / H);
                if (flipY) j = SH - 1 - j;
                sv = sy0 + j;
                if (sv < 0 || sv >= src->height) continue;
            }
            for (int xx = xa; xx < xb; ++xx) {
                uint32_t s = 0;
                if (needSrc) {
                    int i = static_cast<int>(static_cast<int64_t>(xx - dx0) * SW / W);
                    if (flipX) i = SW - 1 - i;
                    int su = sx0 + i;
                    if (su < 0 || su >= src->width) continue;
                    if (srcMono && !dstMono) s = src->GetRaw(su, sv) ? bkRgb : textRgb;
                    else s = src->Get(su, sv);
                    if (useKey && s == key) continue;
                    if (dstMono && !srcMono && srcBkRgb != 0xFFFFFFFFu) s = s == srcBkRgb ? 0xFFFFFF : 0;
                }
                uint32_t p = patIsSolid ? solidPat : BrushColorAt(c, brush, xx, yy);
                uint32_t out;
                if (code == 0xCC) out = s;
                else if (code == 0xF0) out = p;
                else out = Rop3(p, s, c.s->Get(xx, yy), code);
                c.s->Set(xx, yy, out);
            }
        }
    }
    return true;
}

bool Gdi::BitBlt(uint32_t dst, int x, int y, int w, int h, uint32_t src, int sx, int sy, uint32_t rop) {
    RasterMetrics::Scope timing(g_rasterMetrics,RasterStage::Blit);
    DcObj* sd = Dc(src);
    if (!sd) return BlitCore(dst, x, y, w, h, nullptr, false, 0, 0, 0, 0, rop, false, 0);
    return StretchBlt(dst, x, y, w, h, src, sx, sy, w, h, rop);
}

bool Gdi::StretchBlt(uint32_t dst, int x, int y, int w, int h, uint32_t src, int sx, int sy, int sw, int sh, uint32_t rop) {
    RasterMetrics::Scope timing(g_rasterMetrics,RasterStage::Blit);
    DcObj* sd = Dc(src);
    if (!sd) return BlitCore(dst, x, y, w, h, nullptr, false, 0, 0, 0, 0, rop, false, 0);
    Surface* ss;
    int ox, oy;
    Region sclip;
    if (!Target(src, ss, ox, oy, sclip)) return false;
    Point a = ToDevice(*sd, sx, sy), b = ToDevice(*sd, sx + sw, sy + sh);
    return BlitCore(dst, x, y, w, h, ss, ss->bpp == 1, a.x + ox, a.y + oy, b.x - a.x, b.y - a.y, rop, false, 0,
                    ColorrefToRgb(sd->st.bkColor & 0xFFFFFF));
}

bool Gdi::TransparentBlt(uint32_t dst, int x, int y, int w, int h, uint32_t src, int sx, int sy, int sw, int sh, uint32_t key) {
    RasterMetrics::Scope timing(g_rasterMetrics,RasterStage::Blit);
    DcObj* sd = Dc(src);
    if (!sd) return false;
    Surface* ss;
    int ox, oy;
    Region sclip;
    if (!Target(src, ss, ox, oy, sclip)) return false;
    Point a = ToDevice(*sd, sx, sy), b = ToDevice(*sd, sx + sw, sy + sh);
    return BlitCore(dst, x, y, w, h, ss, ss->bpp == 1, a.x + ox, a.y + oy, b.x - a.x, b.y - a.y, 0xCC0020, true, ColorrefToRgb(key));
}

bool Gdi::BlitFromSurface(uint32_t dst, int x, int y, int w, int h, const Surface& src, int sx, int sy, int sw, int sh, uint32_t rop) {
    RasterMetrics::Scope timing(g_rasterMetrics,RasterStage::Blit);
    return BlitCore(dst, x, y, w, h, &src, false, sx, sy, sw, sh, rop, false, 0);
}

bool Gdi::ExtFloodFill(uint32_t hdc, int x, int y, uint32_t colorref, int type) {
    RasterMetrics::Scope timing(g_rasterMetrics,RasterStage::Flood);
    Ctx c;
    if (!MakeCtx(hdc, c)) return false;
    Point p = ToDevice(*c.dc, x, y);
    int px = p.x + c.ox, py = p.y + c.oy;
    if (!c.clip.Contains(px, py)) return false;
    uint32_t ref = ColorrefToRgb(colorref);
    BrushObj* b = Brush(c.dc->st.brush);
    if (!b || b->style == 1) return false;
    int W = c.s->width, H = c.s->height;
    std::vector<uint8_t> done(static_cast<size_t>(W) * H, 0);
    auto fillable = [&](int xx, int yy) {
        if (!c.bounds.Contains(xx, yy) || done[static_cast<size_t>(yy) * W + xx]) return false;
        if (!c.simple && !c.clip.Contains(xx, yy)) return false;
        uint32_t v = c.s->Get(xx, yy);
        return type == 1 ? v == ref : v != ref;
    };
    if (!fillable(px, py)) return false;
    std::vector<Point> stack{{px, py}};
    c.rop2 = 13;
    while (!stack.empty()) {
        Point q = stack.back();
        stack.pop_back();
        if (!fillable(q.x, q.y)) continue;
        int l = q.x, r = q.x;
        while (fillable(l - 1, q.y)) --l;
        while (fillable(r + 1, q.y)) ++r;
        for (int xx = l; xx <= r; ++xx) done[static_cast<size_t>(q.y) * W + xx] = 1;
        for (int ny : {q.y - 1, q.y + 1}) {
            if (ny < 0 || ny >= H) continue;
            bool prev = false;
            for (int xx = l; xx <= r; ++xx) {
                bool f = fillable(xx, ny);
                if (f && !prev) stack.push_back(Point{xx, ny});
                prev = f;
            }
        }
        // красим после пометки: цвет кисти может совпасть с границей
    }
    for (int yy = 0; yy < H; ++yy)
        for (int xx = 0; xx < W; ++xx)
            if (done[static_cast<size_t>(yy) * W + xx]) c.s->Set(xx, yy, BrushColorAt(c, b, xx, yy));
    return true;
}

// ---------------------------------------------------------------------------
// Текст

Font* Gdi::CurrentFont(uint32_t hdc) {
    DcObj* dc = Dc(hdc);
    FontObj* f = dc ? FontO(dc->st.font) : nullptr;
    if (!f) f = FontO(stock_[13]);
    if (!f->font) f->font = FontCache::Get(f->desc);
    return f->font;
}

bool Gdi::TextOut(uint32_t hdc, int x, int y, const std::u16string& text, int options, const Rect* rect,
                  const std::vector<int>* dx) {
    RasterMetrics::Scope timing(g_rasterMetrics,RasterStage::Text);
    DcObj* dc = Dc(hdc);
    if (!dc) return false;
    Font* font = CurrentFont(hdc);
    Ctx c;
    bool can = MakeCtx(hdc, c);
    const DcState& st = dc->st;
    if (st.textAlign & 1) { x = st.curPos.x; y = st.curPos.y; } // TA_UPDATECP
    Point p = ToDevice(*dc, x, y);
    int width = 0;
    if (dx) { for (size_t i = 0; i < text.size() && i < dx->size(); ++i) width += (*dx)[i]; }
    else width = font->MeasureWidth(text) + st.charExtra * static_cast<int>(text.size());
    const TextMetrics& m = font->Metrics();
    int left = p.x, top = p.y;
    switch (st.textAlign & 6) { case 2: left -= width; break; case 6: left -= width / 2; break; }
    if ((st.textAlign & 24) == 24) top -= m.ascent;
    else if (st.textAlign & 8) top -= m.height;
    if (st.textAlign & 1) {
        int adv = (st.textAlign & 6) == 2 ? -width : width;
        dc->st.curPos.x = ToLogical(*dc, p.x + adv, p.y).x;
    }
    if (!can) return true;
    if (rect && (options & 2)) { // ETO_OPAQUE
        Point a = ToDevice(*dc, rect->left, rect->top), b = ToDevice(*dc, rect->right, rect->bottom);
        Region cl = c.clip;
        if (options & 4) cl.IntersectRect(Rect(a.x + c.ox, a.y + c.oy, b.x + c.ox, b.y + c.oy));
        Rect fr(std::min(a.x, b.x) + c.ox, std::min(a.y, b.y) + c.oy, std::max(a.x, b.x) + c.ox, std::max(a.y, b.y) + c.oy);
        FillSurface(*c.s, cl, fr, ColorrefToRgb(st.bkColor));
    }
    Region clip = c.clip;
    if (rect && (options & 4)) {
        Point a = ToDevice(*dc, rect->left, rect->top), b = ToDevice(*dc, rect->right, rect->bottom);
        clip.IntersectRect(Rect(std::min(a.x, b.x) + c.ox, std::min(a.y, b.y) + c.oy, std::max(a.x, b.x) + c.ox, std::max(a.y, b.y) + c.oy));
    }
    if (st.bkMode == 2 && !(options & 2))
        FillSurface(*c.s, clip, Rect(left + c.ox, top + c.oy, left + c.ox + width, top + c.oy + m.height), ColorrefToRgb(st.bkColor));
    font->Draw(*c.s, clip, left + c.ox, top + c.oy, text, ColorrefToRgb(st.textColor), st.charExtra, dx);
    return true;
}

void FillSurface(Surface& s, const Region& clip, const Rect& r, uint32_t rgb) {
    RasterMetrics::Scope timing(g_rasterMetrics,RasterStage::Fill);
    for (const Rect& cr : clip.Rects()) {
        Rect i = cr.Intersect(r).Intersect(Rect(0, 0, s.width, s.height));
        for (int y = i.top; y < i.bottom; ++y)
            for (int x = i.left; x < i.right; ++x) s.Set(x, y, rgb);
    }
}

void Gdi::TextExtent(uint32_t hdc, const std::u16string& text, int& w, int& h) {
    Font* f = CurrentFont(hdc);
    DcObj* dc = Dc(hdc);
    w = f->MeasureWidth(text) + (dc ? dc->st.charExtra * static_cast<int>(text.size()) : 0);
    h = f->Metrics().height;
}

int Gdi::DrawText(uint32_t hdc, const std::u16string& textIn, Rect& rect, uint32_t format) {
    RasterMetrics::Scope timing(g_rasterMetrics,RasterStage::Text);
    DcObj* dc = Dc(hdc);
    if (!dc) return 0;
    Font* font = CurrentFont(hdc);
    const TextMetrics& m = font->Metrics();
    bool single = format & 0x20, wordBreak = format & 0x10, calc = format & 0x400, noPrefix = format & 0x800;
    bool expandTabs = format & 0x40, hidePrefix = format & 0x100000;

    // строки и позиции подчёркиваемых символов (после '&')
    struct Line { std::u16string text; int underline = -1; };
    std::vector<Line> lines(1);
    for (size_t i = 0; i < textIn.size(); ++i) {
        char16_t ch = textIn[i];
        if (!noPrefix && ch == '&') {
            if (i + 1 < textIn.size() && textIn[i + 1] == '&') { lines.back().text.push_back('&'); ++i; continue; }
            if (!hidePrefix) lines.back().underline = static_cast<int>(lines.back().text.size());
            continue;
        }
        if (ch == '\r') { if (!single) continue; ch = ' '; }
        if (ch == '\n') { if (single) { lines.back().text.push_back(' '); continue; } lines.emplace_back(); continue; }
        if (ch == '\t' && !expandTabs) ch = ' ';
        lines.back().text.push_back(ch);
    }
    int maxW = rect.Width();
    if (wordBreak && !single) {
        std::vector<Line> wrapped;
        for (const Line& ln : lines) {
            std::u16string cur;
            int curUnder = -1;
            size_t pos = 0;
            const std::u16string& t = ln.text;
            if (t.empty()) { wrapped.push_back(ln); continue; }
            while (pos < t.size()) {
                size_t end = pos;
                while (end < t.size() && t[end] != ' ') ++end;
                std::u16string word = t.substr(pos, end - pos);
                std::u16string trial = cur.empty() ? word : cur + u" " + word;
                if (!cur.empty() && font->MeasureWidth(trial) > maxW) {
                    wrapped.push_back(Line{cur, curUnder});
                    cur = word;
                    curUnder = -1;
                } else {
                    cur = trial;
                }
                if (ln.underline >= static_cast<int>(pos) && ln.underline < static_cast<int>(end))
                    curUnder = static_cast<int>(cur.size() - (end - ln.underline));
                pos = end;
                while (pos < t.size() && t[pos] == ' ') ++pos;
            }
            wrapped.push_back(Line{cur, curUnder});
        }
        lines.swap(wrapped);
    }
    int lineH = m.height;
    int totalH = lineH * static_cast<int>(lines.size());
    int widest = 0;
    for (const Line& ln : lines) widest = std::max(widest, font->MeasureWidth(ln.text));
    if (calc) {
        if (!wordBreak || single) rect.right = rect.left + widest;
        else rect.right = rect.left + std::max(widest, 0);
        rect.bottom = rect.top + totalH;
        return totalH;
    }
    int y = rect.top;
    if (single) {
        if (format & 4) y = rect.top + (rect.Height() - lineH) / 2;
        else if (format & 8) y = rect.bottom - lineH;
    }
    DcState saved = dc->st;
    dc->st.textAlign = 0;
    bool clipIt = !(format & 0x100);
    for (const Line& ln : lines) {
        int w = font->MeasureWidth(ln.text);
        int x = rect.left;
        if (format & 1) x = rect.left + (rect.Width() - w) / 2;
        else if (format & 2) x = rect.right - w;
        TextOut(hdc, x, y, ln.text, clipIt ? 4 : 0, clipIt ? &rect : nullptr);
        if (ln.underline >= 0 && ln.underline < static_cast<int>(ln.text.size())) {
            int ux = x + font->MeasureWidth(ln.text.substr(0, ln.underline));
            int uw = font->Advance(ln.text[ln.underline]);
            uint32_t pen = CreatePen(0, 1, dc->st.textColor);
            uint32_t oldPen = SelectObject(hdc, pen);
            Point old;
            MoveTo(hdc, ux, y + m.ascent + 1, &old);
            LineTo(hdc, ux + uw, y + m.ascent + 1);
            SelectObject(hdc, oldPen);
            DeleteObject(pen);
        }
        y += lineH;
    }
    dc->st.textAlign = saved.textAlign;
    dc->st.curPos = saved.curPos;
    return totalH;
}

} // namespace ui
