// user32_controls.cpp — системные классы окон USER32 (слой "ui2"):
// регистрация, кнопки (Button), надписи (Static), поля ввода (Edit).
// Списки, комбинированные списки и полосы прокрутки — user32_lists.cpp.
//
// Процедуры элементов — на стороне хоста; у каждой есть адрес host-функции,
// поэтому программа может получить его (GetWindowLong/GetClassInfo),
// подменить процедуру и вызывать старую через CallWindowProc.

#include <algorithm>

#include "user32_controls.h"

namespace win32 {
namespace user {

using namespace hle;

Ctl* C(Window* w) {
    if (!w) return nullptr;
    if (!w->controlState) w->controlState = std::make_shared<Ctl>();
    return static_cast<Ctl*>(w->controlState.get());
}

uint32_t CtlFont(Window* w) {
    Ctl* c = C(w);
    return (c && c->font) ? c->font : ui::g_gdi.StockObject(13);
}

uint32_t CtlDc(uint32_t hwnd) {
    uint32_t hdc = ui::g_gdi.CreateDc(ui::DcKind::Window, hwnd, false);
    ui::g_gdi.SelectObject(hdc, CtlFont(Win(hwnd)));
    return hdc;
}

uint32_t CtlColor(uint32_t hwnd, uint32_t hdc, uint32_t msg) {
    Window* w = Win(hwnd);
    uint32_t parent = w && (w->style & WS_CHILD) ? w->parent : 0;
    uint32_t brush = parent ? SendMsg(parent, msg, hdc, hwnd) : 0;
    if (!brush) brush = DefWindowProc(hwnd, msg, hdc, hwnd, true);
    return brush;
}

void Notify(uint32_t hwnd, uint32_t code) {
    Window* w = Win(hwnd);
    if (!w || !(w->style & WS_CHILD)) return;
    SendMsg(w->parent, WM_COMMAND, (w->id & 0xFFFF) | (code << 16), hwnd);
}

void Line(uint32_t hdc, int x0, int y0, int x1, int y1, int sysColor) {
    ui::g_gdi.FillRect(hdc, Rect(std::min(x0, x1), std::min(y0, y1), std::max(x0, x1) + 1, std::max(y0, y1) + 1), SysColorBrush(sysColor));
}

void Edge3D(uint32_t hdc, Rect r, int tlO, int brO, int tlI, int brI) {
    Line(hdc, r.left, r.top, r.right - 2, r.top, tlO);
    Line(hdc, r.left, r.top, r.left, r.bottom - 2, tlO);
    Line(hdc, r.left, r.bottom - 1, r.right - 1, r.bottom - 1, brO);
    Line(hdc, r.right - 1, r.top, r.right - 1, r.bottom - 1, brO);
    if (tlI < 0) return;
    Line(hdc, r.left + 1, r.top + 1, r.right - 3, r.top + 1, tlI);
    Line(hdc, r.left + 1, r.top + 1, r.left + 1, r.bottom - 3, tlI);
    Line(hdc, r.left + 1, r.bottom - 2, r.right - 2, r.bottom - 2, brI);
    Line(hdc, r.right - 2, r.top + 1, r.right - 2, r.bottom - 2, brI);
}

void FocusRect(uint32_t hdc, const Rect& r) {
    for (int x = r.left; x < r.right; x += 2) {
        ui::g_gdi.PatBlt(hdc, x, r.top, 1, 1, 0x550009);
        ui::g_gdi.PatBlt(hdc, x, r.bottom - 1, 1, 1, 0x550009);
    }
    for (int y = r.top + 2; y < r.bottom - 1; y += 2) {
        ui::g_gdi.PatBlt(hdc, r.left, y, 1, 1, 0x550009);
        ui::g_gdi.PatBlt(hdc, r.right - 1, y, 1, 1, 0x550009);
    }
}

void CtlText(uint32_t hdc, const std::u16string& text, Rect r, uint32_t fmt, bool enabled, uint32_t color) {
    ui::DcObj* dc = ui::g_gdi.Dc(hdc);
    dc->st.bkMode = 1;
    if (!enabled) {
        dc->st.textColor = ui::SysColor(20);
        Rect r1 = r.Offset(1, 1);
        ui::g_gdi.DrawText(hdc, text, r1, fmt);
        dc->st.textColor = ui::SysColor(16);
    } else {
        dc->st.textColor = color;
    }
    ui::g_gdi.DrawText(hdc, text, r, fmt);
}

void Redraw(uint32_t hwnd) {
    Window* w = Win(hwnd);
    if (!w || !IsVisibleChain(w) || !w->redraw) return;
    SendMsg(hwnd, WM_PAINT, 0, 0);
}

bool CommonCtlMessage(uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp, uint32_t& result) {
    Window* w = Win(hwnd);
    Ctl* c = C(w);
    switch (msg) {
    case WM_SETFONT:
        c->font = wp;
        if (lp) InvalidateClient(w, Region(Rect(0, 0, w->client.Width(), w->client.Height())), true, false);
        result = 0;
        return true;
    case WM_GETFONT:
        result = c->font;
        return true;
    case WM_ENABLE:
        InvalidateClient(w, Region(Rect(0, 0, w->client.Width(), w->client.Height())), true, false);
        result = 0;
        return true;
    case WM_ERASEBKGND:
        result = 1;
        return true;
    }
    return false;
}

namespace {

void DrawItemStruct(uint32_t parent, uint32_t type, uint32_t id, uint32_t item, uint32_t action, uint32_t state, uint32_t hwnd,
                    uint32_t hdc, const Rect& r, uint32_t data) {
    cpu::IGuestMemory& m = *G().m;
    StackBuffer dis(48);
    uint32_t p = dis.addr();
    m.Write32(p, type);
    m.Write32(p + 4, id);
    m.Write32(p + 8, item);
    m.Write32(p + 12, action);
    m.Write32(p + 16, state);
    m.Write32(p + 20, hwnd);
    m.Write32(p + 24, hdc);
    m.Write32(p + 28, static_cast<uint32_t>(r.left));
    m.Write32(p + 32, static_cast<uint32_t>(r.top));
    m.Write32(p + 36, static_cast<uint32_t>(r.right));
    m.Write32(p + 40, static_cast<uint32_t>(r.bottom));
    m.Write32(p + 44, data);
    SendMsg(parent, 0x2B /* WM_DRAWITEM */, id, p);
}

// ---------------------------------------------------------------------------
// Button

constexpr uint32_t BS_TYPEMASK = 0xF, BS_LEFTTEXT = 0x20, BS_LEFT = 0x100, BS_RIGHT = 0x200, BS_CENTER = 0x300,
                   BS_PUSHLIKE = 0x1000, BS_MULTILINE = 0x2000, BS_NOTIFY = 0x4000, BS_FLAT = 0x8000;
enum { PUSH = 0, DEFPUSH = 1, CHECK = 2, AUTOCHECK = 3, RADIO = 4, STATE3 = 5, AUTO3 = 6, GROUP = 7, USERBTN = 8, AUTORADIO = 9, OWNERDRAW = 0xB };

uint32_t BtnType(Window* w) { return w->style & BS_TYPEMASK; }
bool IsCheckLike(uint32_t t) { return t == CHECK || t == AUTOCHECK || t == RADIO || t == STATE3 || t == AUTO3 || t == AUTORADIO; }

void DrawRadio(uint32_t hdc, const Rect& b, bool face, bool checked, bool enabled) {
    // круг 12x12: тёмные дуги сверху-слева, светлые снизу-справа
    static const char* const kRadio[12] = {
        "....aaaa....", "..aabbbbaa..", ".abb....bbc.", ".ab......dc.", "ab........dc", "ab........dc",
        "ab........dc", "ab........dc", ".ab......dc.", ".bd......dc.", "..ccddddcc..", "....cccc...."};
    uint32_t inside = ui::SysColor(face ? 15 : 5);
    for (int yy = 0; yy < 12; ++yy) {
        const char* row = kRadio[yy];
        int first = -1, last = -1;
        for (int xx = 0; xx < 12; ++xx) if (row[xx] != '.') { if (first < 0) first = xx; last = xx; }
        for (int xx = 0; xx < 12; ++xx) {
            int col = -1;
            switch (row[xx]) {
            case 'a': col = 16; break;
            case 'b': col = 21; break;
            case 'c': col = 20; break;
            case 'd': col = 22; break;
            }
            if (col >= 0) ui::g_gdi.SetPixel(hdc, b.left + xx, b.top + yy, ui::SysColor(col));
            else if (xx > first && xx < last) ui::g_gdi.SetPixel(hdc, b.left + xx, b.top + yy, inside);
        }
    }
    if (checked) {
        ui::g_gdi.FillRect(hdc, Rect(b.left + 4, b.top + 5, b.left + 8, b.top + 7), SysColorBrush(enabled ? 18 : 16));
        ui::g_gdi.FillRect(hdc, Rect(b.left + 5, b.top + 4, b.left + 7, b.top + 8), SysColorBrush(enabled ? 18 : 16));
    }
}

void DrawCheckMark(uint32_t hdc, const Rect& b, int col) {
    // галочка 7x7
    static const int dy[7] = {2, 3, 4, 3, 2, 1, 0};
    for (int k = 0; k < 7; ++k) Line(hdc, b.left + 3 + k, b.top + 3 + dy[k], b.left + 3 + k, b.top + 5 + dy[k], col);
}

void PaintButton(uint32_t hwnd) {
    Window* w = Win(hwnd);
    Ctl* c = C(w);
    uint32_t type = BtnType(w);
    bool enabled = !(w->style & WS_DISABLED);
    bool focus = FocusForWindow(hwnd) == hwnd;
    Rect r(0, 0, w->client.Width(), w->client.Height());
    uint32_t hdc = CtlDc(hwnd);
    if (type == OWNERDRAW) {
        DrawItemStruct(w->parent, 4, w->id, 0, 1, (c->pushed ? 1 : 0) | (enabled ? 0 : 4) | (focus ? 0x10 : 0), hwnd, hdc, r, 0);
        ui::g_gdi.DeleteDc(hdc);
        return;
    }
    uint32_t hAlign = w->style & BS_CENTER;
    if (type == PUSH || type == DEFPUSH || type == USERBTN || (w->style & BS_PUSHLIKE)) {
        bool pushed = c->pushed || ((w->style & BS_PUSHLIKE) && c->check);
        bool def = type == DEFPUSH;
        ui::g_gdi.FillRect(hdc, r, SysColorBrush(15));
        Rect e = r;
        if (def) {
            ui::g_gdi.FrameRect(hdc, e, SysColorBrush(21));
            e = Rect(e.left + 1, e.top + 1, e.right - 1, e.bottom - 1);
        }
        if (pushed) ui::g_gdi.FrameRect(hdc, e, SysColorBrush(16));
        else if (w->style & BS_FLAT) ui::g_gdi.FrameRect(hdc, e, SysColorBrush(21));
        else Edge3D(hdc, e, 20, 21, 22, 16);
        Rect tr(r.left + 4, r.top + 2, r.right - 4, r.bottom - 2);
        if (pushed) tr = tr.Offset(1, 1);
        uint32_t fmt = (hAlign == BS_LEFT ? 0 : hAlign == BS_RIGHT ? 2 : 1) | ((w->style & BS_MULTILINE) ? 0x10 : 0x20 | 0x4);
        if (c->image) {
            DrawImageHandle(hdc, c->image, tr.left + (tr.Width() - 32) / 2, tr.top + (tr.Height() - 32) / 2, (w->style & 0x80) != 0);
        } else {
            if (w->style & BS_MULTILINE) {
                Rect calc = tr;
                ui::g_gdi.DrawText(hdc, w->text, calc, fmt | 0x400);
                tr.top += (tr.Height() - calc.Height()) / 2;
            }
            CtlText(hdc, w->text, tr, fmt, enabled, ui::SysColor(18));
        }
        if (focus) FocusRect(hdc, Rect(r.left + 4, r.top + 4, r.right - 4, r.bottom - 4));
    } else if (type == GROUP) {
        uint32_t brush = CtlColor(hwnd, hdc, WM_CTLCOLORSTATIC);
        uint32_t textColor = ui::DcColorOr(hdc, ui::SysColor(18));
        int th = ui::g_gdi.CurrentFont(hdc)->Metrics().height;
        int top = th / 2;
        Rect fr(r.left, r.top + top, r.right, r.bottom);
        ui::g_gdi.FrameRect(hdc, Rect(fr.left, fr.top, fr.right - 1, fr.bottom - 1), SysColorBrush(16));
        ui::g_gdi.FrameRect(hdc, Rect(fr.left + 1, fr.top + 1, fr.right, fr.bottom), SysColorBrush(20));
        if (!w->text.empty()) {
            Rect calc(8, 0, r.right - 8, th);
            ui::g_gdi.DrawText(hdc, w->text, calc, 0x20 | 0x400);
            ui::g_gdi.FillRect(hdc, Rect(6, 0, std::min(calc.right + 2, r.right), th), brush);
            CtlText(hdc, w->text, Rect(8, 0, r.right - 8, th), 0x20, enabled, textColor);
        }
    } else if (IsCheckLike(type)) {
        uint32_t brush = CtlColor(hwnd, hdc, WM_CTLCOLORSTATIC);
        uint32_t textColor = ui::DcColorOr(hdc, ui::SysColor(18));
        ui::g_gdi.FillRect(hdc, r, brush);
        bool radio = type == RADIO || type == AUTORADIO;
        int box = radio ? 12 : 13;
        bool leftText = (w->style & BS_LEFTTEXT) != 0;
        int bx = leftText ? r.right - box : 0;
        int by = (r.Height() - box) / 2;
        Rect b(bx, by, bx + box, by + box);
        bool face = c->pushed || !enabled;
        if (radio) {
            DrawRadio(hdc, b, face, c->check != 0, enabled);
        } else {
            ui::g_gdi.FillRect(hdc, b, SysColorBrush(face || c->check == 2 ? 15 : 5));
            Edge3D(hdc, b, 16, 20, 21, 22);
            if (c->check) DrawCheckMark(hdc, b, (c->check == 2 || !enabled) ? 16 : 18);
        }
        Rect tr = leftText ? Rect(0, 0, bx - 4, r.bottom) : Rect(box + 4, 0, r.right, r.bottom);
        uint32_t fmt = (w->style & BS_MULTILINE) ? 0x10 : 0x20 | 0x4;
        if (hAlign == BS_CENTER) fmt |= 1;
        else if (hAlign == BS_RIGHT) fmt |= 2;
        if (w->style & BS_MULTILINE) {
            Rect calc = tr;
            ui::g_gdi.DrawText(hdc, w->text, calc, fmt | 0x400);
            tr.top += std::max(0, (tr.Height() - calc.Height()) / 2);
        }
        CtlText(hdc, w->text, tr, fmt, enabled, textColor);
        if (focus && !w->text.empty()) {
            Rect calc = tr;
            ui::g_gdi.DrawText(hdc, w->text, calc, (fmt & ~0x4u) | 0x400);
            int ty = (w->style & BS_MULTILINE) ? tr.top : (r.Height() - calc.Height()) / 2;
            FocusRect(hdc, Rect(tr.left - 1, ty, std::min(calc.right + 2, r.right), ty + calc.Height()));
        }
    }
    ui::g_gdi.DeleteDc(hdc);
}

void ClickButton(uint32_t hwnd) {
    Window* w = Win(hwnd);
    Ctl* c = C(w);
    uint32_t type = BtnType(w);
    if (type == AUTOCHECK) c->check = c->check ? 0 : 1;
    else if (type == AUTO3) c->check = (c->check + 1) % 3;
    else if (type == AUTORADIO) {
        if (Window* p = Win(w->parent)) {
            std::vector<uint32_t> v(p->children); // z-порядок = порядок создания
            size_t i = static_cast<size_t>(std::find(v.begin(), v.end(), hwnd) - v.begin());
            size_t gs = i;
            while (gs > 0 && Win(v[gs]) && !(Win(v[gs])->style & 0x20000)) --gs;
            size_t ge = i + 1;
            while (ge < v.size() && Win(v[ge]) && !(Win(v[ge])->style & 0x20000)) ++ge;
            for (size_t k = gs; k < ge && k < v.size(); ++k) {
                Window* x = Win(v[k]);
                if (x && v[k] != hwnd && x->cls && x->cls->name == u"Button" && BtnType(x) == AUTORADIO && C(x)->check) {
                    C(x)->check = 0;
                    Redraw(v[k]);
                }
            }
        }
        c->check = 1;
    }
    Redraw(hwnd);
    Notify(hwnd, 0); // BN_CLICKED
}

} // namespace

uint32_t ButtonProc(uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp, bool unicode) {
    Window* w = Win(hwnd);
    if (!w) return 0;
    Ctl* c = C(w);
    uint32_t r;
    if (CommonCtlMessage(hwnd, msg, wp, lp, r)) return r;
    uint32_t type = BtnType(w);
    switch (msg) {
    case WM_PAINT:
        if (w->ncDirty) DrawNonClient(w);
        PaintButton(hwnd);
        if ((w = Win(hwnd))) { w->update.Clear(); w->erase = false; }
        return 0;
    case WM_SETTEXT: {
        uint32_t res = DefWindowProc(hwnd, msg, wp, lp, unicode);
        Redraw(hwnd);
        return res;
    }
    case 0x87: // WM_GETDLGCODE
        if (type == RADIO || type == AUTORADIO) return 0x2000 | 0x40;
        if (type == GROUP) return 0x100;
        if (type == DEFPUSH) return 0x2000 | 0x10;
        if (type == PUSH || type == OWNERDRAW || type == USERBTN) return 0x2000 | 0x20;
        return 0x2000;
    case WM_NCHITTEST:
        if (type == GROUP) return HTTRANSPARENT;
        return DefWindowProc(hwnd, msg, wp, lp, unicode);
    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK:
        if (type == GROUP) return 0;
        if (msg == WM_LBUTTONDBLCLK && (type == RADIO || type == AUTORADIO || (w->style & BS_NOTIFY) || type == OWNERDRAW))
            Notify(hwnd, 5); // BN_DOUBLECLICKED
        SetCaptureInternal(hwnd);
        SetFocusInternal(hwnd);
        if (!(w = Win(hwnd))) return 0;
        c->captured = S().capture==hwnd;
        c->pushed = c->captured;
        Redraw(hwnd);
        return 0;
    case WM_MOUSEMOVE:
        if (c->captured) {
            int x = static_cast<int16_t>(lp & 0xFFFF), y = static_cast<int16_t>(lp >> 16);
            bool inside = Rect(0, 0, w->client.Width(), w->client.Height()).Contains(x, y);
            if (inside != c->pushed) { c->pushed = inside; Redraw(hwnd); }
        }
        return 0;
    case WM_LBUTTONUP:
        if (c->captured) {
            c->captured = false;
            if (S().capture == hwnd) ReleaseCaptureInternal();
            bool click = c->pushed;
            c->pushed = false;
            Redraw(hwnd);
            if (click) ClickButton(hwnd);
        }
        return 0;
    case WM_CAPTURECHANGED:
        if (c->captured && lp != hwnd) { c->captured = false; c->pushed = false; Redraw(hwnd); }
        return 0;
    case WM_KEYDOWN:
        if (wp == 0x20 && !c->pushed) { c->pushed = true; Redraw(hwnd); }
        return 0;
    case WM_KEYUP:
        if (wp == 0x20 && c->pushed) { c->pushed = false; Redraw(hwnd); ClickButton(hwnd); }
        return 0;
    case WM_SETFOCUS:
        Redraw(hwnd);
        if (w->style & BS_NOTIFY) Notify(hwnd, 6); // BN_SETFOCUS
        return 0;
    case WM_KILLFOCUS:
        c->pushed = false;
        Redraw(hwnd);
        if (w->style & BS_NOTIFY) Notify(hwnd, 7);
        return 0;
    case 0xF0: // BM_GETCHECK
        return (IsCheckLike(type) || (w->style & BS_PUSHLIKE)) ? static_cast<uint32_t>(c->check) : 0;
    case 0xF1: // BM_SETCHECK
        if (IsCheckLike(type) || (w->style & BS_PUSHLIKE)) { c->check = static_cast<int>(wp > 2 ? 1 : wp); Redraw(hwnd); }
        return 0;
    case 0xF2: // BM_GETSTATE
        return static_cast<uint32_t>(c->check) | (c->pushed ? 4 : 0) | (FocusForWindow(hwnd) == hwnd ? 8 : 0);
    case 0xF3: // BM_SETSTATE
        c->pushed = wp != 0;
        Redraw(hwnd);
        return 0;
    case 0xF4: // BM_SETSTYLE
        w->style = (w->style & ~0xFFFFu) | (wp & 0xFFFF);
        if (lp) Redraw(hwnd);
        return 0;
    case 0xF5: // BM_CLICK
        if (!(w->style & WS_DISABLED)) ClickButton(hwnd);
        return 0;
    case 0xF6: return c->image;              // BM_GETIMAGE
    case 0xF7: { uint32_t o = c->image; c->image = lp; Redraw(hwnd); return o; } // BM_SETIMAGE
    }
    return DefWindowProc(hwnd, msg, wp, lp, unicode);
}

// ---------------------------------------------------------------------------
// Static

uint32_t StaticProc(uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp, bool unicode) {
    Window* w = Win(hwnd);
    if (!w) return 0;
    Ctl* c = C(w);
    uint32_t r;
    if (CommonCtlMessage(hwnd, msg, wp, lp, r)) return r;
    uint32_t type = w->style & 0x1F;
    switch (msg) {
    case WM_PAINT: {
        if (w->ncDirty) DrawNonClient(w);
        Rect rc(0, 0, w->client.Width(), w->client.Height());
        uint32_t hdc = CtlDc(hwnd);
        bool enabled = !(w->style & WS_DISABLED);
        switch (type) {
        case 4: ui::g_gdi.FillRect(hdc, rc, SysColorBrush(21)); break;   // SS_BLACKRECT
        case 5: ui::g_gdi.FillRect(hdc, rc, SysColorBrush(16)); break;   // SS_GRAYRECT
        case 6: ui::g_gdi.FillRect(hdc, rc, SysColorBrush(20)); break;   // SS_WHITERECT
        case 7: ui::g_gdi.FrameRect(hdc, rc, SysColorBrush(21)); break;  // SS_BLACKFRAME
        case 8: ui::g_gdi.FrameRect(hdc, rc, SysColorBrush(16)); break;  // SS_GRAYFRAME
        case 9: ui::g_gdi.FrameRect(hdc, rc, SysColorBrush(20)); break;  // SS_WHITEFRAME
        case 0x10: Line(hdc, 0, 0, rc.right - 1, 0, 16); Line(hdc, 0, 1, rc.right - 1, 1, 20); break;       // SS_ETCHEDHORZ
        case 0x11: Line(hdc, 0, 0, 0, rc.bottom - 1, 16); Line(hdc, 1, 0, 1, rc.bottom - 1, 20); break;     // SS_ETCHEDVERT
        case 0x12: // SS_ETCHEDFRAME
            ui::g_gdi.FrameRect(hdc, Rect(0, 0, rc.right - 1, rc.bottom - 1), SysColorBrush(16));
            ui::g_gdi.FrameRect(hdc, Rect(1, 1, rc.right, rc.bottom), SysColorBrush(20));
            break;
        case 0xD: // SS_OWNERDRAW
            DrawItemStruct(w->parent, 5, w->id, 0, 1, enabled ? 0 : 4, hwnd, hdc, rc, 0);
            break;
        case 3: case 0xE: { // SS_ICON, SS_BITMAP
            uint32_t brush = CtlColor(hwnd, hdc, WM_CTLCOLORSTATIC);
            ui::g_gdi.FillRect(hdc, rc, brush);
            if (c->image) DrawImageHandle(hdc, c->image, 0, 0, type == 0xE);
            break;
        }
        default: {
            uint32_t brush = CtlColor(hwnd, hdc, WM_CTLCOLORSTATIC);
            uint32_t color = ui::DcColorOr(hdc, ui::SysColor(18));
            if (type != 0xB) ui::g_gdi.FillRect(hdc, rc, brush);
            uint32_t fmt = 0;
            if (type == 1) fmt |= 1;
            else if (type == 2) fmt |= 2;
            if (type == 0xB || type == 0xC) fmt |= 0x20;
            else fmt |= 0x10;
            if (w->style & 0x80) fmt |= 0x800;                 // SS_NOPREFIX
            if (w->style & 0x200) fmt |= 0x20 | 0x4;           // SS_CENTERIMAGE
            if ((w->style & 0xC000) == 0x4000) fmt |= 0x8000;  // SS_ENDELLIPSIS
            CtlText(hdc, w->text, rc, fmt, enabled, color);
        }
        }
        if (w->style & 0x1000) Edge3D(hdc, rc, 16, 20, -1, -1); // SS_SUNKEN
        ui::g_gdi.DeleteDc(hdc);
        if ((w = Win(hwnd))) { w->update.Clear(); w->erase = false; }
        return 0;
    }
    case WM_CREATE:
        if ((type == 3 || type == 0xE) && !w->text.empty()) {
            uint16_t id = (w->text[0] == 0xFFFF && w->text.size() > 1) ? w->text[1] : 0;
            c->image = LoadImageForStatic(w->hInstance, id, w->text, type == 0xE);
            if (c->image && type == 3 && !(w->style & 0x40 /* SS_REALSIZEIMAGE */)) {
                SetWindowPosInternal(hwnd, 0, 0, 0, 32, 32, 0x2 | 0x4 | 0x10);
            }
        }
        return 0;
    case WM_SETTEXT: {
        uint32_t res = DefWindowProc(hwnd, msg, wp, lp, unicode);
        if (type != 3 && type != 0xE) Redraw(hwnd);
        return res;
    }
    case WM_NCHITTEST:
        return (w->style & 0x100) ? HTCLIENT : HTTRANSPARENT; // SS_NOTIFY
    case WM_LBUTTONDOWN:
        if (w->style & 0x100) Notify(hwnd, 0); // STN_CLICKED
        return 0;
    case WM_LBUTTONDBLCLK:
        if (w->style & 0x100) Notify(hwnd, 1);
        return 0;
    case 0x87:
        return 0x100; // DLGC_STATIC
    case 0x170: case 0x172: { // STM_SETICON / STM_SETIMAGE
        uint32_t o = c->image;
        c->image = msg == 0x170 ? wp : lp;
        Redraw(hwnd);
        return o;
    }
    case 0x171: case 0x173:
        return c->image;
    }
    return DefWindowProc(hwnd, msg, wp, lp, unicode);
}

// ---------------------------------------------------------------------------
// Edit

namespace {

constexpr uint32_t ES_CENTER = 0x1, ES_RIGHT = 0x2, ES_MULTILINE = 0x4, ES_UPPERCASE = 0x8, ES_LOWERCASE = 0x10,
                   ES_PASSWORD = 0x20, ES_NOHIDESEL = 0x100, ES_READONLY = 0x800, ES_WANTRETURN = 0x1000,
                   ES_NUMBER = 0x2000;

std::u16string g_clipboard;

std::u16string Shown(Window* w, Ctl* c) {
    if (!(w->style & ES_PASSWORD)) return w->text;
    return std::u16string(w->text.size(), c->passChar ? c->passChar : u'*');
}

std::vector<size_t> LineStarts(const std::u16string& t) {
    std::vector<size_t> v{0};
    for (size_t i = 0; i < t.size(); ++i)
        if (t[i] == '\n') v.push_back(i + 1);
    return v;
}
size_t LineOf(const std::vector<size_t>& starts, size_t pos) {
    size_t l = 0;
    while (l + 1 < starts.size() && starts[l + 1] <= pos) ++l;
    return l;
}
size_t LineEnd(const std::u16string& t, const std::vector<size_t>& starts, size_t l) {
    size_t e = l + 1 < starts.size() ? starts[l + 1] - 1 : t.size();
    if (e > starts[l] && t[e - 1] == '\r') --e;
    return e;
}

int TextW(uint32_t hdc, const std::u16string& s) {
    int w, h;
    ui::g_gdi.TextExtent(hdc, s, w, h);
    return w;
}

void EnsureCaretVisible(uint32_t hwnd) {
    Window* w = Win(hwnd);
    Ctl* c = C(w);
    uint32_t hdc = CtlDc(hwnd);
    int lineH = std::max(1, ui::g_gdi.CurrentFont(hdc)->Metrics().height);
    std::u16string shown = Shown(w, c);
    int width = w->client.Width() - 4;
    if (w->style & ES_MULTILINE) {
        std::vector<size_t> st = LineStarts(shown);
        size_t l = LineOf(st, c->caret);
        int visible = std::max(1, (w->client.Height() - 2) / lineH);
        if (static_cast<int>(l) < c->topLine) c->topLine = static_cast<int>(l);
        if (static_cast<int>(l) >= c->topLine + visible) c->topLine = static_cast<int>(l) - visible + 1;
        int x = TextW(hdc, shown.substr(st[l], c->caret - st[l]));
        if (x - c->scrollX > width) c->scrollX = x - width + width / 3;
        if (x < c->scrollX) c->scrollX = std::max(0, x - width / 3);
    } else {
        int x = TextW(hdc, shown.substr(0, c->caret));
        if (x - c->scrollX > width) c->scrollX = x - width + width / 4;
        if (x < c->scrollX) c->scrollX = std::max(0, x - width / 4);
    }
    ui::g_gdi.DeleteDc(hdc);
}

void PaintEdit(uint32_t hwnd) {
    Window* w = Win(hwnd);
    Ctl* c = C(w);
    Rect rc(0, 0, w->client.Width(), w->client.Height());
    uint32_t hdc = CtlDc(hwnd);
    bool enabled = !(w->style & WS_DISABLED);
    bool readonly = (w->style & ES_READONLY) != 0;
    uint32_t brush = CtlColor(hwnd, hdc, (!enabled || readonly) ? WM_CTLCOLORSTATIC : WM_CTLCOLOREDIT);
    ui::g_gdi.FillRect(hdc, rc, brush);
    ui::DcObj* dc = ui::g_gdi.Dc(hdc);
    uint32_t textColor = enabled ? dc->st.textColor : ui::SysColor(17);
    int lineH = ui::g_gdi.CurrentFont(hdc)->Metrics().height;
    std::u16string shown = Shown(w, c);
    size_t s0 = std::min(c->caret, c->anchor), s1 = std::max(c->caret, c->anchor);
    bool focus = FocusForWindow(hwnd) == hwnd;
    bool showSel = focus || (w->style & ES_NOHIDESEL);
    dc->st.bkMode = 1;
    auto drawRun = [&](int x, int y, size_t from, size_t to) {
        size_t a = std::max(from, s0), b = std::min(to, s1);
        dc->st.textColor = textColor;
        if (!showSel || a >= b) {
            ui::g_gdi.TextOut(hdc, x, y, shown.substr(from, to - from));
            return;
        }
        int xa = x + TextW(hdc, shown.substr(from, a - from));
        int xb = xa + TextW(hdc, shown.substr(a, b - a));
        ui::g_gdi.TextOut(hdc, x, y, shown.substr(from, a - from));
        ui::g_gdi.FillRect(hdc, Rect(xa, y, xb, y + lineH), SysColorBrush(focus ? 13 : 15));
        dc->st.textColor = ui::SysColor(focus ? 14 : 18);
        ui::g_gdi.TextOut(hdc, xa, y, shown.substr(a, b - a));
        dc->st.textColor = textColor;
        ui::g_gdi.TextOut(hdc, xb, y, shown.substr(b, to - b));
    };
    int caretX = -1, caretY = 0;
    if (w->style & ES_MULTILINE) {
        std::vector<size_t> st = LineStarts(shown);
        size_t caretLine = LineOf(st, c->caret);
        for (int l = c->topLine; l < static_cast<int>(st.size()); ++l) {
            int y = 1 + (l - c->topLine) * lineH;
            if (y >= rc.bottom) break;
            size_t from = st[static_cast<size_t>(l)], to = LineEnd(shown, st, static_cast<size_t>(l));
            int x = 2 - c->scrollX;
            drawRun(x, y, from, to);
            if (caretLine == static_cast<size_t>(l)) {
                caretX = x + TextW(hdc, shown.substr(from, c->caret - from));
                caretY = y;
            }
        }
    } else {
        int tw = TextW(hdc, shown);
        int x = 2 - c->scrollX;
        if ((w->style & ES_RIGHT) && tw < rc.Width() - 4) x = rc.right - 2 - tw;
        else if ((w->style & ES_CENTER) && tw < rc.Width() - 4) x = (rc.Width() - tw) / 2;
        int y = rc.Height() > lineH + 6 ? 1 : std::max(0, (rc.Height() - lineH) / 2);
        drawRun(x, y, 0, shown.size());
        caretX = x + TextW(hdc, shown.substr(0, c->caret));
        caretY = y;
    }
    if (focus && caretX >= 0 && !readonly)
        ui::g_gdi.FillRect(hdc, Rect(caretX, caretY, caretX + 1, caretY + lineH), SysColorBrush(8));
    ui::g_gdi.DeleteDc(hdc);
}

void Changed(uint32_t hwnd) {
    Window* w = Win(hwnd);
    if (!w) return;
    C(w)->modified = true;
    EnsureCaretVisible(hwnd);
    Redraw(hwnd);
    Notify(hwnd, 0x400); // EN_UPDATE
    Notify(hwnd, 0x300); // EN_CHANGE
}

void ReplaceSel(uint32_t hwnd, const std::u16string& ins) {
    Window* w = Win(hwnd);
    Ctl* c = C(w);
    size_t s0 = std::min(c->caret, c->anchor), s1 = std::max(c->caret, c->anchor);
    s1 = std::min(s1, w->text.size());
    s0 = std::min(s0, s1);
    std::u16string add = ins;
    if (!(w->style & ES_MULTILINE)) {
        size_t nl = add.find_first_of(u"\r\n");
        if (nl != std::u16string::npos) add = add.substr(0, nl);
    }
    if (w->style & ES_UPPERCASE) for (char16_t& ch : add) if (ch >= 'a' && ch <= 'z') ch = static_cast<char16_t>(ch - 32);
    if (w->style & ES_LOWERCASE) for (char16_t& ch : add) if (ch >= 'A' && ch <= 'Z') ch = static_cast<char16_t>(ch + 32);
    size_t rest = w->text.size() - (s1 - s0);
    size_t room = c->limit > rest ? c->limit - rest : 0;
    if (add.size() > room) { add = add.substr(0, room); Notify(hwnd, 0x501); } // EN_MAXTEXT
    c->undo = w->text;
    w->text = w->text.substr(0, s0) + add + w->text.substr(s1);
    c->caret = c->anchor = s0 + add.size();
}

size_t PosFromPoint(uint32_t hwnd, int x, int y) {
    Window* w = Win(hwnd);
    Ctl* c = C(w);
    uint32_t hdc = CtlDc(hwnd);
    int lineH = std::max(1, ui::g_gdi.CurrentFont(hdc)->Metrics().height);
    std::u16string shown = Shown(w, c);
    size_t from = 0, to = shown.size();
    if (w->style & ES_MULTILINE) {
        std::vector<size_t> st = LineStarts(shown);
        int l = c->topLine + std::max(0, y - 1) / lineH;
        l = std::min(l, static_cast<int>(st.size()) - 1);
        from = st[static_cast<size_t>(l)];
        to = LineEnd(shown, st, static_cast<size_t>(l));
    }
    size_t best = from;
    int px = 2 - c->scrollX;
    for (size_t i = from; i < to; ++i) {
        int adv = TextW(hdc, shown.substr(i, 1));
        if (x < px + adv / 2) break;
        px += adv;
        best = i + 1;
    }
    ui::g_gdi.DeleteDc(hdc);
    return best;
}

} // namespace

uint32_t EditProc(uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp, bool unicode) {
    Window* w = Win(hwnd);
    if (!w) return 0;
    Ctl* c = C(w);
    uint32_t r;
    if (CommonCtlMessage(hwnd, msg, wp, lp, r)) {
        if (msg == WM_ENABLE) Redraw(hwnd);
        return r;
    }
    bool multi = (w->style & ES_MULTILINE) != 0;
    bool readonly = (w->style & ES_READONLY) != 0;
    bool shift = (Q().keys[0x10] & 0x80) != 0, ctrl = (Q().keys[0x11] & 0x80) != 0;
    cpu::IGuestMemory& m = *G().m;
    switch (msg) {
    case WM_NCCREATE:
        // в диалогах WS_BORDER у поля ввода — утопленная рамка
        if (w->style & WS_BORDER) { w->style &= ~WS_BORDER; w->exStyle |= WS_EX_CLIENTEDGE; }
        c->limit = multi ? 0x7FFFFFFE : 30000;
        return DefWindowProc(hwnd, msg, wp, lp, unicode);
    case WM_PAINT:
        if (w->ncDirty) DrawNonClient(w);
        PaintEdit(hwnd);
        if ((w = Win(hwnd))) { w->update.Clear(); w->erase = false; }
        return 0;
    case WM_SETTEXT: {
        DefWindowProc(hwnd, msg, wp, lp, unicode);
        if (w->text.size() > c->limit) w->text.resize(c->limit);
        c->caret = c->anchor = 0;
        c->scrollX = 0;
        c->topLine = 0;
        c->modified = false;
        Redraw(hwnd);
        Notify(hwnd, 0x400);
        Notify(hwnd, 0x300);
        return 1;
    }
    case 0x87: { // WM_GETDLGCODE
        uint32_t code = 0x80 | 0x8 | 0x1; // WANTCHARS | HASSETSEL | WANTARROWS
        if (multi && (w->style & ES_WANTRETURN)) code |= 0x4;
        return code;
    }
    case WM_SETFOCUS:
        Redraw(hwnd);
        Notify(hwnd, 0x100); // EN_SETFOCUS
        return 0;
    case WM_KILLFOCUS:
        Redraw(hwnd);
        Notify(hwnd, 0x200);
        return 0;
    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: {
        if (FocusForWindow(hwnd) != hwnd) SetFocusInternal(hwnd);
        if (!(w = Win(hwnd))) return 0;
        // Switch: экранная клавиатура вместо физической
        if (g_platform.textInput && !readonly && !(w->style & WS_DISABLED)) {
            std::u16string out;
            if (g_platform.textInput(w->text, multi, (w->style & ES_PASSWORD) != 0, out)) {
                c->anchor = 0;
                c->caret = w->text.size();
                ReplaceSel(hwnd, out);
                Changed(hwnd);
            }
            return 0;
        }
        size_t pos = PosFromPoint(hwnd, static_cast<int16_t>(lp & 0xFFFF), static_cast<int16_t>(lp >> 16));
        if (msg == WM_LBUTTONDBLCLK) {
            size_t a = pos, b = pos;
            while (a > 0 && w->text[a - 1] != ' ' && w->text[a - 1] != '\n') --a;
            while (b < w->text.size() && w->text[b] != ' ' && w->text[b] != '\r' && w->text[b] != '\n') ++b;
            c->anchor = a;
            c->caret = b;
        } else {
            c->caret = pos;
            if (!(wp & 4)) c->anchor = pos; // MK_SHIFT
            c->captured = true;
            SetCaptureInternal(hwnd);
        }
        EnsureCaretVisible(hwnd);
        Redraw(hwnd);
        return 0;
    }
    case WM_MOUSEMOVE:
        if (c->captured) {
            c->caret = PosFromPoint(hwnd, static_cast<int16_t>(lp & 0xFFFF), static_cast<int16_t>(lp >> 16));
            EnsureCaretVisible(hwnd);
            Redraw(hwnd);
        }
        return 0;
    case WM_LBUTTONUP:
        if (c->captured) { c->captured = false; if (S().capture == hwnd) ReleaseCaptureInternal(); }
        return 0;
    case WM_CHAR: {
        uint32_t ch = wp & 0xFFFF;
        if (!unicode) ch &= 0xFF;
        if (ch == 1) { c->anchor = 0; c->caret = w->text.size(); Redraw(hwnd); return 0; } // Ctrl+A
        if (ch == 3) { SendMsg(hwnd, WM_COPY, 0, 0); return 0; }
        if (ch == 0x16) { SendMsg(hwnd, WM_PASTE, 0, 0); return 0; }
        if (ch == 0x18) { SendMsg(hwnd, WM_CUT, 0, 0); return 0; }
        if (ch == 0x1A) { SendMsg(hwnd, WM_UNDO, 0, 0); return 0; }
        if (readonly) return 0;
        if (ch == 8) {
            if (c->caret == c->anchor && c->caret > 0) {
                c->anchor = c->caret - 1;
                if (c->anchor > 0 && w->text[c->anchor] == '\n' && w->text[c->anchor - 1] == '\r') --c->anchor;
            }
            ReplaceSel(hwnd, u"");
            Changed(hwnd);
            return 0;
        }
        if (ch == '\r') { if (multi) { ReplaceSel(hwnd, u"\r\n"); Changed(hwnd); } return 0; }
        if (ch == '\t' && !multi) return 0;
        if (ch < 0x20 && ch != '\t') return 0;
        if ((w->style & ES_NUMBER) && (ch < '0' || ch > '9')) return 0;
        ReplaceSel(hwnd, std::u16string(1, static_cast<char16_t>(ch)));
        Changed(hwnd);
        return 0;
    }
    case WM_KEYDOWN: {
        size_t len = w->text.size();
        std::vector<size_t> st = LineStarts(w->text);
        size_t pos = c->caret;
        switch (wp) {
        case 0x25: if (pos > 0) { --pos; if (pos > 0 && w->text[pos] == '\n' && w->text[pos - 1] == '\r') --pos; } break;
        case 0x27: if (pos < len) { if (w->text[pos] == '\r' && pos + 1 < len && w->text[pos + 1] == '\n') ++pos; ++pos; } break;
        case 0x24: pos = multi && !ctrl ? st[LineOf(st, pos)] : 0; break;
        case 0x23: pos = multi && !ctrl ? LineEnd(w->text, st, LineOf(st, pos)) : len; break;
        case 0x26: case 0x28: {
            if (!multi) return 0;
            size_t l = LineOf(st, pos);
            if (wp == 0x26 && l == 0) return 0;
            if (wp == 0x28 && l + 1 >= st.size()) return 0;
            size_t col = pos - st[l];
            size_t nl = wp == 0x26 ? l - 1 : l + 1;
            pos = std::min(st[nl] + col, LineEnd(w->text, st, nl));
            break;
        }
        case 0x2E: // Delete
            if (readonly) return 0;
            if (c->caret == c->anchor && c->caret < len)
                c->anchor = c->caret + ((w->text[c->caret] == '\r' && c->caret + 1 < len) ? 2 : 1);
            ReplaceSel(hwnd, u"");
            Changed(hwnd);
            return 0;
        default:
            return 0;
        }
        c->caret = pos;
        if (!shift) c->anchor = pos;
        EnsureCaretVisible(hwnd);
        Redraw(hwnd);
        return 0;
    }
    case WM_COPY: {
        size_t s0 = std::min(c->caret, c->anchor), s1 = std::max(c->caret, c->anchor);
        if (!(w->style & ES_PASSWORD) && s1 > s0) g_clipboard = w->text.substr(s0, s1 - s0);
        return 0;
    }
    case WM_CUT:
        if (readonly) return 0;
        SendMsg(hwnd, WM_COPY, 0, 0);
        ReplaceSel(hwnd, u"");
        Changed(hwnd);
        return 0;
    case WM_PASTE:
        if (readonly) return 0;
        ReplaceSel(hwnd, g_clipboard);
        Changed(hwnd);
        return 0;
    case WM_CLEAR:
        if (readonly) return 0;
        ReplaceSel(hwnd, u"");
        Changed(hwnd);
        return 0;
    case WM_UNDO: case 0xC7: { // EM_UNDO
        std::u16string t = w->text;
        w->text = c->undo;
        c->undo = t;
        c->caret = c->anchor = w->text.size();
        Changed(hwnd);
        return 1;
    }
    case 0xC6: return 1;                                           // EM_CANUNDO
    case 0xCD: c->undo = w->text; return 0;                        // EM_EMPTYUNDOBUFFER
    case 0xB0: { // EM_GETSEL
        size_t s0 = std::min(c->caret, c->anchor), s1 = std::max(c->caret, c->anchor);
        if (wp) m.Write32(wp, static_cast<uint32_t>(s0));
        if (lp) m.Write32(lp, static_cast<uint32_t>(s1));
        return static_cast<uint32_t>((s0 & 0xFFFF) | (s1 << 16));
    }
    case 0xB1: { // EM_SETSEL
        int32_t a = static_cast<int32_t>(wp), b = static_cast<int32_t>(lp);
        size_t len = w->text.size();
        if (a == -1) c->anchor = c->caret;
        else {
            if (b < 0) b = static_cast<int32_t>(len);
            c->anchor = std::min(static_cast<size_t>(a), len);
            c->caret = std::min(static_cast<size_t>(b), len);
        }
        EnsureCaretVisible(hwnd);
        Redraw(hwnd);
        return 0;
    }
    case 0xB2: // EM_GETRECT
        if (lp) {
            m.Write32(lp, 2);
            m.Write32(lp + 4, 1);
            m.Write32(lp + 8, static_cast<uint32_t>(w->client.Width() - 2));
            m.Write32(lp + 12, static_cast<uint32_t>(w->client.Height() - 1));
        }
        return 0;
    case 0xB3: case 0xB4: return 0; // EM_SETRECT(NP)
    case 0xB5: { // EM_SCROLL
        int d = wp == 0 ? -1 : wp == 1 ? 1 : wp == 2 ? -10 : wp == 3 ? 10 : 0;
        c->topLine = std::max(0, c->topLine + d);
        Redraw(hwnd);
        return 0x10000u | static_cast<uint32_t>(d & 0xFFFF);
    }
    case 0xB6: // EM_LINESCROLL
        c->topLine = std::max(0, c->topLine + static_cast<int32_t>(lp));
        Redraw(hwnd);
        return 1;
    case 0xB7: EnsureCaretVisible(hwnd); Redraw(hwnd); return 1;   // EM_SCROLLCARET
    case 0xB8: return c->modified ? 1 : 0;                         // EM_GETMODIFY
    case 0xB9: c->modified = wp != 0; return 0;                    // EM_SETMODIFY
    case 0xBA: return multi ? static_cast<uint32_t>(LineStarts(w->text).size()) : 1; // EM_GETLINECOUNT
    case 0xBB: { // EM_LINEINDEX
        std::vector<size_t> st = LineStarts(w->text);
        int32_t l = static_cast<int32_t>(wp);
        if (l < 0) return static_cast<uint32_t>(st[LineOf(st, c->caret)]);
        return static_cast<size_t>(l) < st.size() ? static_cast<uint32_t>(st[static_cast<size_t>(l)]) : 0xFFFFFFFFu;
    }
    case 0xC1: { // EM_LINELENGTH
        std::vector<size_t> st = LineStarts(w->text);
        size_t l = static_cast<int32_t>(wp) < 0 ? LineOf(st, c->caret) : LineOf(st, wp);
        return static_cast<uint32_t>(LineEnd(w->text, st, l) - st[l]);
    }
    case 0xC9: { // EM_LINEFROMCHAR
        std::vector<size_t> st = LineStarts(w->text);
        return static_cast<uint32_t>(LineOf(st, static_cast<int32_t>(wp) < 0 ? c->caret : wp));
    }
    case 0xCE: return static_cast<uint32_t>(c->topLine);           // EM_GETFIRSTVISIBLELINE
    case 0xC4: { // EM_GETLINE
        std::vector<size_t> st = LineStarts(w->text);
        if (wp >= st.size() || !lp) return 0;
        std::u16string line = w->text.substr(st[wp], LineEnd(w->text, st, wp) - st[wp]);
        uint32_t cap = m.Read16(lp);
        if (line.size() > cap) line.resize(cap);
        for (size_t i = 0; i < line.size(); ++i) {
            if (unicode) m.Write16(lp + static_cast<uint32_t>(i) * 2, line[i]);
            else m.Write8(lp + static_cast<uint32_t>(i), static_cast<uint8_t>(line[i] < 256 ? line[i] : '?'));
        }
        return static_cast<uint32_t>(line.size());
    }
    case 0xC2: // EM_REPLACESEL
        if (lp) { ReplaceSel(hwnd, ReadText(lp, unicode)); Changed(hwnd); }
        return 0;
    case 0xC5: // EM_LIMITTEXT
        c->limit = wp ? wp : 0x7FFFFFFE;
        return 0;
    case 0xD5: return static_cast<uint32_t>(c->limit);             // EM_GETLIMITTEXT
    case 0xCC: // EM_SETPASSWORDCHAR
        c->passChar = static_cast<char16_t>(wp);
        if (wp) w->style |= ES_PASSWORD; else w->style &= ~ES_PASSWORD;
        Redraw(hwnd);
        return 0;
    case 0xD2: return c->passChar ? c->passChar : ((w->style & ES_PASSWORD) ? '*' : 0); // EM_GETPASSWORDCHAR
    case 0xCF: // EM_SETREADONLY
        if (wp) w->style |= ES_READONLY; else w->style &= ~ES_READONLY;
        Redraw(hwnd);
        return 1;
    case 0xD3: return 0;                                           // EM_SETMARGINS
    case 0xD4: return 2 | (2 << 16);                               // EM_GETMARGINS
    case 0xD7: { // EM_POSFROMCHAR
        uint32_t hdc = CtlDc(hwnd);
        std::u16string shown = Shown(w, c);
        size_t p = std::min<size_t>(wp, shown.size());
        int x = 2 - c->scrollX + TextW(hdc, shown.substr(0, p));
        ui::g_gdi.DeleteDc(hdc);
        return static_cast<uint32_t>(x & 0xFFFF) | (1u << 16);
    }
    case 0xD9: return static_cast<uint32_t>(PosFromPoint(hwnd, static_cast<int16_t>(lp & 0xFFFF), static_cast<int16_t>(lp >> 16))); // EM_CHARFROMPOS
    }
    return DefWindowProc(hwnd, msg, wp, lp, unicode);
}

// ---------------------------------------------------------------------------
// Регистрация классов

namespace {

uint32_t DefProcA(uint32_t h, uint32_t msg, uint32_t wp, uint32_t lp, bool u) { return DefWindowProc(h, msg, wp, lp, u); }

uint32_t HostAddress(const char* name) {
    int idx = FindHostFunction("USER32.dll", name);
    return idx >= 0 ? IndexToHostAddress(idx) : 0;
}

void AddSystemClass(const char16_t* name, uint32_t style, uint32_t proc, int extra, uint32_t brush) {
    WndClass wc;
    wc.name = name;
    wc.style = style;
    wc.wndProc = proc;
    wc.unicode = true;
    wc.cbWndExtra = extra;
    wc.hbrBackground = brush;
    wc.system = true;
    wc.hCursor = 0x00017F00;
    RegisterClassInternal(wc);
}

// Адреса процедур для гостя: host-функции, вызывающие ту же процедуру
#define HOSTPROC(Name, Fn)                                                             \
    HostCallOutcome Name(cpu::CpuState& c, cpu::IGuestMemory& m) {                     \
        G().c = &c;                                                                    \
        G().m = &m;                                                                    \
        return Ret(Fn(Arg(c, m, 0), Arg(c, m, 1), Arg(c, m, 2), Arg(c, m, 3), true)); \
    }
HOSTPROC(Host_ButtonProc, ButtonProc)
HOSTPROC(Host_StaticProc, StaticProc)
HOSTPROC(Host_EditProc, EditProc)
HOSTPROC(Host_ListBoxProc, ListBoxProc)
HOSTPROC(Host_ComboBoxProc, ComboBoxProc)
HOSTPROC(Host_ScrollBarProc, ScrollBarProc)
HOSTPROC(Host_MdiClientProc, DefProcA)
HOSTPROC(Host_StatusBarProc, StatusBarProc)

const HostFunctionEntry kEntries[] = {
    {"USER32.dll", "__ButtonWndProc", 16, CallConv::Stdcall, &Host_ButtonProc},
    {"USER32.dll", "__StaticWndProc", 16, CallConv::Stdcall, &Host_StaticProc},
    {"USER32.dll", "__EditWndProc", 16, CallConv::Stdcall, &Host_EditProc},
    {"USER32.dll", "__ListBoxWndProc", 16, CallConv::Stdcall, &Host_ListBoxProc},
    {"USER32.dll", "__ComboBoxWndProc", 16, CallConv::Stdcall, &Host_ComboBoxProc},
    {"USER32.dll", "__ScrollBarWndProc", 16, CallConv::Stdcall, &Host_ScrollBarProc},
    {"USER32.dll", "__MDIClientWndProc", 16, CallConv::Stdcall, &Host_MdiClientProc},
    {"USER32.dll", "__StatusBarWndProc", 16, CallConv::Stdcall, &Host_StatusBarProc},
};
const bool kRegistered = RegisterHostFunctionsLayer(kEntries, sizeof(kEntries) / sizeof(kEntries[0]), kUiLayer);

uint32_t ProcAddr(const char* name, HostWndProc fn) {
    uint32_t a = HostAddress(name);
    if (a) RegisterHostWndProc(a, fn, true);
    return a;
}

} // namespace

void RegisterSystemClasses() {
    uint32_t defA = HostAddress("DefWindowProcA"), defW = HostAddress("DefWindowProcW");
    S().defWindowProcA = defA;
    S().defWindowProcW = defW;
    if (defA) RegisterHostWndProc(defA, &DefProcA, false);
    if (defW) RegisterHostWndProc(defW, &DefProcA, true);
    constexpr uint32_t CS_DBLCLKS = 0x8, CS_PARENTDC = 0x80, CS_SAVEBITS = 0x800, CS_GLOBALCLASS = 0x4000;
    uint32_t lb = ProcAddr("__ListBoxWndProc", &ListBoxProc);
    AddSystemClass(u"Button", CS_DBLCLKS | CS_PARENTDC | CS_GLOBALCLASS, ProcAddr("__ButtonWndProc", &ButtonProc), 8, 0);
    AddSystemClass(u"Static", CS_DBLCLKS | CS_PARENTDC | CS_GLOBALCLASS, ProcAddr("__StaticWndProc", &StaticProc), 8, 0);
    AddSystemClass(u"Edit", CS_DBLCLKS | CS_PARENTDC | CS_GLOBALCLASS, ProcAddr("__EditWndProc", &EditProc), 8, 0);
    AddSystemClass(u"ListBox", CS_DBLCLKS | CS_PARENTDC | CS_GLOBALCLASS, lb, 8, 0);
    AddSystemClass(u"ComboBox", CS_DBLCLKS | CS_PARENTDC | CS_GLOBALCLASS, ProcAddr("__ComboBoxWndProc", &ComboBoxProc), 8, 0);
    AddSystemClass(u"ComboLBox", CS_DBLCLKS | CS_SAVEBITS | CS_GLOBALCLASS, lb, 8, 0);
    AddSystemClass(u"ScrollBar", CS_DBLCLKS | CS_PARENTDC | CS_GLOBALCLASS, ProcAddr("__ScrollBarWndProc", &ScrollBarProc), 8, 0);
    AddSystemClass(u"MDIClient", CS_GLOBALCLASS, ProcAddr("__MDIClientWndProc", &DefProcA), 8, 13);
    // классы COMCTL32 (в Windows их регистрирует загрузка comctl32.dll)
    AddSystemClass(u"msctls_statusbar32", CS_DBLCLKS | CS_GLOBALCLASS, ProcAddr("__StatusBarWndProc", &StatusBarProc), 0, 0);
    AddSystemClass(u"#32770", CS_SAVEBITS | CS_DBLCLKS | CS_GLOBALCLASS, DefDlgProcAddress(true), 30, 0);
}

} // namespace user
} // namespace win32
