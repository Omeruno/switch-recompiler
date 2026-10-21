// comctl32_status.cpp — COMCTL32: строка состояния (msctls_statusbar32).
//
// Части (SB_SETPARTS) с текстом и стилем рамки, простой режим (SB_SIMPLE),
// рисуемые владельцем части (WM_DRAWITEM), уголок изменения размера.
// Без CCS_NORESIZE/CCS_NOPARENTALIGN строка сама встаёт к низу родителя
// при WM_SIZE, как в Windows; MFC (CStatusBar) располагает её сама.

#include <algorithm>
#include <map>
#include <vector>

#include "user32_controls.h"

namespace win32 {
namespace user {

namespace {

constexpr uint32_t SB_SETTEXTA = 0x401, SB_GETTEXTA = 0x402, SB_GETTEXTLENGTHA = 0x403, SB_SETPARTS = 0x404,
                   SB_GETPARTS = 0x406, SB_GETBORDERS = 0x407, SB_SETMINHEIGHT = 0x408, SB_SIMPLE = 0x409,
                   SB_GETRECT = 0x40A, SB_SETTEXTW = 0x40B, SB_GETTEXTLENGTHW = 0x40C, SB_GETTEXTW = 0x40D,
                   SB_ISSIMPLE = 0x40E, SB_SETICON = 0x40F, SB_GETICON = 0x414, SB_SETBKCOLOR = 0x2001;
constexpr uint32_t SBT_OWNERDRAW = 0x1000, SBT_NOBORDERS = 0x100, SBT_POPOUT = 0x200;
constexpr uint32_t SBARS_SIZEGRIP = 0x100, CCS_NORESIZE = 0x4, CCS_NOPARENTALIGN = 0x8, CCS_TOP = 0x1;
constexpr int kBorderY = 2, kGap = 2, kGrip = 13;

struct Part {
    int right = -1;
    std::u16string text;
    uint32_t flags = 0, data = 0, icon = 0;
};
struct StatusBar {
    std::vector<Part> parts = std::vector<Part>(1);
    Part simpleText;
    bool simple = false;
    int minHeight = 0;
    uint32_t bk = 0xFFFFFFFFu; // CLR_DEFAULT
};
std::map<uint32_t, StatusBar>& Bars() {
    static std::map<uint32_t, StatusBar> b;
    return b;
}

int BarHeight(Window* w, const StatusBar& sb) {
    uint32_t hdc = CtlDc(w->hwnd);
    int tw = 0, th = 0;
    ui::g_gdi.TextExtent(hdc, u"Ag", tw, th);
    ui::g_gdi.DeleteDc(hdc);
    return std::max(sb.minHeight, th + 2 * kBorderY + 4);
}

bool HasGrip(Window* w) {
    if (!(w->style & SBARS_SIZEGRIP)) return false;
    Window* p = Win(w->parent);
    return !p || !(p->style & 0x01000000); // у развёрнутого окна уголка нет
}

// Прямоугольник части i в клиентских координатах строки
Rect PartRect(Window* w, const StatusBar& sb, int i) {
    int width = w->client.Width(), height = w->client.Height();
    if (sb.simple) return Rect(0, kBorderY, width, height);
    int left = 0;
    for (int k = 0; k < i; ++k) left = (sb.parts[k].right < 0 ? width : sb.parts[k].right) + kGap;
    int right = sb.parts[i].right < 0 ? width : sb.parts[i].right;
    return Rect(std::min(left, width), kBorderY, std::min(right, width), height);
}

void DrawPart(Window* w, uint32_t hdc, const Part& p, Rect r, int index) {
    if (r.Width() <= 0) return;
    if (!(p.flags & SBT_NOBORDERS)) {
        if (p.flags & SBT_POPOUT) Edge3D(hdc, r, 20, 16, -1, -1);
        else Edge3D(hdc, r, 16, 20, -1, -1);
    }
    Rect in(r.left + 1, r.top + 1, r.right - 1, r.bottom - 1);
    if (p.flags & SBT_OWNERDRAW) {
        cpu::IGuestMemory& m = *G().m;
        StackBuffer dis(48);
        uint32_t d = dis.addr();
        m.Write32(d, 0);           // CtlType
        m.Write32(d + 4, w->id);
        m.Write32(d + 8, static_cast<uint32_t>(index));
        m.Write32(d + 12, 1);      // ODA_DRAWENTIRE
        m.Write32(d + 16, 0);
        m.Write32(d + 20, w->hwnd);
        m.Write32(d + 24, hdc);
        m.Write32(d + 28, static_cast<uint32_t>(in.left));
        m.Write32(d + 32, static_cast<uint32_t>(in.top));
        m.Write32(d + 36, static_cast<uint32_t>(in.right));
        m.Write32(d + 40, static_cast<uint32_t>(in.bottom));
        m.Write32(d + 44, p.data);
        SendMsg(w->parent, 0x2B /* WM_DRAWITEM */, w->id, d);
        return;
    }
    int x = in.left + 2;
    if (p.icon) {
        DrawImageHandle(hdc, p.icon, x, in.top + (in.Height() - 16) / 2, false);
        x += 18;
    }
    // табуляции делят текст: левая часть, по центру, справа
    std::u16string cols[3];
    int col = 0;
    for (char16_t ch : p.text) {
        if (ch == u'\t') { if (col < 2) ++col; continue; }
        cols[col] += ch;
    }
    Rect tr(x, in.top, in.right - 2, in.bottom);
    uint32_t color = ui::SysColor(18);
    const uint32_t kAlign[3] = {0x0, 0x1, 0x2}; // DT_LEFT, DT_CENTER, DT_RIGHT
    for (int k = 0; k < 3; ++k)
        if (!cols[k].empty()) CtlText(hdc, cols[k], tr, kAlign[k] | 0x4 | 0x20 | 0x800 | 0x8000, true, color);
}

void Paint(Window* w, StatusBar& sb) {
    uint32_t hwnd = w->hwnd;
    if (w->ncDirty) DrawNonClient(w);
    uint32_t hdc = CtlDc(hwnd);
    Rect rc(0, 0, w->client.Width(), w->client.Height());
    uint32_t brush = sb.bk == 0xFFFFFFFFu ? SysColorBrush(15) : ui::g_gdi.CreateSolidBrush(sb.bk);
    ui::g_gdi.FillRect(hdc, rc, brush);
    if (sb.bk != 0xFFFFFFFFu) ui::g_gdi.DeleteObject(brush);
    if (ui::DcObj* dc = ui::g_gdi.Dc(hdc)) dc->st.bkMode = 1; // TRANSPARENT
    if (sb.simple) DrawPart(w, hdc, sb.simpleText, PartRect(w, sb, 0), 255);
    else
        for (size_t i = 0; i < sb.parts.size(); ++i) {
            DrawPart(w, hdc, sb.parts[i], PartRect(w, sb, static_cast<int>(i)), static_cast<int>(i));
            if (!(w = Win(hwnd))) return;
        }
    if (HasGrip(w)) {
        // три пары диагональных штрихов в правом нижнем углу
        int r = rc.right - 1, b = rc.bottom - 1;
        for (int k = 0; k < 3; ++k) {
            int o = 3 + k * 4;
            Line(hdc, r - o, b, r, b - o, 20);
            Line(hdc, r - o + 1, b, r, b - o + 1, 16);
            Line(hdc, r - o + 2, b, r, b - o + 2, 16);
        }
    }
    ui::g_gdi.DeleteDc(hdc);
    if ((w = Win(hwnd))) { w->update.Clear(); w->erase = false; }
}

// Встать к низу (или верху) родителя по ширине его клиентской области
void Align(Window* w, StatusBar& sb) {
    if (w->style & (CCS_NORESIZE | CCS_NOPARENTALIGN)) return;
    Window* p = Win(w->parent);
    if (!p) return;
    int h = BarHeight(w, sb), pw = p->client.Width(), ph = p->client.Height();
    int y = (w->style & 0x3) == CCS_TOP ? 0 : ph - h;
    SetWindowPosInternal(w->hwnd, 0, 0, y, pw, h, 0x4 | 0x10);
}

void Invalidate(Window* w) {
    InvalidateClient(w, Region(Rect(0, 0, w->client.Width(), w->client.Height())), true, false);
}

uint32_t SetText(Window* w, StatusBar& sb, uint32_t wp, uint32_t lp, bool unicode) {
    uint32_t index = wp & 0xFF, flags = wp & 0xFF00;
    Part* p = index == 255 ? &sb.simpleText : index < sb.parts.size() ? &sb.parts[index] : nullptr;
    if (!p) return 0;
    p->flags = flags;
    p->data = lp;
    p->text = (flags & SBT_OWNERDRAW) || !lp ? std::u16string() : ReadText(lp, unicode);
    Invalidate(w);
    return 1;
}

uint32_t GetText(StatusBar& sb, uint32_t index, uint32_t lp, bool unicode, bool lengthOnly) {
    Part* p = index == 255 ? &sb.simpleText : index < sb.parts.size() ? &sb.parts[index] : nullptr;
    if (!p) return 0;
    if (p->flags & SBT_OWNERDRAW) return lengthOnly ? 0 : p->data;
    if (!lengthOnly && lp) WriteText(lp, p->text, static_cast<uint32_t>(p->text.size() + 1), unicode);
    return static_cast<uint32_t>(p->text.size()) | (p->flags << 16);
}

} // namespace

uint32_t StatusBarProc(uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp, bool unicode) {
    Window* w = Win(hwnd);
    if (!w) return 0;
    StatusBar& sb = Bars()[hwnd];
    cpu::IGuestMemory& m = *G().m;
    uint32_t r;
    if (msg != WM_SETFONT && CommonCtlMessage(hwnd, msg, wp, lp, r)) return r;
    switch (msg) {
    case WM_CREATE:
        C(w)->font = ui::g_gdi.StockObject(17); // DEFAULT_GUI_FONT
        if (lp) {
            uint32_t name = m.Read32(lp + 36);
            if (name) sb.parts[0].text = ReadText(name, unicode);
        }
        Align(w, sb);
        return 0;
    case WM_NCDESTROY:
        Bars().erase(hwnd);
        return DefWindowProc(hwnd, msg, wp, lp, unicode);
    case WM_SETFONT:
        CommonCtlMessage(hwnd, msg, wp, lp, r);
        return 0;
    case WM_SIZE:
        // приложения пересылают сюда WM_SIZE родителя
        Align(w, sb);
        Invalidate(w);
        return 0;
    case WM_PAINT:
        Paint(w, sb);
        return 0;
    case WM_SETTEXT:
        SetText(w, sb, 0, lp, unicode);
        return DefWindowProc(hwnd, msg, wp, lp, unicode);
    case SB_SETTEXTA: case SB_SETTEXTW:
        return SetText(w, sb, wp, lp, msg == SB_SETTEXTW);
    case SB_GETTEXTA: case SB_GETTEXTW:
        return GetText(sb, wp, lp, msg == SB_GETTEXTW, false);
    case SB_GETTEXTLENGTHA: case SB_GETTEXTLENGTHW:
        return GetText(sb, wp, 0, msg == SB_GETTEXTLENGTHW, true);
    case SB_SETPARTS: {
        if (!wp || wp > 256 || !lp) return 0;
        sb.parts.resize(wp);
        for (uint32_t i = 0; i < wp; ++i) sb.parts[i].right = static_cast<int32_t>(m.Read32(lp + i * 4));
        Invalidate(w);
        return 1;
    }
    case SB_GETPARTS:
        for (uint32_t i = 0; lp && i < wp && i < sb.parts.size(); ++i)
            m.Write32(lp + i * 4, static_cast<uint32_t>(sb.parts[i].right));
        return static_cast<uint32_t>(sb.parts.size());
    case SB_GETBORDERS:
        if (lp) { m.Write32(lp, 0); m.Write32(lp + 4, kBorderY); m.Write32(lp + 8, kGap); }
        return 1;
    case SB_SETMINHEIGHT:
        sb.minHeight = static_cast<int>(wp);
        return 0;
    case SB_SIMPLE:
        sb.simple = wp != 0;
        Invalidate(w);
        return 0;
    case SB_ISSIMPLE:
        return sb.simple ? 1 : 0;
    case SB_GETRECT: {
        if (!lp || (!sb.simple && wp >= sb.parts.size())) return 0;
        Rect pr = PartRect(w, sb, static_cast<int>(sb.simple ? 0 : wp));
        m.Write32(lp, static_cast<uint32_t>(pr.left));
        m.Write32(lp + 4, static_cast<uint32_t>(pr.top));
        m.Write32(lp + 8, static_cast<uint32_t>(pr.right));
        m.Write32(lp + 12, static_cast<uint32_t>(pr.bottom));
        return 1;
    }
    case SB_SETICON:
        if (wp == 0xFFFFFFFFu) sb.simpleText.icon = lp;
        else if (wp < sb.parts.size()) sb.parts[wp].icon = lp;
        else return 0;
        Invalidate(w);
        return 1;
    case SB_GETICON:
        return wp == 0xFFFFFFFFu ? sb.simpleText.icon : wp < sb.parts.size() ? sb.parts[wp].icon : 0;
    case SB_SETBKCOLOR: {
        uint32_t old = sb.bk;
        sb.bk = lp;
        Invalidate(w);
        return old;
    }
    }
    return DefWindowProc(hwnd, msg, wp, lp, unicode);
}

} // namespace user
} // namespace win32
