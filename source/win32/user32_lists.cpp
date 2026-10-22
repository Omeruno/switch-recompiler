// user32_lists.cpp — списки (ListBox), комбинированные списки (ComboBox) и
// полосы прокрутки USER32 (слой "ui2"): элемент ScrollBar и полосы в рамке
// окна (WS_VSCROLL/WS_HSCROLL, SetScrollInfo и т.п.), их отрисовка и
// перетаскивание мышью.

#include <algorithm>

#include "user32_controls.h"

namespace win32 {
namespace user {

using namespace hle;

// ---------------------------------------------------------------------------
// Полосы прокрутки

namespace {

struct SbData { int min = 0, max = 100, page = 0, pos = 0, track = 0; uint32_t disabled = 0; };
struct WinScroll { SbData bar[2]; };
std::map<uint32_t, WinScroll>& Scrolls() {
    static std::map<uint32_t, WinScroll> s;
    return s;
}

// Данные полосы: окна (SB_HORZ 0 / SB_VERT 1) или элемента (SB_CTL 2)
SbData* Bar(uint32_t hwnd, int which, bool create = true) {
    Window* w = Win(hwnd);
    if (!w) return nullptr;
    if (which == 2) {
        static std::map<uint32_t, SbData> ctl;
        Ctl* c = C(w);
        static SbData tmp;
        tmp.min = c->sbMin; tmp.max = c->sbMax; tmp.page = c->sbPage; tmp.pos = c->sbPos; tmp.track = c->sbTrack;
        (void)ctl;
        return &tmp;
    }
    if (which != 0 && which != 1) return nullptr;
    auto it = Scrolls().find(hwnd);
    if (it == Scrolls().end()) {
        if (!create) return nullptr;
        it = Scrolls().emplace(hwnd, WinScroll()).first;
    }
    return &it->second.bar[which];
}
void StoreCtl(uint32_t hwnd, const SbData& d) {
    Ctl* c = C(Win(hwnd));
    c->sbMin = d.min; c->sbMax = d.max; c->sbPage = d.page; c->sbPos = d.pos; c->sbTrack = d.track;
}

uint32_t TrackBrush() {
    static uint32_t b = 0;
    if (!b) {
        ui::BrushObj br;
        br.style = 5;
        br.pattern.Allocate(2, 2, 32);
        uint32_t light = ui::ColorrefToRgb(ui::SysColor(20)), face = ui::ColorrefToRgb(ui::SysColor(15));
        br.pattern.s.Set(0, 0, light); br.pattern.s.Set(1, 1, light);
        br.pattern.s.Set(1, 0, face); br.pattern.s.Set(0, 1, face);
        b = ui::g_gdi.CreateBrush(br);
        ui::g_gdi.MarkStock(b);
    }
    return b;
}

// Части полосы: кнопки, дорожка, ползунок (в координатах полосы)
struct SbLayout { int len = 0, arrow = kScrollW, thumbPos = 0, thumbLen = 0; bool canScroll = false; };
SbLayout Layout(const SbData& d, int len, int thick) {
    SbLayout l;
    l.len = len;
    l.arrow = std::min(thick, len / 2);
    int track = len - 2 * l.arrow;
    int range = d.max - d.min + 1;
    int page = std::max(0, d.page);
    l.canScroll = range > std::max(page, 1) && track > 8;
    if (!l.canScroll) return l;
    l.thumbLen = page ? std::max(8, static_cast<int>(static_cast<int64_t>(track) * page / range)) : std::min(thick, track);
    if (l.thumbLen > track) l.thumbLen = track;
    int maxPos = d.max - std::max(page - 1, 0);
    int span = maxPos - d.min;
    int pos = std::max(d.min, std::min(d.pos, maxPos));
    l.thumbPos = l.arrow + (span > 0 ? static_cast<int>(static_cast<int64_t>(track - l.thumbLen) * (pos - d.min) / span) : 0);
    return l;
}

void Arrow(uint32_t hdc, const Rect& r, int dir, bool enabled, bool pushed) {
    ui::g_gdi.FillRect(hdc, r, SysColorBrush(15));
    if (pushed) ui::g_gdi.FrameRect(hdc, r, SysColorBrush(16));
    else Edge3D(hdc, r, 22, 21, 20, 16);
    int cx = (r.left + r.right) / 2 + (pushed ? 1 : 0), cy = (r.top + r.bottom) / 2 + (pushed ? 1 : 0);
    int col = enabled ? 18 : 16;
    for (int k = 0; k < 4; ++k) {
        switch (dir) {
        case 0: Line(hdc, cx - k, cy - 2 + k, cx + k, cy - 2 + k, col); break;          // вверх
        case 1: Line(hdc, cx - 3 + k, cy - 1 + k, cx + 3 - k, cy - 1 + k, col); break;  // вниз
        case 2: Line(hdc, cx - 2 + k, cy - k, cx - 2 + k, cy + k, col); break;          // влево
        case 3: Line(hdc, cx - 1 + k, cy - 3 + k, cx - 1 + k, cy + 3 - k, col); break;  // вправо
        }
    }
}

void DrawBar(uint32_t hdc, const Rect& r, bool vertical, const SbData& d, bool enabled) {
    int len = vertical ? r.Height() : r.Width();
    int thick = vertical ? r.Width() : r.Height();
    SbLayout l = Layout(d, len, thick);
    auto part = [&](int a, int b) {
        return vertical ? Rect(r.left, r.top + a, r.right, r.top + b) : Rect(r.left + a, r.top, r.left + b, r.bottom);
    };
    bool active = enabled && l.canScroll;
    Arrow(hdc, part(0, l.arrow), vertical ? 0 : 2, active, false);
    Arrow(hdc, part(len - l.arrow, len), vertical ? 1 : 3, active, false);
    ui::g_gdi.FillRect(hdc, part(l.arrow, len - l.arrow), TrackBrush());
    if (active && l.thumbLen > 0) {
        Rect t = part(l.thumbPos, l.thumbPos + l.thumbLen);
        ui::g_gdi.FillRect(hdc, t, SysColorBrush(15));
        Edge3D(hdc, t, 22, 21, 20, 16);
    }
}

// Часть под точкой: 0 стрелка-назад, 1 стрелка-вперёд, 2 дорожка до
// ползунка, 3 после, 4 ползунок
int HitPart(const SbData& d, int len, int thick, int at) {
    SbLayout l = Layout(d, len, thick);
    if (at < l.arrow) return 0;
    if (at >= len - l.arrow) return 1;
    if (!l.canScroll) return -1;
    if (at < l.thumbPos) return 2;
    if (at >= l.thumbPos + l.thumbLen) return 3;
    return 4;
}

int PosFromThumb(const SbData& d, int len, int thick, int thumbStart) {
    SbLayout l = Layout(d, len, thick);
    int track = len - 2 * l.arrow - l.thumbLen;
    int maxPos = d.max - std::max(d.page - 1, 0);
    if (track <= 0) return d.min;
    int p = d.min + static_cast<int>(static_cast<int64_t>(thumbStart - l.arrow) * (maxPos - d.min) / track);
    return std::max(d.min, std::min(p, maxPos));
}

void SendScroll(uint32_t hwnd, int which, uint32_t code, int pos) {
    Window* w = Win(hwnd);
    if (!w) return;
    uint32_t msg = which == 0 ? WM_HSCROLL : WM_VSCROLL;
    if (which == 2) {
        msg = (w->style & 1) ? WM_VSCROLL : WM_HSCROLL; // SBS_VERT
        SendMsg(w->parent, msg, code | (static_cast<uint32_t>(pos & 0xFFFF) << 16), hwnd);
        return;
    }
    SendMsg(hwnd, msg, code | (static_cast<uint32_t>(pos & 0xFFFF) << 16), 0);
}

// Прямоугольник полосы окна в координатах экрана
Rect WindowBarRect(Window* w, int which) {
    Rect sr = ScreenRect(w);
    Rect c = w->client.Offset(sr.left, sr.top);
    if (which == 1) return Rect(c.right, c.top, c.right + kScrollW, c.bottom);
    return Rect(c.left, c.bottom, c.right, c.bottom + kScrollW);
}

void RedrawBar(uint32_t hwnd, int which) {
    Window* w = Win(hwnd);
    if (!w || !IsVisibleChain(w)) return;
    if (which == 2) { Redraw(hwnd); return; }
    DrawNonClient(w);
}

// Модальное перетаскивание ползунка / нажатие стрелок
void TrackBar(uint32_t hwnd, int which, int x, int y) {
    Window* w = Win(hwnd);
    if (!w) return;
    bool vertical = which == 1 || (which == 2 && (w->style & 1));
    Rect r = which == 2 ? ScreenClientRect(w) : WindowBarRect(w, which);
    SbData* dp = Bar(hwnd, which);
    if (!dp) return;
    SbData d = *dp;
    int len = vertical ? r.Height() : r.Width(), thick = vertical ? r.Width() : r.Height();
    int at = vertical ? y - r.top : x - r.left;
    int part = HitPart(d, len, thick, at);
    if (part < 0) return;
    static const uint32_t codes[4] = {0, 1, 2, 3}; // SB_LINEUP/LINEDOWN/PAGEUP/PAGEDOWN
    if (part < 4) { SendScroll(hwnd, which, codes[part], 0); SendScroll(hwnd, which, 8, 0); return; } // + SB_ENDSCROLL
    SbLayout l = Layout(d, len, thick);
    int grab = at - l.thumbPos;
    State& s = S();
    for (;;) {
        Msg msg;
        if (!FetchMessage(msg, 0, 0, 0, true, true) || g_guestExitRequested) break;
        if (msg.message == WM_QUIT) { PostQuit(msg.wParam); break; }
        bool isMouse = (msg.message >= WM_MOUSEMOVE && msg.message <= WM_MBUTTONDBLCLK) || (msg.message >= WM_NCMOUSEMOVE && msg.message <= 0xA9);
        if (!isMouse) { if (msg.hwnd) SendMsg(msg.hwnd, msg.message, msg.wParam, msg.lParam); continue; }
        if (!Win(hwnd)) return;
        int p = vertical ? s.cursorY - r.top : s.cursorX - r.left;
        SbData* cur = Bar(hwnd, which);
        int pos = PosFromThumb(*cur, len, thick, p - grab);
        if (msg.message == WM_LBUTTONUP || msg.message == WM_NCLBUTTONUP) {
            SendScroll(hwnd, which, 4, pos); // SB_THUMBPOSITION
            SendScroll(hwnd, which, 8, 0);
            break;
        }
        if (msg.message == WM_MOUSEMOVE || msg.message == WM_NCMOUSEMOVE) {
            cur->track = pos;
            if (which == 2) StoreCtl(hwnd, *cur);
            SendScroll(hwnd, which, 5, pos); // SB_THUMBTRACK
        }
    }
}

} // namespace

void DrawScrollBars(Window* w, uint32_t hdc) {
    if (!(w->style & (WS_VSCROLL | WS_HSCROLL))) return;
    const Rect& c = w->client;
    bool enabled = !(w->style & WS_DISABLED);
    SbData* v = Bar(w->hwnd, 1);
    SbData* h = Bar(w->hwnd, 0);
    if ((w->style & WS_VSCROLL) && v) DrawBar(hdc, Rect(c.right, c.top, c.right + kScrollW, c.bottom), true, *v, enabled && !(v->disabled & 3));
    if ((w->style & WS_HSCROLL) && h) DrawBar(hdc, Rect(c.left, c.bottom, c.right, c.bottom + kScrollW), false, *h, enabled && !(h->disabled & 3));
    if ((w->style & WS_VSCROLL) && (w->style & WS_HSCROLL))
        ui::g_gdi.FillRect(hdc, Rect(c.right, c.bottom, c.right + kScrollW, c.bottom + kScrollW), SysColorBrush(15));
}

// Щелчок по полосе в рамке окна (из DefWindowProc)
void TrackWindowScrollBar(uint32_t hwnd, bool vertical, int x, int y) { TrackBar(hwnd, vertical ? 1 : 0, x, y); }

namespace {

// Изменение полосы окна: показать/скрыть по диапазону, перерисовать
void UpdateWindowBar(uint32_t hwnd, int which, bool redraw, bool disableNoScroll) {
    Window* w = Win(hwnd);
    SbData* d = Bar(hwnd, which);
    if (!w || !d) return;
    if (which == 2) { if (redraw) Redraw(hwnd); return; }
    uint32_t flag = which == 1 ? WS_VSCROLL : WS_HSCROLL;
    int range = d->max - d->min + 1;
    bool need = range > std::max(d->page, 1) && d->max > d->min;
    bool has = (w->style & flag) != 0;
    if (!disableNoScroll && need != has) {
        if (need) w->style |= flag; else w->style &= ~flag;
        SetWindowPosInternal(hwnd, 0, 0, 0, 0, 0, 0x1 | 0x2 | 0x4 | 0x10 | 0x20);
    } else if (disableNoScroll && !has) {
        w->style |= flag;
        SetWindowPosInternal(hwnd, 0, 0, 0, 0, 0, 0x1 | 0x2 | 0x4 | 0x10 | 0x20);
    } else if (redraw) {
        RedrawBar(hwnd, which);
    }
}

#define FN(name) HostCallOutcome name(cpu::CpuState& c, cpu::IGuestMemory& m)
#define ENTER (G().c = &c, G().m = &m)
inline uint32_t A(cpu::CpuState& c, cpu::IGuestMemory& m, int i) { return Arg(c, m, i); }

FN(SetScrollInfo) {
    ENTER;
    uint32_t hwnd = A(c, m, 0), p = A(c, m, 2);
    int which = static_cast<int>(A(c, m, 1));
    SbData* d = Bar(hwnd, which);
    if (!d || !p) return Ret(0);
    uint32_t mask = m.Read32(p + 4);
    if (mask & 1) { d->min = static_cast<int32_t>(m.Read32(p + 8)); d->max = static_cast<int32_t>(m.Read32(p + 12)); }
    if (mask & 2) d->page = static_cast<int32_t>(m.Read32(p + 16));
    if (mask & 4) d->pos = static_cast<int32_t>(m.Read32(p + 20));
    int maxPos = d->max - std::max(d->page - 1, 0);
    if (d->page > d->max - d->min + 1) d->page = d->max - d->min + 1;
    if (d->pos > maxPos) d->pos = maxPos;
    if (d->pos < d->min) d->pos = d->min;
    if (which == 2) StoreCtl(hwnd, *d);
    UpdateWindowBar(hwnd, which, A(c, m, 3) != 0, (mask & 8) != 0);
    return Ret(static_cast<uint32_t>(d->pos));
}
FN(GetScrollInfo) {
    ENTER;
    uint32_t hwnd = A(c, m, 0), p = A(c, m, 2);
    SbData* d = Bar(hwnd, static_cast<int>(A(c, m, 1)));
    if (!d || !p) return Ret(0);
    uint32_t mask = m.Read32(p + 4);
    if (mask & 1) { m.Write32(p + 8, static_cast<uint32_t>(d->min)); m.Write32(p + 12, static_cast<uint32_t>(d->max)); }
    if (mask & 2) m.Write32(p + 16, static_cast<uint32_t>(d->page));
    if (mask & 4) m.Write32(p + 20, static_cast<uint32_t>(d->pos));
    if (mask & 0x10) m.Write32(p + 24, static_cast<uint32_t>(d->track));
    return Ret(1);
}
FN(SetScrollPos) {
    ENTER;
    uint32_t hwnd = A(c, m, 0);
    int which = static_cast<int>(A(c, m, 1));
    SbData* d = Bar(hwnd, which);
    if (!d) return Ret(0);
    int old = d->pos;
    int maxPos = d->max - std::max(d->page - 1, 0);
    d->pos = std::max(d->min, std::min(static_cast<int>(static_cast<int32_t>(A(c, m, 2))), maxPos));
    if (which == 2) StoreCtl(hwnd, *d);
    if (A(c, m, 3)) RedrawBar(hwnd, which);
    return Ret(static_cast<uint32_t>(old));
}
FN(GetScrollPos) {
    ENTER;
    SbData* d = Bar(A(c, m, 0), static_cast<int>(A(c, m, 1)));
    return Ret(d ? static_cast<uint32_t>(d->pos) : 0);
}
FN(SetScrollRange) {
    ENTER;
    uint32_t hwnd = A(c, m, 0);
    int which = static_cast<int>(A(c, m, 1));
    SbData* d = Bar(hwnd, which);
    if (!d) return Ret(0);
    d->min = static_cast<int32_t>(A(c, m, 2));
    d->max = static_cast<int32_t>(A(c, m, 3));
    d->pos = std::max(d->min, std::min(d->pos, d->max));
    if (which == 2) StoreCtl(hwnd, *d);
    UpdateWindowBar(hwnd, which, A(c, m, 4) != 0, false);
    return Ret(1);
}
FN(GetScrollRange) {
    ENTER;
    SbData* d = Bar(A(c, m, 0), static_cast<int>(A(c, m, 1)));
    if (!d) return Ret(0);
    if (A(c, m, 2)) m.Write32(A(c, m, 2), static_cast<uint32_t>(d->min));
    if (A(c, m, 3)) m.Write32(A(c, m, 3), static_cast<uint32_t>(d->max));
    return Ret(1);
}
FN(ShowScrollBar) {
    ENTER;
    uint32_t hwnd = A(c, m, 0), which = A(c, m, 1);
    Window* w = Win(hwnd);
    if (!w) return Ret(0);
    if (which == 2) { ShowWindowInternal(hwnd, A(c, m, 2) ? 5 : 0); return Ret(1); }
    uint32_t flags = which == 0 ? WS_HSCROLL : which == 1 ? WS_VSCROLL : (WS_HSCROLL | WS_VSCROLL);
    uint32_t old = w->style;
    if (A(c, m, 2)) w->style |= flags; else w->style &= ~flags;
    if (w->style != old) SetWindowPosInternal(hwnd, 0, 0, 0, 0, 0, 0x1 | 0x2 | 0x4 | 0x10 | 0x20);
    return Ret(1);
}
FN(EnableScrollBar) {
    ENTER;
    uint32_t hwnd = A(c, m, 0), which = A(c, m, 1), arrows = A(c, m, 2);
    for (int b = 0; b < 2; ++b)
        if (which == 3 || static_cast<int>(which) == b) if (SbData* d = Bar(hwnd, b)) d->disabled = arrows;
    RedrawBar(hwnd, which == 2 ? 2 : 1);
    return Ret(1);
}

const HostFunctionEntry kEntries[] = {
    {"USER32.dll", "SetScrollInfo", 16, CallConv::Stdcall, &SetScrollInfo},
    {"USER32.dll", "GetScrollInfo", 12, CallConv::Stdcall, &GetScrollInfo},
    {"USER32.dll", "SetScrollPos", 16, CallConv::Stdcall, &SetScrollPos},
    {"USER32.dll", "GetScrollPos", 8, CallConv::Stdcall, &GetScrollPos},
    {"USER32.dll", "SetScrollRange", 20, CallConv::Stdcall, &SetScrollRange},
    {"USER32.dll", "GetScrollRange", 16, CallConv::Stdcall, &GetScrollRange},
    {"USER32.dll", "ShowScrollBar", 12, CallConv::Stdcall, &ShowScrollBar},
    {"USER32.dll", "EnableScrollBar", 12, CallConv::Stdcall, &EnableScrollBar},
};
const bool kRegistered = RegisterHostFunctionsLayer(kEntries, sizeof(kEntries) / sizeof(kEntries[0]), kUiLayer);

} // namespace

uint32_t ScrollBarProc(uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp, bool unicode) {
    Window* w = Win(hwnd);
    if (!w) return 0;
    uint32_t r;
    if (CommonCtlMessage(hwnd, msg, wp, lp, r)) return r;
    Ctl* c = C(w);
    bool vertical = (w->style & 1) != 0;
    switch (msg) {
    case WM_PAINT: {
        uint32_t hdc = CtlDc(hwnd);
        SbData d;
        d.min = c->sbMin; d.max = c->sbMax; d.page = c->sbPage; d.pos = c->sbPos;
        DrawBar(hdc, Rect(0, 0, w->client.Width(), w->client.Height()), vertical, d, !(w->style & WS_DISABLED));
        ui::g_gdi.DeleteDc(hdc);
        w->update.Clear();
        w->erase = false;
        return 0;
    }
    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK:
        TrackBar(hwnd, 2, ClientOriginX(w) + static_cast<int16_t>(lp & 0xFFFF), ClientOriginY(w) + static_cast<int16_t>(lp >> 16));
        return 0;
    case 0xE0: { // SBM_SETPOS
        int old = c->sbPos;
        c->sbPos = std::max(c->sbMin, std::min(static_cast<int>(static_cast<int32_t>(wp)), c->sbMax));
        if (lp) Redraw(hwnd);
        return static_cast<uint32_t>(old);
    }
    case 0xE1: return static_cast<uint32_t>(c->sbPos); // SBM_GETPOS
    case 0xE2: case 0xE6: // SBM_SETRANGE(REDRAW)
        c->sbMin = static_cast<int32_t>(wp);
        c->sbMax = static_cast<int32_t>(lp);
        c->sbPos = std::max(c->sbMin, std::min(c->sbPos, c->sbMax));
        if (msg == 0xE6) Redraw(hwnd);
        return 0;
    case 0xE3: // SBM_GETRANGE
        if (wp) G().m->Write32(wp, static_cast<uint32_t>(c->sbMin));
        if (lp) G().m->Write32(lp, static_cast<uint32_t>(c->sbMax));
        return 0;
    case 0xE9: case 0xEA: { // SBM_SETSCROLLINFO / GETSCROLLINFO
        cpu::IGuestMemory& m = *G().m;
        uint32_t mask = m.Read32(lp + 4);
        if (msg == 0xE9) {
            if (mask & 1) { c->sbMin = static_cast<int32_t>(m.Read32(lp + 8)); c->sbMax = static_cast<int32_t>(m.Read32(lp + 12)); }
            if (mask & 2) c->sbPage = static_cast<int32_t>(m.Read32(lp + 16));
            if (mask & 4) c->sbPos = static_cast<int32_t>(m.Read32(lp + 20));
            if (wp) Redraw(hwnd);
            return static_cast<uint32_t>(c->sbPos);
        }
        if (mask & 1) { m.Write32(lp + 8, static_cast<uint32_t>(c->sbMin)); m.Write32(lp + 12, static_cast<uint32_t>(c->sbMax)); }
        if (mask & 2) m.Write32(lp + 16, static_cast<uint32_t>(c->sbPage));
        if (mask & 4) m.Write32(lp + 20, static_cast<uint32_t>(c->sbPos));
        if (mask & 0x10) m.Write32(lp + 24, static_cast<uint32_t>(c->sbTrack));
        return 1;
    }
    case 0xE4: return 1; // SBM_ENABLE_ARROWS
    case 0x87: return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp, unicode);
}

// ---------------------------------------------------------------------------
// ListBox

namespace {

constexpr uint32_t LBS_NOTIFY = 0x1, LBS_SORT = 0x2, LBS_MULTIPLESEL = 0x8, LBS_OWNERDRAWFIXED = 0x10,
                   LBS_OWNERDRAWVARIABLE = 0x20, LBS_HASSTRINGS = 0x40, LBS_EXTENDEDSEL = 0x800, LBS_NOSEL = 0x4000,
                   LBS_COMBOBOX = 0x8000;
constexpr uint32_t LB_ERR = 0xFFFFFFFFu;

bool OwnerDraw(Window* w) { return (w->style & (LBS_OWNERDRAWFIXED | LBS_OWNERDRAWVARIABLE)) != 0; }
bool HasStrings(Window* w) { return !OwnerDraw(w) || (w->style & LBS_HASSTRINGS); }
bool Multi(Window* w) { return (w->style & (LBS_MULTIPLESEL | LBS_EXTENDEDSEL)) != 0; }

int ItemHeight(uint32_t hwnd) {
    Window* w = Win(hwnd);
    Ctl* c = C(w);
    if (c->itemH > 0) return c->itemH;
    uint32_t hdc = CtlDc(hwnd);
    int h = ui::g_gdi.CurrentFont(hdc)->Metrics().height;
    ui::g_gdi.DeleteDc(hdc);
    if (OwnerDraw(w)) {
        cpu::IGuestMemory& m = *G().m;
        StackBuffer mis(24);
        m.Write32(mis.addr(), 2);       // ODT_LISTBOX
        m.Write32(mis.addr() + 4, w->id);
        m.Write32(mis.addr() + 8, 0);
        m.Write32(mis.addr() + 12, 0);
        m.Write32(mis.addr() + 16, static_cast<uint32_t>(h));
        m.Write32(mis.addr() + 20, 0);
        uint32_t target = c->comboOwner ? c->comboOwner : w->parent;
        Window* tw = Win(target);
        if (tw && c->comboOwner) { m.Write32(mis.addr(), 3); m.Write32(mis.addr() + 4, tw->id); target = tw->parent; }
        SendMsg(target, 0x2C /* WM_MEASUREITEM */, m.Read32(mis.addr() + 4), mis.addr());
        h = static_cast<int>(m.Read32(mis.addr() + 16));
    }
    c->itemH = std::max(1, h);
    return c->itemH;
}

int Visible(uint32_t hwnd) {
    Window* w = Win(hwnd);
    return std::max(1, w->client.Height() / ItemHeight(hwnd));
}

void UpdateListScroll(uint32_t hwnd) {
    Window* w = Win(hwnd);
    Ctl* c = C(w);
    int n = static_cast<int>(c->items.size()), vis = Visible(hwnd);
    if (c->top > std::max(0, n - vis)) c->top = std::max(0, n - vis);
    if (!(w->style & WS_VSCROLL) && !Scrolls().count(hwnd) && n <= vis) return;
    SbData* d = Bar(hwnd, 1);
    d->min = 0;
    d->max = std::max(0, n - 1);
    d->page = vis;
    d->pos = c->top;
    bool need = n > vis;
    bool has = (w->style & WS_VSCROLL) != 0;
    if (need != has && !(w->style & 0x1000 /* LBS_DISABLENOSCROLL */)) {
        if (need) w->style |= WS_VSCROLL; else w->style &= ~WS_VSCROLL;
        SetWindowPosInternal(hwnd, 0, 0, 0, 0, 0, 0x1 | 0x2 | 0x4 | 0x10 | 0x20);
    } else if (IsVisibleChain(w)) {
        DrawNonClient(w);
    }
}

void PaintList(uint32_t hwnd) {
    Window* w = Win(hwnd);
    Ctl* c = C(w);
    Rect rc(0, 0, w->client.Width(), w->client.Height());
    uint32_t hdc = CtlDc(hwnd);
    uint32_t parent = c->comboOwner ? c->comboOwner : w->parent;
    uint32_t brush = parent ? SendMsg(parent, WM_CTLCOLORLISTBOX, hdc, hwnd) : 0;
    if (!brush) brush = SysColorBrush(5);
    uint32_t textColor = ui::DcColorOr(hdc, ui::SysColor(8));
    ui::g_gdi.FillRect(hdc, rc, brush);
    int ih = ItemHeight(hwnd);
    bool focus = FocusForWindow(hwnd) == hwnd || c->comboOwner;
    bool enabled = !(w->style & WS_DISABLED);
    ui::DcObj* dc = ui::g_gdi.Dc(hdc);
    for (int i = c->top; i < static_cast<int>(c->items.size()); ++i) {
        int y = (i - c->top) * ih;
        if (y >= rc.bottom) break;
        Rect ir(0, y, rc.right, y + ih);
        ListItem& it = c->items[static_cast<size_t>(i)];
        bool sel = Multi(w) ? it.selected : i == c->cur;
        if (OwnerDraw(w)) {
            uint32_t state = (sel ? 1 : 0) | (enabled ? 0 : 4) | (focus && i == c->caretItem ? 0x10 : 0);
            cpu::IGuestMemory& m = *G().m;
            StackBuffer dis(48);
            uint32_t p = dis.addr();
            Window* ow = Win(parent);
            m.Write32(p, c->comboOwner ? 3 : 2);
            m.Write32(p + 4, ow && c->comboOwner ? ow->id : w->id);
            m.Write32(p + 8, static_cast<uint32_t>(i));
            m.Write32(p + 12, 1);
            m.Write32(p + 16, state);
            m.Write32(p + 20, c->comboOwner ? c->comboOwner : hwnd);
            m.Write32(p + 24, hdc);
            m.Write32(p + 28, 0);
            m.Write32(p + 32, static_cast<uint32_t>(y));
            m.Write32(p + 36, static_cast<uint32_t>(rc.right));
            m.Write32(p + 40, static_cast<uint32_t>(y + ih));
            m.Write32(p + 44, it.data);
            uint32_t target = (c->comboOwner && ow) ? ow->parent : parent;
            SendMsg(target, 0x2B, m.Read32(p + 4), p);
            continue;
        }
        if (sel) ui::g_gdi.FillRect(hdc, ir, SysColorBrush(13));
        dc->st.bkMode = 1;
        dc->st.textColor = sel ? ui::SysColor(14) : (enabled ? textColor : ui::SysColor(17));
        Rect tr(2, y, rc.right - 2, y + ih);
        ui::g_gdi.DrawText(hdc, it.text, tr, 0x20 | 0x800 | ((w->style & 0x80) ? 0x40 : 0));
        if (focus && i == c->caretItem && FocusForWindow(hwnd) == hwnd && Multi(w)) FocusRect(hdc, ir);
    }
    ui::g_gdi.DeleteDc(hdc);
}

int Insert(uint32_t hwnd, int at, const std::u16string& text, uint32_t data) {
    Window* w = Win(hwnd);
    Ctl* c = C(w);
    ListItem it;
    it.text = text;
    it.data = data;
    if (at < 0 || at > static_cast<int>(c->items.size())) at = static_cast<int>(c->items.size());
    c->items.insert(c->items.begin() + at, it);
    if (c->cur >= at) ++c->cur;
    UpdateListScroll(hwnd);
    if (c->redraw) InvalidateClient(w, Region(Rect(0, 0, w->client.Width(), w->client.Height())), true, false);
    return at;
}

int SortedPos(Window* w, Ctl* c, const std::u16string& text) {
    auto lower = [](std::u16string s) { for (char16_t& ch : s) if (ch >= 'A' && ch <= 'Z') ch = static_cast<char16_t>(ch + 32); return s; };
    std::u16string t = lower(text);
    int i = 0;
    for (; i < static_cast<int>(c->items.size()); ++i) if (lower(c->items[static_cast<size_t>(i)].text) > t) break;
    (void)w;
    return i;
}

void SetCur(uint32_t hwnd, int idx, bool notify) {
    Window* w = Win(hwnd);
    Ctl* c = C(w);
    if (idx >= static_cast<int>(c->items.size())) idx = static_cast<int>(c->items.size()) - 1;
    c->cur = idx;
    c->caretItem = std::max(0, idx);
    int vis = Visible(hwnd);
    if (idx >= 0) {
        if (idx < c->top) c->top = idx;
        if (idx >= c->top + vis) c->top = idx - vis + 1;
    }
    UpdateListScroll(hwnd);
    Redraw(hwnd);
    if (notify && ((w->style & LBS_NOTIFY) || c->comboOwner)) {
        if (c->comboOwner) SendMsg(c->comboOwner, WM_USER + 0x7F00, 1, idx >= 0 ? static_cast<uint32_t>(idx) : 0xFFFFFFFFu);
        else Notify(hwnd, 1); // LBN_SELCHANGE
    }
}

int ItemAt(uint32_t hwnd, int y) {
    Ctl* c = C(Win(hwnd));
    if (y < 0) return -1;
    int i = c->top + y / ItemHeight(hwnd);
    return i < static_cast<int>(c->items.size()) ? i : -1;
}

void CloseDrop(uint32_t list, bool accept);

int FindPrefix(Ctl* c, const std::u16string& prefix, int start, bool exact) {
    auto lower = [](char16_t ch) { return (ch >= 'A' && ch <= 'Z') ? static_cast<char16_t>(ch + 32) : ch; };
    int n = static_cast<int>(c->items.size());
    for (int k = 1; k <= n; ++k) {
        int i = (start + k) % n;
        const std::u16string& t = c->items[static_cast<size_t>(i)].text;
        if (exact ? t.size() != prefix.size() : t.size() < prefix.size()) continue;
        bool ok = true;
        for (size_t j = 0; j < prefix.size(); ++j) if (lower(t[j]) != lower(prefix[j])) { ok = false; break; }
        if (ok) return i;
    }
    return -1;
}

} // namespace

uint32_t ListBoxProc(uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp, bool unicode) {
    Window* w = Win(hwnd);
    if (!w) return 0;
    uint32_t r;
    Ctl* c = C(w);
    if (CommonCtlMessage(hwnd, msg, wp, lp, r)) {
        if (msg == WM_SETFONT) { c->itemH = 0; UpdateListScroll(hwnd); }
        return r;
    }
    cpu::IGuestMemory& m = *G().m;
    auto text = [&](uint32_t p) { return HasStrings(w) ? ReadText(p, unicode) : std::u16string(); };
    switch (msg) {
    case WM_NCCREATE:
        if (w->style & WS_BORDER) { w->style &= ~WS_BORDER; w->exStyle |= WS_EX_CLIENTEDGE; }
        return DefWindowProc(hwnd, msg, wp, lp, unicode);
    case WM_PAINT:
        if (w->ncDirty) DrawNonClient(w);
        PaintList(hwnd);
        if ((w = Win(hwnd))) { w->update.Clear(); w->erase = false; }
        return 0;
    case WM_SIZE:
        UpdateListScroll(hwnd);
        return 0;
    case 0x87: return 0x80 | 0x1; // WANTCHARS | WANTARROWS
    case WM_SETFOCUS: case WM_KILLFOCUS:
        Redraw(hwnd);
        if (w->style & LBS_NOTIFY) Notify(hwnd, msg == WM_SETFOCUS ? 4 : 5);
        return 0;
    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: {
        int y = static_cast<int16_t>(lp >> 16), x = static_cast<int16_t>(lp & 0xFFFF);
        if (c->comboOwner && !Rect(0, 0, w->client.Width(), w->client.Height()).Contains(x, y)) {
            // щелчок вне выпадающего списка: закрыть
            Rect sr = ScreenRect(w);
            if (!sr.Contains(ClientOriginX(w) + x, ClientOriginY(w) + y)) { CloseDrop(hwnd, false); return 0; }
            return 0;
        }
        if (!c->comboOwner) SetFocusInternal(hwnd);
        int i = ItemAt(hwnd, y);
        if (i < 0 || (w->style & LBS_NOSEL)) return 0;
        if (Multi(w)) {
            bool ctrl = (wp & 8) != 0, shiftK = (wp & 4) != 0;
            if ((w->style & LBS_EXTENDEDSEL) && !ctrl && !shiftK) for (auto& it : c->items) it.selected = false;
            if ((w->style & LBS_EXTENDEDSEL) && shiftK && c->anchorItem >= 0) {
                for (auto& it : c->items) it.selected = false;
                for (int k = std::min(c->anchorItem, i); k <= std::max(c->anchorItem, i); ++k) c->items[static_cast<size_t>(k)].selected = true;
            } else {
                c->items[static_cast<size_t>(i)].selected = !c->items[static_cast<size_t>(i)].selected || (w->style & LBS_EXTENDEDSEL && !ctrl);
                c->anchorItem = i;
            }
            c->caretItem = i;
            Redraw(hwnd);
            if (w->style & LBS_NOTIFY) Notify(hwnd, 1);
        } else {
            SetCur(hwnd, i, true);
        }
        if (msg == WM_LBUTTONDBLCLK && (w->style & LBS_NOTIFY)) Notify(hwnd, 2); // LBN_DBLCLK
        if (!c->comboOwner) { c->captured = true; SetCaptureInternal(hwnd); }
        return 0;
    }
    case WM_MOUSEMOVE: {
        int y = static_cast<int16_t>(lp >> 16);
        if (c->comboOwner) {
            int x = static_cast<int16_t>(lp & 0xFFFF);
            if (Rect(0, 0, w->client.Width(), w->client.Height()).Contains(x, y)) {
                int i = ItemAt(hwnd, y);
                if (i >= 0 && i != c->cur) { c->cur = i; Redraw(hwnd); }
            }
            return 0;
        }
        if (c->captured && !Multi(w)) {
            int i = ItemAt(hwnd, std::max(0, std::min(y, w->client.Height() - 1)));
            if (i >= 0 && i != c->cur) SetCur(hwnd, i, true);
        }
        return 0;
    }
    case WM_LBUTTONUP:
        if (c->comboOwner) {
            int x = static_cast<int16_t>(lp & 0xFFFF), y = static_cast<int16_t>(lp >> 16);
            if (Rect(0, 0, w->client.Width(), w->client.Height()).Contains(x, y)) CloseDrop(hwnd, true);
            return 0;
        }
        if (c->captured) { c->captured = false; if (S().capture == hwnd) ReleaseCaptureInternal(); }
        return 0;
    case WM_MOUSEWHEEL: {
        int d = static_cast<int16_t>(wp >> 16) > 0 ? -3 : 3;
        c->top = std::max(0, std::min(c->top + d, std::max(0, static_cast<int>(c->items.size()) - Visible(hwnd))));
        UpdateListScroll(hwnd);
        Redraw(hwnd);
        return 0;
    }
    case WM_VSCROLL: {
        int n = static_cast<int>(c->items.size()), vis = Visible(hwnd);
        switch (wp & 0xFFFF) {
        case 0: c->top--; break;
        case 1: c->top++; break;
        case 2: c->top -= vis; break;
        case 3: c->top += vis; break;
        case 4: case 5: c->top = static_cast<int>(wp >> 16); break;
        case 6: c->top = 0; break;
        case 7: c->top = n; break;
        }
        c->top = std::max(0, std::min(c->top, std::max(0, n - vis)));
        UpdateListScroll(hwnd);
        Redraw(hwnd);
        return 0;
    }
    case WM_KEYDOWN: {
        int n = static_cast<int>(c->items.size());
        if (!n) return 0;
        int i = Multi(w) ? c->caretItem : c->cur;
        switch (wp) {
        case 0x26: i = std::max(0, i - 1); break;
        case 0x28: i = std::min(n - 1, i + 1); break;
        case 0x21: i = std::max(0, i - Visible(hwnd)); break;
        case 0x22: i = std::min(n - 1, i + Visible(hwnd)); break;
        case 0x24: i = 0; break;
        case 0x23: i = n - 1; break;
        case 0x20:
            if (Multi(w)) { c->items[static_cast<size_t>(c->caretItem)].selected ^= true; Redraw(hwnd); if (w->style & LBS_NOTIFY) Notify(hwnd, 1); }
            return 0;
        case 0x0D:
            if (c->comboOwner) CloseDrop(hwnd, true);
            return 0;
        case 0x1B:
            if (c->comboOwner) CloseDrop(hwnd, false);
            return 0;
        default:
            return 0;
        }
        if (Multi(w)) { c->caretItem = i; Redraw(hwnd); }
        else SetCur(hwnd, i, true);
        return 0;
    }
    case WM_CHAR: {
        if (c->items.empty() || !HasStrings(w)) return 0;
        std::u16string p(1, static_cast<char16_t>(wp));
        int i = FindPrefix(c, p, std::max(0, c->cur), false);
        if (i >= 0 && !Multi(w)) SetCur(hwnd, i, true);
        return 0;
    }
    case 0x180: { // LB_ADDSTRING
        std::u16string t = text(lp);
        int at = (w->style & LBS_SORT) && HasStrings(w) ? SortedPos(w, c, t) : -1;
        return static_cast<uint32_t>(Insert(hwnd, at, t, HasStrings(w) ? 0 : lp));
    }
    case 0x181: // LB_INSERTSTRING
        return static_cast<uint32_t>(Insert(hwnd, static_cast<int32_t>(wp), text(lp), HasStrings(w) ? 0 : lp));
    case 0x182: { // LB_DELETESTRING
        if (wp >= c->items.size()) return LB_ERR;
        c->items.erase(c->items.begin() + static_cast<long>(wp));
        if (c->cur == static_cast<int>(wp)) c->cur = -1;
        else if (c->cur > static_cast<int>(wp)) --c->cur;
        UpdateListScroll(hwnd);
        InvalidateClient(w, Region(Rect(0, 0, w->client.Width(), w->client.Height())), true, false);
        return static_cast<uint32_t>(c->items.size());
    }
    case 0x184: // LB_RESETCONTENT
        c->items.clear();
        c->cur = -1;
        c->top = 0;
        UpdateListScroll(hwnd);
        InvalidateClient(w, Region(Rect(0, 0, w->client.Width(), w->client.Height())), true, false);
        return 0;
    case 0x185: { // LB_SETSEL
        if (!Multi(w)) return LB_ERR;
        int32_t i = static_cast<int32_t>(lp);
        for (size_t k = 0; k < c->items.size(); ++k) if (i == -1 || static_cast<int32_t>(k) == i) c->items[k].selected = wp != 0;
        Redraw(hwnd);
        return 0;
    }
    case 0x186: // LB_SETCURSEL
        if (Multi(w)) return LB_ERR;
        SetCur(hwnd, static_cast<int32_t>(wp), false);
        return static_cast<int32_t>(wp) < 0 || wp >= c->items.size() ? LB_ERR : wp;
    case 0x187: // LB_GETSEL
        if (wp >= c->items.size()) return LB_ERR;
        return Multi(w) ? (c->items[wp].selected ? 1 : 0) : (static_cast<int>(wp) == c->cur ? 1 : 0);
    case 0x188: // LB_GETCURSEL
        if (Multi(w)) return static_cast<uint32_t>(c->caretItem);
        return c->cur >= 0 ? static_cast<uint32_t>(c->cur) : LB_ERR;
    case 0x189: // LB_GETTEXT
        if (wp >= c->items.size()) return LB_ERR;
        if (!HasStrings(w)) { m.Write32(lp, c->items[wp].data); return 4; }
        return WriteText(lp, c->items[wp].text, static_cast<uint32_t>(c->items[wp].text.size() + 1), unicode);
    case 0x18A: // LB_GETTEXTLEN
        if (wp >= c->items.size()) return LB_ERR;
        return HasStrings(w) ? static_cast<uint32_t>(c->items[wp].text.size()) : 4;
    case 0x18B: return static_cast<uint32_t>(c->items.size()); // LB_GETCOUNT
    case 0x18C: { // LB_SELECTSTRING
        int i = FindPrefix(c, text(lp), static_cast<int32_t>(wp), false);
        if (i < 0) return LB_ERR;
        SetCur(hwnd, i, false);
        return static_cast<uint32_t>(i);
    }
    case 0x18E: return static_cast<uint32_t>(c->top); // LB_GETTOPINDEX
    case 0x18F: case 0x1A2: { // LB_FINDSTRING / LB_FINDSTRINGEXACT
        if (c->items.empty()) return LB_ERR;
        int start = static_cast<int32_t>(wp);
        if (!HasStrings(w)) {
            for (size_t k = 0; k < c->items.size(); ++k) if (c->items[k].data == lp) return static_cast<uint32_t>(k);
            return LB_ERR;
        }
        int i = FindPrefix(c, text(lp), start < 0 ? -1 : start, msg == 0x1A2);
        return i < 0 ? LB_ERR : static_cast<uint32_t>(i);
    }
    case 0x190: { // LB_GETSELCOUNT
        if (!Multi(w)) return LB_ERR;
        uint32_t n = 0;
        for (auto& it : c->items) n += it.selected;
        return n;
    }
    case 0x191: { // LB_GETSELITEMS
        if (!Multi(w)) return LB_ERR;
        uint32_t n = 0;
        for (size_t k = 0; k < c->items.size() && n < wp; ++k) if (c->items[k].selected) m.Write32(lp + 4 * n++, static_cast<uint32_t>(k));
        return n;
    }
    case 0x197: // LB_SETTOPINDEX
        c->top = std::max(0, std::min(static_cast<int>(static_cast<int32_t>(wp)), std::max(0, static_cast<int>(c->items.size()) - Visible(hwnd))));
        UpdateListScroll(hwnd);
        Redraw(hwnd);
        return 0;
    case 0x198: { // LB_GETITEMRECT
        int ih = ItemHeight(hwnd);
        int y = (static_cast<int>(wp) - c->top) * ih;
        m.Write32(lp, 0);
        m.Write32(lp + 4, static_cast<uint32_t>(y));
        m.Write32(lp + 8, static_cast<uint32_t>(w->client.Width()));
        m.Write32(lp + 12, static_cast<uint32_t>(y + ih));
        return 1;
    }
    case 0x199: return wp < c->items.size() ? c->items[wp].data : LB_ERR; // LB_GETITEMDATA
    case 0x19A: // LB_SETITEMDATA
        if (static_cast<int32_t>(wp) == -1) { for (auto& it : c->items) it.data = lp; return 1; }
        if (wp >= c->items.size()) return LB_ERR;
        c->items[wp].data = lp;
        return 1;
    case 0x19E: c->caretItem = static_cast<int32_t>(wp); Redraw(hwnd); return 0; // LB_SETCARETINDEX
    case 0x19F: return static_cast<uint32_t>(c->caretItem);                   // LB_GETCARETINDEX
    case 0x1A0: c->itemH = static_cast<int>(lp & 0xFFFF); UpdateListScroll(hwnd); Redraw(hwnd); return 0; // LB_SETITEMHEIGHT
    case 0x1A1: return static_cast<uint32_t>(ItemHeight(hwnd));                 // LB_GETITEMHEIGHT
    case 0x1A9: { // LB_ITEMFROMPOINT
        int i = ItemAt(hwnd, static_cast<int16_t>(lp >> 16));
        return i < 0 ? 0x10000u | static_cast<uint32_t>(c->items.empty() ? 0 : c->items.size() - 1) : static_cast<uint32_t>(i);
    }
    case 0x1A8: return 0;                                                       // LB_INITSTORAGE
    case 0x195: case 0x194: return 1;                                          // LB_SETTABSTOPS / SETCOLUMNWIDTH
    case 0x196: return 0;                                                       // LB_SETHORIZONTALEXTENT
    case 0x193: return 0;                                                       // LB_GETHORIZONTALEXTENT
    case 0x183: return 0;                                                       // LB_SELITEMRANGEEX
    case 0x19B: { // LB_SELITEMRANGE
        for (int k = static_cast<int>(lp & 0xFFFF); k <= static_cast<int>(lp >> 16) && k < static_cast<int>(c->items.size()); ++k)
            c->items[static_cast<size_t>(k)].selected = wp != 0;
        Redraw(hwnd);
        return 0;
    }
    case WM_SETREDRAW:
        c->redraw = wp != 0;
        w->redraw = wp != 0;
        if (wp) InvalidateClient(w, Region(Rect(0, 0, w->client.Width(), w->client.Height())), true, false);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp, unicode);
}

// ---------------------------------------------------------------------------
// ComboBox

namespace {

constexpr uint32_t CBS_SIMPLE = 1, CBS_DROPDOWN = 2, CBS_DROPDOWNLIST = 3, CBS_SORT = 0x100, CBS_HASSTRINGS = 0x200,
                   CBS_OWNERDRAWFIXED = 0x10, CBS_OWNERDRAWVARIABLE = 0x20;
constexpr uint32_t CB_ERR = 0xFFFFFFFFu;
constexpr uint32_t WM_COMBO_LIST = WM_USER + 0x7F00; // внутреннее: список -> комбинированный

uint32_t ListOf(Window* w) { return C(w)->listHwnd; }

int ClosedHeight(uint32_t hwnd) {
    uint32_t hdc = CtlDc(hwnd);
    int h = ui::g_gdi.CurrentFont(hdc)->Metrics().height;
    ui::g_gdi.DeleteDc(hdc);
    return h + 8;
}

Rect ButtonRect(Window* w) { return Rect(w->client.Width() - kScrollW - 2, 2, w->client.Width() - 2, w->client.Height() - 2); }

void PaintCombo(uint32_t hwnd) {
    Window* w = Win(hwnd);
    Ctl* c = C(w);
    uint32_t hdc = CtlDc(hwnd);
    Rect rc(0, 0, w->client.Width(), w->client.Height());
    bool enabled = !(w->style & WS_DISABLED);
    uint32_t brush = CtlColor(hwnd, hdc, (w->style & 3) == CBS_DROPDOWNLIST ? WM_CTLCOLORLISTBOX : WM_CTLCOLOREDIT);
    uint32_t textColor = ui::DcColorOr(hdc, ui::SysColor(8));
    ui::g_gdi.FillRect(hdc, rc, enabled ? brush : SysColorBrush(15));
    Edge3D(hdc, rc, 16, 20, 21, 22);
    Rect br = ButtonRect(w);
    ui::g_gdi.FillRect(hdc, br, SysColorBrush(15));
    if (c->dropped) ui::g_gdi.FrameRect(hdc, br, SysColorBrush(16));
    else Edge3D(hdc, br, 22, 21, 20, 16);
    int cx = (br.left + br.right) / 2 + (c->dropped ? 1 : 0), cy = (br.top + br.bottom) / 2 + (c->dropped ? 1 : 0);
    for (int k = 0; k < 4; ++k) Line(hdc, cx - 3 + k, cy - 1 + k, cx + 3 - k, cy - 1 + k, enabled ? 18 : 16);
    if ((w->style & 3) == CBS_DROPDOWNLIST) {
        Window* lw = Win(ListOf(w));
        Ctl* lc = lw ? C(lw) : nullptr;
        Rect tr(3, 3, br.left - 1, rc.bottom - 3);
        bool focus = FocusForWindow(hwnd) == hwnd && !c->dropped;
        if (focus) ui::g_gdi.FillRect(hdc, tr, SysColorBrush(13));
        if (lc && lc->cur >= 0 && lc->cur < static_cast<int>(lc->items.size())) {
            if (w->style & (CBS_OWNERDRAWFIXED | CBS_OWNERDRAWVARIABLE)) {
                cpu::IGuestMemory& m = *G().m;
                StackBuffer dis(48);
                uint32_t p = dis.addr();
                m.Write32(p, 3); m.Write32(p + 4, w->id); m.Write32(p + 8, static_cast<uint32_t>(lc->cur)); m.Write32(p + 12, 1);
                m.Write32(p + 16, (focus ? 0x11 : 0) | 0x1000 /* ODS_COMBOBOXEDIT */ | (enabled ? 0 : 4));
                m.Write32(p + 20, hwnd); m.Write32(p + 24, hdc);
                m.Write32(p + 28, static_cast<uint32_t>(tr.left)); m.Write32(p + 32, static_cast<uint32_t>(tr.top));
                m.Write32(p + 36, static_cast<uint32_t>(tr.right)); m.Write32(p + 40, static_cast<uint32_t>(tr.bottom));
                m.Write32(p + 44, lc->items[static_cast<size_t>(lc->cur)].data);
                SendMsg(w->parent, 0x2B, w->id, p);
            } else {
                CtlText(hdc, lc->items[static_cast<size_t>(lc->cur)].text, Rect(tr.left + 1, tr.top, tr.right, tr.bottom), 0x20 | 0x4 | 0x800,
                        enabled, focus ? ui::SysColor(14) : textColor);
            }
        }
        if (focus) FocusRect(hdc, tr);
    }
    ui::g_gdi.DeleteDc(hdc);
}

void SyncEdit(uint32_t hwnd) {
    Window* w = Win(hwnd);
    if (!w || (w->style & 3) == CBS_DROPDOWNLIST) return;
    Window* lw = Win(ListOf(w));
    if (!lw || w->children.empty()) return;
    Ctl* lc = C(lw);
    uint32_t edit = w->children.front();
    std::u16string t = (lc->cur >= 0 && lc->cur < static_cast<int>(lc->items.size())) ? lc->items[static_cast<size_t>(lc->cur)].text : u"";
    if (Window* ew = Win(edit)) {
        ew->text = t;
        C(ew)->caret = C(ew)->anchor = 0;
        Redraw(edit);
    }
}

void OpenDrop(uint32_t hwnd) {
    Window* w = Win(hwnd);
    Ctl* c = C(w);
    Window* lw = Win(c->listHwnd);
    if (!lw || c->dropped) return;
    Notify(hwnd, 7); // CBN_DROPDOWN
    if (!(w = Win(hwnd)) || !(lw = Win(c->listHwnd))) return;
    Ctl* lc = C(lw);
    Rect sr = ScreenRect(w);
    int ih = ItemHeight(c->listHwnd);
    int n = std::max<int>(1, static_cast<int>(lc->items.size()));
    int h = std::min(std::max(c->dropHeight - sr.Height(), ih + 2), n * ih + 2);
    if (h < ih + 2) h = ih + 2;
    int y = sr.bottom;
    if (y + h > S().screenH) y = sr.top - h;
    lw->style |= WS_VISIBLE;
    SetWindowPosInternal(c->listHwnd, 0xFFFFFFFFu, sr.left, y, sr.Width(), h, 0x10 | 0x40);
    if (lc->cur >= 0) {
        int vis = Visible(c->listHwnd);
        if (lc->cur < lc->top || lc->cur >= lc->top + vis) lc->top = std::max(0, lc->cur - vis / 2);
    }
    UpdateListScroll(c->listHwnd);
    c->dropped = true;
    lc->anchorItem = lc->cur;
    SetCaptureInternal(c->listHwnd);
    Redraw(hwnd);
}

void CloseDrop(uint32_t list, bool accept) {
    Window* lw = Win(list);
    if (!lw) return;
    Ctl* lc = C(lw);
    uint32_t combo = lc->comboOwner;
    Window* w = Win(combo);
    if (!w) return;
    Ctl* c = C(w);
    if (!c->dropped) return;
    c->dropped = false;
    if (S().capture == list) ReleaseCaptureInternal();
    if (!accept) lc->cur = lc->anchorItem;
    ShowWindowInternal(list, 0);
    Notify(combo, 8); // CBN_CLOSEUP
    if (!Win(combo)) return;
    SyncEdit(combo);
    Redraw(combo);
    if (accept) {
        if (lc->cur != lc->anchorItem) Notify(combo, 1); // CBN_SELCHANGE
        Notify(combo, 9);                                // CBN_SELENDOK
    } else {
        Notify(combo, 10);                               // CBN_SELENDCANCEL
    }
}

} // namespace

uint32_t ComboBoxProc(uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp, bool unicode) {
    Window* w = Win(hwnd);
    if (!w) return 0;
    Ctl* c = C(w);
    uint32_t list = c->listHwnd;
    uint32_t r;
    if (msg == WM_SETFONT) {
        c->font = wp;
        if (Window* lw = Win(list)) { C(lw)->font = wp; C(lw)->itemH = 0; }
        if (!w->children.empty()) SendMsg(w->children.front(), WM_SETFONT, wp, 0);
        int h = ClosedHeight(hwnd);
        if (w->rect.Height() != h) SetWindowPosInternal(hwnd, 0, 0, 0, w->rect.Width(), h, 0x2 | 0x4 | 0x10);
        return 0;
    }
    if (CommonCtlMessage(hwnd, msg, wp, lp, r)) return r;
    // CB_* -> LB_* для списка
    auto fwd = [&](uint32_t lbMsg, uint32_t a, uint32_t b) { return SendMsg(list, lbMsg, a, b, unicode); };
    bool hasStrings = !(w->style & (CBS_OWNERDRAWFIXED | CBS_OWNERDRAWVARIABLE)) || (w->style & CBS_HASSTRINGS);
    switch (msg) {
    case WM_CREATE: {
        c->dropHeight = w->rect.Height();
        uint32_t lstyle = WS_POPUP | WS_BORDER | WS_VSCROLL | LBS_COMBOBOX | 0x100 /* NOINTEGRALHEIGHT */ |
                          ((w->style & CBS_SORT) ? LBS_SORT : 0) | ((w->style & CBS_HASSTRINGS) ? LBS_HASSTRINGS : 0) |
                          ((w->style & CBS_OWNERDRAWFIXED) ? LBS_OWNERDRAWFIXED : 0) | ((w->style & CBS_OWNERDRAWVARIABLE) ? LBS_OWNERDRAWVARIABLE : 0);
        uint32_t lh = CreateWindowInternal(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, u"ComboLBox", 0, u"", lstyle, 0, 0, 10, 10, 0, 0,
                                           w->hInstance, 0, true, 0, 0);
        if (!(w = Win(hwnd))) return 0;
        c->listHwnd = lh;
        if (Window* lw = Win(lh)) { C(lw)->comboOwner = hwnd; lw->exStyle &= ~WS_EX_CLIENTEDGE; }
        if ((w->style & 3) != CBS_DROPDOWNLIST) {
            Rect br = ButtonRect(w);
            uint32_t e = CreateWindowInternal(0, u"Edit", 0, u"", WS_CHILD | WS_VISIBLE | 0x80 /* ES_AUTOHSCROLL */, 3, 3,
                                              br.left - 4, ClosedHeight(hwnd) - 6, hwnd, 1001, w->hInstance, 0, true, 0, 0);
            (void)e;
        }
        int h = ClosedHeight(hwnd);
        SetWindowPosInternal(hwnd, 0, 0, 0, w->rect.Width(), h, 0x2 | 0x4 | 0x10);
        return 0;
    }
    case WM_DESTROY:
        if (Win(list)) DestroyWindowInternal(list);
        return 0;
    case WM_PAINT:
        if (w->ncDirty) DrawNonClient(w);
        PaintCombo(hwnd);
        if ((w = Win(hwnd))) { w->update.Clear(); w->erase = false; }
        return 0;
    case 0x87: return 0x1 | 0x80; // WANTARROWS | WANTCHARS
    case WM_SETFOCUS: case WM_KILLFOCUS:
        if (msg == WM_KILLFOCUS && c->dropped) CloseDrop(list, false);
        Redraw(hwnd);
        Notify(hwnd, msg == WM_SETFOCUS ? 3 : 4); // CBN_SETFOCUS / KILLFOCUS
        return 0;
    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK:
        SetFocusInternal(hwnd);
        if (c->dropped) CloseDrop(list, false);
        else if (g_platform.textInput && (w->style & 3) == CBS_DROPDOWN && static_cast<int16_t>(lp & 0xFFFF) < ButtonRect(w).left) {
            if (!w->children.empty()) SendMsg(w->children.front(), WM_LBUTTONDOWN, wp, lp);
        } else OpenDrop(hwnd);
        return 0;
    case WM_KEYDOWN: {
        Window* lw = Win(list);
        if (!lw) return 0;
        Ctl* lc = C(lw);
        if (wp == 0x73 || ((wp == 0x26 || wp == 0x28) && (Q().keys[0x12] & 0x80))) { // F4, Alt+стрелки
            if (c->dropped) CloseDrop(list, true); else OpenDrop(hwnd);
            return 0;
        }
        if (c->dropped) return SendMsg(list, msg, wp, lp);
        int n = static_cast<int>(lc->items.size()), i = lc->cur;
        if (!n) return 0;
        if (wp == 0x26) i = std::max(0, i - 1);
        else if (wp == 0x28) i = std::min(n - 1, i + 1);
        else if (wp == 0x24) i = 0;
        else if (wp == 0x23) i = n - 1;
        else return 0;
        if (i != lc->cur) {
            lc->cur = i;
            SyncEdit(hwnd);
            Redraw(hwnd);
            Notify(hwnd, 1);
        }
        return 0;
    }
    case WM_COMBO_LIST: // выбор в выпадающем списке мышью/клавишами
        Redraw(hwnd);
        return 0;
    case WM_COMMAND:
        // уведомления поля ввода — как CBN_EDITCHANGE/CBN_EDITUPDATE
        if ((wp >> 16) == 0x300) Notify(hwnd, 5);
        else if ((wp >> 16) == 0x400) Notify(hwnd, 6);
        return 0;
    case WM_SETTEXT: {
        DefWindowProc(hwnd, msg, wp, lp, unicode);
        if (!w->children.empty()) SendMsg(w->children.front(), WM_SETTEXT, 0, lp, unicode);
        else {
            int i = static_cast<int>(fwd(0x1A2, 0xFFFFFFFFu, lp));
            if (Window* lw = Win(list)) C(lw)->cur = i;
            Redraw(hwnd);
        }
        return 1;
    }
    case WM_GETTEXT: case WM_GETTEXTLENGTH: {
        if (!w->children.empty()) return SendMsg(w->children.front(), msg, wp, lp, unicode);
        Window* lw = Win(list);
        Ctl* lc = lw ? C(lw) : nullptr;
        std::u16string t = (lc && lc->cur >= 0 && lc->cur < static_cast<int>(lc->items.size())) ? lc->items[static_cast<size_t>(lc->cur)].text : u"";
        if (msg == WM_GETTEXTLENGTH) return static_cast<uint32_t>(t.size());
        return wp ? WriteText(lp, t, wp, unicode) : 0;
    }
    case 0x143: return fwd(0x180, wp, lp);                     // CB_ADDSTRING
    case 0x144: return fwd(0x182, wp, lp);                     // CB_DELETESTRING
    case 0x146: return fwd(0x18B, wp, lp);                     // CB_GETCOUNT
    case 0x147: { Window* lw = Win(list); return lw && C(lw)->cur >= 0 ? static_cast<uint32_t>(C(lw)->cur) : CB_ERR; } // CB_GETCURSEL
    case 0x148: return fwd(0x189, wp, lp);                     // CB_GETLBTEXT
    case 0x149: return fwd(0x18A, wp, lp);                     // CB_GETLBTEXTLEN
    case 0x14A: return fwd(0x181, wp, lp);                     // CB_INSERTSTRING
    case 0x14B: fwd(0x184, 0, 0); SyncEdit(hwnd); Redraw(hwnd); return 0; // CB_RESETCONTENT
    case 0x14C: return fwd(0x18F, wp, lp);                     // CB_FINDSTRING
    case 0x158: return fwd(0x1A2, wp, lp);                     // CB_FINDSTRINGEXACT
    case 0x14D: { // CB_SELECTSTRING
        uint32_t i = fwd(0x18C, wp, lp);
        SyncEdit(hwnd);
        Redraw(hwnd);
        return i;
    }
    case 0x14E: { // CB_SETCURSEL
        Window* lw = Win(list);
        if (!lw) return CB_ERR;
        Ctl* lc = C(lw);
        int32_t i = static_cast<int32_t>(wp);
        lc->cur = (i >= 0 && i < static_cast<int>(lc->items.size())) ? i : -1;
        SyncEdit(hwnd);
        Redraw(hwnd);
        return lc->cur >= 0 ? static_cast<uint32_t>(lc->cur) : CB_ERR;
    }
    case 0x14F: if (wp) OpenDrop(hwnd); else CloseDrop(list, true); return 1; // CB_SHOWDROPDOWN
    case 0x150: return fwd(0x199, wp, lp);                     // CB_GETITEMDATA
    case 0x151: return fwd(0x19A, wp, lp);                     // CB_SETITEMDATA
    case 0x152: { // CB_GETDROPPEDCONTROLRECT
        Rect sr = ScreenRect(w);
        cpu::IGuestMemory& m = *G().m;
        m.Write32(lp, static_cast<uint32_t>(sr.left)); m.Write32(lp + 4, static_cast<uint32_t>(sr.top));
        m.Write32(lp + 8, static_cast<uint32_t>(sr.right)); m.Write32(lp + 12, static_cast<uint32_t>(sr.top + c->dropHeight));
        return 1;
    }
    case 0x153: if (static_cast<int32_t>(wp) == -1) return 0; return fwd(0x1A0, 0, lp); // CB_SETITEMHEIGHT
    case 0x154: if (static_cast<int32_t>(wp) == -1) return static_cast<uint32_t>(ClosedHeight(hwnd) - 6); return fwd(0x1A1, 0, 0); // CB_GETITEMHEIGHT
    case 0x155: return 0;                                      // CB_SETEXTENDEDUI
    case 0x156: return 0;                                      // CB_GETEXTENDEDUI
    case 0x157: return c->dropped ? 1 : 0;                     // CB_GETDROPPEDSTATE
    case 0x15F: return static_cast<uint32_t>(w->rect.Width()); // CB_GETDROPPEDWIDTH
    case 0x160: return wp;                                     // CB_SETDROPPEDWIDTH
    case 0x161: return 0;                                      // CB_INITSTORAGE
    case 0x141: if (!w->children.empty()) SendMsg(w->children.front(), 0xC5, wp, 0); return 1; // CB_LIMITTEXT
    case 0x140: return !w->children.empty() ? SendMsg(w->children.front(), 0xB0, wp, lp) : CB_ERR; // CB_GETEDITSEL
    case 0x142: if (!w->children.empty()) SendMsg(w->children.front(), 0xB1, lp & 0xFFFF, lp >> 16); return 1; // CB_SETEDITSEL
    }
    (void)hasStrings;
    return DefWindowProc(hwnd, msg, wp, lp, unicode);
}

} // namespace user
} // namespace win32
