// user32_controls.cpp — системные классы окон USER32 (слой "ui2").
//
// Пока здесь регистрация классов и связь DefWindowProc с его адресом;
// сами элементы управления (кнопки, поля ввода, списки, полосы
// прокрутки) и меню — следующий этап.

#include "user32.h"

namespace win32 {
namespace user {

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
    RegisterClassInternal(wc);
}

} // namespace

void RegisterSystemClasses() {
    uint32_t defA = HostAddress("DefWindowProcA"), defW = HostAddress("DefWindowProcW");
    S().defWindowProcA = defA;
    S().defWindowProcW = defW;
    if (defA) RegisterHostWndProc(defA, &DefProcA, false);
    if (defW) RegisterHostWndProc(defW, &DefProcA, true);
    constexpr uint32_t CS_DBLCLKS = 0x8, CS_PARENTDC = 0x80, CS_SAVEBITS = 0x800, CS_GLOBALCLASS = 0x4000;
    AddSystemClass(u"Button", CS_DBLCLKS | CS_PARENTDC | CS_GLOBALCLASS, defW, 8, 0);
    AddSystemClass(u"Static", CS_DBLCLKS | CS_PARENTDC | CS_GLOBALCLASS, defW, 8, 0);
    AddSystemClass(u"Edit", CS_DBLCLKS | CS_PARENTDC | CS_GLOBALCLASS, defW, 8, 0);
    AddSystemClass(u"ListBox", CS_DBLCLKS | CS_PARENTDC | CS_GLOBALCLASS, defW, 8, 0);
    AddSystemClass(u"ComboBox", CS_DBLCLKS | CS_PARENTDC | CS_GLOBALCLASS, defW, 8, 0);
    AddSystemClass(u"ComboLBox", CS_DBLCLKS | CS_SAVEBITS | CS_GLOBALCLASS, defW, 8, 0);
    AddSystemClass(u"ScrollBar", CS_DBLCLKS | CS_PARENTDC | CS_GLOBALCLASS, defW, 8, 0);
    AddSystemClass(u"MDIClient", CS_GLOBALCLASS, defW, 8, 13);
    AddSystemClass(u"#32770", CS_SAVEBITS | CS_DBLCLKS | CS_GLOBALCLASS, defW, 30, 0);
}

// Полосы прокрутки в рамке окна (WS_VSCROLL/WS_HSCROLL): фон и кнопки
void DrawScrollBars(Window* w, uint32_t hdc) {
    if (!(w->style & (WS_VSCROLL | WS_HSCROLL))) return;
    auto box = [&](const Rect& r) {
        ui::g_gdi.FillRect(hdc, r, SysColorBrush(15));
        ui::g_gdi.FillRect(hdc, Rect(r.left, r.top, r.right - 1, r.top + 1), SysColorBrush(22));
        ui::g_gdi.FillRect(hdc, Rect(r.left, r.top, r.left + 1, r.bottom - 1), SysColorBrush(22));
        ui::g_gdi.FillRect(hdc, Rect(r.left, r.bottom - 1, r.right, r.bottom), SysColorBrush(21));
        ui::g_gdi.FillRect(hdc, Rect(r.right - 1, r.top, r.right, r.bottom), SysColorBrush(21));
    };
    const Rect& c = w->client;
    if (w->style & WS_VSCROLL) {
        Rect bar(c.right, c.top, c.right + kScrollW, c.bottom);
        ui::g_gdi.FillRect(hdc, bar, SysColorBrush(0));
        box(Rect(bar.left, bar.top, bar.right, bar.top + kScrollW));
        box(Rect(bar.left, bar.bottom - kScrollW, bar.right, bar.bottom));
    }
    if (w->style & WS_HSCROLL) {
        Rect bar(c.left, c.bottom, c.right, c.bottom + kScrollW);
        ui::g_gdi.FillRect(hdc, bar, SysColorBrush(0));
        box(Rect(bar.left, bar.top, bar.left + kScrollW, bar.bottom));
        box(Rect(bar.right - kScrollW, bar.top, bar.right, bar.bottom));
    }
    if ((w->style & WS_VSCROLL) && (w->style & WS_HSCROLL))
        ui::g_gdi.FillRect(hdc, Rect(c.right, c.bottom, c.right + kScrollW, c.bottom + kScrollW), SysColorBrush(15));
}

// Полоса меню: меню ещё не реализованы, рисуется только фон
void DrawMenuBarInto(Window*, uint32_t, const Rect&) {}

} // namespace user
} // namespace win32
