// user32_menu.cpp — меню USER32 (слой "ui2"): объекты меню, загрузка из
// ресурсов (обычный и расширенный формат), полоса меню в рамке окна,
// выпадающие меню с модальным циклом (мышь и клавиатура), контекстные
// меню (TrackPopupMenu), таблицы ускорителей.

#include <algorithm>
#include <cstring>

#include "../runtime/process.h"
#include "../runtime/resources.h"
#include "user32.h"

namespace win32 {
namespace user {

using namespace hle;

namespace {

constexpr uint32_t MF_GRAYED = 0x1, MF_DISABLED = 0x2, MF_BITMAP = 0x4, MF_CHECKED = 0x8, MF_POPUP = 0x10,
                   MF_MENUBARBREAK = 0x20, MF_MENUBREAK = 0x40, MF_HILITE = 0x80, MF_OWNERDRAW = 0x100,
                   MF_RADIOCHECK = 0x200, MF_BYPOSITION = 0x400, MF_SEPARATOR = 0x800, MF_DEFAULT = 0x1000,
                   MF_RIGHTJUSTIFY = 0x4000;

struct MenuItem {
    uint32_t id = 0;
    std::u16string text;
    uint32_t flags = 0;       // MF_* (тип и состояние вместе)
    uint32_t subMenu = 0;
    uint32_t data = 0;
    uint32_t bitmap = 0;
    Rect rect;                // раскладка: для полосы — в координатах окна, для выпадающего — в клиентских
};

struct Menu {
    std::vector<MenuItem> items;
    bool popup = false;
    uint32_t owner = 0;       // окно, у которого это полоса меню
    int barHeight = kMenuH;
};

std::map<uint32_t, std::unique_ptr<Menu>>& Menus() {
    static std::map<uint32_t, std::unique_ptr<Menu>> m;
    return m;
}
uint32_t g_nextMenu = 0x00060010;

Menu* M(uint32_t h) {
    auto it = Menus().find(h);
    return it == Menus().end() ? nullptr : it->second.get();
}

uint32_t NewMenu(bool popup) {
    uint32_t h = g_nextMenu;
    g_nextMenu += 4;
    Menus()[h].reset(new Menu);
    Menus()[h]->popup = popup;
    return h;
}

void DestroyMenuTree(uint32_t h) {
    Menu* m = M(h);
    if (!m) return;
    std::vector<uint32_t> subs;
    for (auto& it : m->items) if (it.subMenu) subs.push_back(it.subMenu);
    Menus().erase(h);
    for (uint32_t s : subs) DestroyMenuTree(s);
}

// Поиск пункта по ID (в подменю тоже) или по позиции
MenuItem* FindItem(uint32_t hmenu, uint32_t item, bool byPos, uint32_t* ownerMenu = nullptr) {
    Menu* m = M(hmenu);
    if (!m) return nullptr;
    if (byPos) {
        if (item >= m->items.size()) return nullptr;
        if (ownerMenu) *ownerMenu = hmenu;
        return &m->items[item];
    }
    for (auto& it : m->items) {
        if (!(it.flags & MF_POPUP) && it.id == item) { if (ownerMenu) *ownerMenu = hmenu; return &it; }
    }
    for (auto& it : m->items) {
        if (it.subMenu) if (MenuItem* r = FindItem(it.subMenu, item, false, ownerMenu)) return r;
    }
    // пункт-подменю можно найти и по его хендлу как ID
    for (auto& it : m->items) if ((it.flags & MF_POPUP) && it.subMenu == item) { if (ownerMenu) *ownerMenu = hmenu; return &it; }
    return nullptr;
}

// --- Шаблоны меню из ресурсов ---

struct Reader {
    const uint8_t* p;
    size_t size, off = 0;
    uint16_t U16() { if (off + 2 > size) { off = size; return 0; } uint16_t v = static_cast<uint16_t>(p[off] | (p[off + 1] << 8)); off += 2; return v; }
    uint32_t U32() { uint32_t lo = U16(); return lo | (static_cast<uint32_t>(U16()) << 16); }
    std::u16string Str() { std::u16string s; while (off < size) { char16_t c = static_cast<char16_t>(U16()); if (!c) break; s.push_back(c); } return s; }
    void Align4(size_t base) { while ((off - base) % 4) ++off; }
};

void ParseMenu(Reader& r, uint32_t hmenu) {
    for (;;) {
        if (r.off >= r.size) return;
        uint16_t flags = r.U16();
        MenuItem it;
        it.flags = flags & ~0x80u;
        if (!(flags & MF_POPUP)) it.id = r.U16();
        it.text = r.Str();
        if (!(flags & MF_POPUP) && it.id == 0 && it.text.empty()) it.flags |= MF_SEPARATOR;
        if (flags & MF_POPUP) {
            it.subMenu = NewMenu(true);
            ParseMenu(r, it.subMenu);
        }
        M(hmenu)->items.push_back(it);
        if (flags & 0x80) return; // MF_END
    }
}

void ParseMenuEx(Reader& r, uint32_t hmenu, size_t base) {
    for (;;) {
        if (r.off >= r.size) return;
        r.Align4(base);
        MenuItem it;
        uint32_t type = r.U32(), state = r.U32();
        it.id = r.U32();
        uint16_t resInfo = r.U16();
        it.text = r.Str();
        r.Align4(base);
        it.flags = (type & (MF_BITMAP | MF_MENUBARBREAK | MF_MENUBREAK | MF_OWNERDRAW | MF_RADIOCHECK | MF_SEPARATOR | MF_RIGHTJUSTIFY)) |
                   (state & (MF_GRAYED | MF_DISABLED | MF_CHECKED | MF_HILITE | MF_DEFAULT));
        if (resInfo & 0x01) {
            r.U32(); // dwHelpId
            it.flags |= MF_POPUP;
            it.subMenu = NewMenu(true);
            ParseMenuEx(r, it.subMenu, base);
        }
        M(hmenu)->items.push_back(it);
        if (resInfo & 0x80) return;
    }
}

uint32_t LoadMenuTemplate(const uint8_t* p, size_t size) {
    Reader r{p, size};
    uint16_t version = r.U16(), offset = r.U16();
    uint32_t h = NewMenu(false);
    if (version == 1) {
        r.off = 4 + offset;
        ParseMenuEx(r, h, 0);
    } else {
        r.off = 4 + offset;
        ParseMenu(r, h);
    }
    return h;
}

// --- Отрисовка ---

uint32_t MenuFont() {
    static uint32_t f = 0;
    if (!f) {
        ui::FontDesc d;
        d.face = "Tahoma";
        d.height = -11;
        f = ui::g_gdi.CreateFont(d);
        ui::g_gdi.MarkStock(f);
    }
    return f;
}

std::u16string Label(const std::u16string& t) {
    size_t tab = t.find(u'\t');
    return tab == std::u16string::npos ? t : t.substr(0, tab);
}
std::u16string Accel(const std::u16string& t) {
    size_t tab = t.find(u'\t');
    return tab == std::u16string::npos ? std::u16string() : t.substr(tab + 1);
}

int TextWidth(const std::u16string& s) {
    ui::FontObj* f = ui::g_gdi.FontO(MenuFont());
    if (!f->font) f->font = ui::FontCache::Get(f->desc);
    std::u16string plain;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '&') { if (i + 1 < s.size() && s[i + 1] == '&') { plain.push_back('&'); ++i; } continue; }
        plain.push_back(s[i]);
    }
    return f->font->MeasureWidth(plain);
}

void LayoutBar(Menu* m, int width) {
    int x = 0, y = 0, h = kMenuH - 1;
    for (auto& it : m->items) {
        int w = TextWidth(Label(it.text)) + 16;
        if (x + w > width && x > 0) { x = 0; y += h; }
        it.rect = Rect(x, y, x + w, y + h);
        x += w;
    }
    m->barHeight = y + h + 1;
}

void DrawMenuText(uint32_t hdc, const std::u16string& text, Rect r, uint32_t color, uint32_t align) {
    ui::DcObj* dc = ui::g_gdi.Dc(hdc);
    dc->st.textColor = color;
    dc->st.bkMode = 1;
    ui::g_gdi.DrawText(hdc, text, r, align | 0x20 | 0x4);
}

void DrawItemText(uint32_t hdc, const MenuItem& it, Rect r, bool selected, bool rightAccel, int accelRight) {
    bool gray = (it.flags & (MF_GRAYED | MF_DISABLED)) != 0;
    std::u16string label = Label(it.text), accel = Accel(it.text);
    uint32_t col = selected ? ui::SysColor(14) : ui::SysColor(7);
    if (gray && !selected) {
        DrawMenuText(hdc, label, r.Offset(1, 1), ui::SysColor(20), 0);
        col = ui::SysColor(16);
    } else if (gray) {
        col = ui::SysColor(17);
    }
    DrawMenuText(hdc, label, r, col, 0);
    if (rightAccel && !accel.empty()) {
        Rect ar(r.left, r.top, accelRight, r.bottom);
        if (gray && !selected) DrawMenuText(hdc, accel, ar.Offset(1, 1), ui::SysColor(20), 2);
        DrawMenuText(hdc, accel, ar, col, 2);
    }
}

// --- Выпадающие меню: окна класса "#32768" ---

struct PopupState {
    uint32_t hmenu = 0;
    uint32_t hwnd = 0;
    int selected = -1;
};
std::vector<PopupState> g_popups;   // открытые выпадающие меню (каскад)
uint32_t g_menuOwner = 0;
uint32_t g_menuBar = 0;              // меню полосы (0 — контекстное)
int g_barSelected = -1;
bool g_menuDone = false;
uint32_t g_menuResult = 0;
bool g_returnCmd = false;            // TPM_RETURNCMD

constexpr int kItemH = 17, kSepH = 9, kCheckW = 17, kArrowW = 17, kPopupBorder = 3;

void LayoutPopup(Menu* m, int& w, int& h) {
    int textW = 0, accelW = 0;
    for (auto& it : m->items) {
        if (it.flags & MF_SEPARATOR) continue;
        textW = std::max(textW, TextWidth(Label(it.text)));
        std::u16string a = Accel(it.text);
        if (!a.empty()) accelW = std::max(accelW, TextWidth(a));
    }
    int itemW = kCheckW + textW + (accelW ? accelW + 20 : 0) + kArrowW;
    int y = 0;
    for (auto& it : m->items) {
        int ih = (it.flags & MF_SEPARATOR) ? kSepH : kItemH;
        it.rect = Rect(0, y, itemW, y + ih);
        y += ih;
    }
    w = itemW + 2 * kPopupBorder;
    h = y + 2 * kPopupBorder;
}

void PaintPopup(uint32_t hwnd) {
    PopupState* ps = nullptr;
    for (auto& p : g_popups) if (p.hwnd == hwnd) ps = &p;
    Window* w = Win(hwnd);
    Menu* m = ps ? M(ps->hmenu) : nullptr;
    if (!w || !m) return;
    uint32_t hdc = ui::g_gdi.CreateDc(ui::DcKind::Window, hwnd, false);
    Rect all(0, 0, w->client.Width(), w->client.Height());
    ui::g_gdi.FillRect(hdc, all, SysColorBrush(4));
    // рамка: выпуклая кромка
    auto line = [&](int x0, int y0, int x1, int y1, int col) { ui::g_gdi.FillRect(hdc, Rect(x0, y0, x1, y1), SysColorBrush(col)); };
    line(0, 0, all.right - 1, 1, 22);
    line(0, 0, 1, all.bottom - 1, 22);
    line(1, 1, all.right - 2, 2, 20);
    line(1, 1, 2, all.bottom - 2, 20);
    line(0, all.bottom - 1, all.right, all.bottom, 21);
    line(all.right - 1, 0, all.right, all.bottom, 21);
    line(1, all.bottom - 2, all.right - 1, all.bottom - 1, 16);
    line(all.right - 2, 1, all.right - 1, all.bottom - 1, 16);
    uint32_t oldFont = ui::g_gdi.SelectObject(hdc, MenuFont());
    for (size_t i = 0; i < m->items.size(); ++i) {
        const MenuItem& it = m->items[i];
        Rect r = it.rect.Offset(kPopupBorder, kPopupBorder);
        if (it.flags & MF_SEPARATOR) {
            int y = r.top + kSepH / 2 - 1;
            line(r.left + 1, y, r.right - 1, y + 1, 16);
            line(r.left + 1, y + 1, r.right - 1, y + 2, 20);
            continue;
        }
        bool sel = static_cast<int>(i) == ps->selected;
        if (sel) ui::g_gdi.FillRect(hdc, r, SysColorBrush(13));
        Rect tr(r.left + kCheckW, r.top, r.right - kArrowW, r.bottom);
        DrawItemText(hdc, it, tr, sel, true, r.right - kArrowW);
        uint32_t markColor = sel ? 14 : 7;
        if (it.flags & MF_CHECKED) {
            int cx = r.left + 8, cy = r.top + kItemH / 2;
            if (it.flags & MF_RADIOCHECK) {
                ui::g_gdi.FillRect(hdc, Rect(cx - 2, cy - 2, cx + 3, cy + 3), SysColorBrush(markColor));
            } else {
                for (int k = 0; k < 7; ++k) {
                    int yy = cy + (k < 3 ? k - 1 : 4 - k) ;
                    line(cx - 3 + k, yy - 1, cx - 2 + k, yy + 2, markColor);
                }
            }
        }
        if (it.flags & MF_POPUP) {
            int cx = r.right - 9, cy = r.top + kItemH / 2;
            for (int k = 0; k < 4; ++k) line(cx + k, cy - 3 + k, cx + k + 1, cy + 4 - k, markColor);
        }
    }
    ui::g_gdi.SelectObject(hdc, oldFont);
    ui::g_gdi.DeleteDc(hdc);
}

uint32_t PopupProc(uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp, bool unicode) {
    switch (msg) {
    case WM_PAINT: {
        PaintPopup(hwnd);
        if (Window* w = Win(hwnd)) { w->update.Clear(); w->erase = false; }
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEACTIVATE:
        return 3; // MA_NOACTIVATE
    }
    return DefWindowProc(hwnd, msg, wp, lp, unicode);
}

uint32_t PopupClassAtom() {
    static bool registered = false;
    if (!registered) {
        registered = true;
        WndClass wc;
        wc.name = u"#32768";
        wc.style = 0x800 | 0x20000; // CS_SAVEBITS | CS_DROPSHADOW
        wc.unicode = true;
        wc.system = true;
        // процедура — внутренняя (адрес-метка, гость её не вызывает)
        wc.wndProc = 0xF00F0000u;
        RegisterHostWndProc(wc.wndProc, &PopupProc, true);
        RegisterClassInternal(wc);
    }
    return 0;
}

void ClosePopupsFrom(size_t level) {
    while (g_popups.size() > level) {
        uint32_t h = g_popups.back().hwnd;
        g_popups.pop_back();
        DestroyWindowInternal(h);
    }
}

void OpenPopup(uint32_t hmenu, int x, int y, int alignRightX = -1) {
    Menu* m = M(hmenu);
    if (!m) return;
    if (g_menuOwner) {
        SendMsg(g_menuOwner, WM_INITMENUPOPUP, hmenu, 0);
        if (!(m = M(hmenu))) return;
    }
    int w, h;
    LayoutPopup(m, w, h);
    State& s = S();
    if (x + w > s.screenW) x = (alignRightX >= 0 ? alignRightX : s.screenW) - w;
    if (y + h > s.screenH) y = std::max(0, s.screenH - h);
    if (x < 0) x = 0;
    PopupClassAtom();
    uint32_t hwnd = CreateWindowInternal(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, u"#32768", 0, u"", WS_POPUP, x, y, w, h, 0, 0, 0, 0, true, 0, 0);
    PopupState ps;
    ps.hmenu = hmenu;
    ps.hwnd = hwnd;
    g_popups.push_back(ps);
    if (Window* pw = Win(hwnd)) {
        pw->style |= WS_VISIBLE;
        SetWindowPosInternal(hwnd, 0xFFFFFFFFu, x, y, w, h, 0x10 | 0x40);
        PaintPopup(hwnd);
        pw->update.Clear();
        pw->ncDirty = false;
    }
}

void RedrawPopup(size_t level) {
    if (level < g_popups.size()) PaintPopup(g_popups[level].hwnd);
}

void RedrawBar() {
    if (Window* w = Win(g_menuOwner)) if (g_menuBar) DrawNonClient(w);
}

// Полоса меню: прямоугольник пункта в координатах экрана
Rect BarItemScreenRect(Window* w, Menu* m, int i) {
    Rect sr = ScreenRect(w);
    int b = BorderWidth(w->style, w->exStyle);
    int top = b + (((w->style & WS_CAPTION) == WS_CAPTION) ? ((w->exStyle & WS_EX_TOOLWINDOW) ? kSmCaptionH : kCaptionH) : 0);
    return m->items[static_cast<size_t>(i)].rect.Offset(sr.left + b, sr.top + top);
}

void SelectBarItem(int i, bool open) {
    Window* w = Win(g_menuOwner);
    Menu* bar = M(g_menuBar);
    if (!w || !bar) return;
    ClosePopupsFrom(0);
    g_barSelected = i;
    RedrawBar();
    if (open && i >= 0 && static_cast<size_t>(i) < bar->items.size()) {
        MenuItem& it = bar->items[static_cast<size_t>(i)];
        if (!(it.flags & MF_POPUP) || (it.flags & (MF_GRAYED | MF_DISABLED))) return;
        Rect r = BarItemScreenRect(w, bar, i);
        OpenPopup(it.subMenu, r.left, r.bottom);
    }
}

void FinishMenu(uint32_t cmd, bool sys) {
    g_menuDone = true;
    g_menuResult = cmd;
    if (cmd && g_menuOwner && !g_returnCmd) PostMsg(g_menuOwner, sys ? WM_SYSCOMMAND : WM_COMMAND, cmd, 0);
}

// Пункт под точкой экрана: уровень каскада и индекс
bool HitPopup(int x, int y, size_t& level, int& index) {
    for (size_t l = g_popups.size(); l-- > 0;) {
        Window* w = Win(g_popups[l].hwnd);
        Menu* m = M(g_popups[l].hmenu);
        if (!w || !m) continue;
        Rect sr = ScreenRect(w);
        if (!sr.Contains(x, y)) continue;
        level = l;
        index = -1;
        for (size_t i = 0; i < m->items.size(); ++i)
            if (m->items[i].rect.Offset(sr.left + kPopupBorder, sr.top + kPopupBorder).Contains(x, y)) index = static_cast<int>(i);
        return true;
    }
    return false;
}

int HitBar(int x, int y) {
    Window* w = Win(g_menuOwner);
    Menu* bar = M(g_menuBar);
    if (!w || !bar) return -1;
    for (size_t i = 0; i < bar->items.size(); ++i)
        if (BarItemScreenRect(w, bar, static_cast<int>(i)).Contains(x, y)) return static_cast<int>(i);
    return -1;
}

void ActivateItem(size_t level, int index) {
    Menu* m = M(g_popups[level].hmenu);
    if (!m || index < 0 || static_cast<size_t>(index) >= m->items.size()) return;
    MenuItem& it = m->items[static_cast<size_t>(index)];
    if (it.flags & (MF_SEPARATOR | MF_GRAYED | MF_DISABLED)) return;
    if (it.flags & MF_POPUP) {
        ClosePopupsFrom(level + 1);
        Window* pw = Win(g_popups[level].hwnd);
        Rect sr = ScreenRect(pw);
        Rect ir = it.rect.Offset(sr.left + kPopupBorder, sr.top + kPopupBorder);
        OpenPopup(it.subMenu, sr.right - 3, ir.top - kPopupBorder, sr.left + 3);
        return;
    }
    FinishMenu(it.id, false);
}

void HoverPopup(size_t level, int index) {
    if (g_popups[level].selected == index) return;
    g_popups[level].selected = index;
    ClosePopupsFrom(level + 1);
    RedrawPopup(level);
    if (Window* w = Win(g_menuOwner)) {
        Menu* m = M(g_popups[level].hmenu);
        uint32_t id = (index >= 0 && m) ? m->items[static_cast<size_t>(index)].id : 0;
        SendMsg(w->hwnd, WM_MENUSELECT, (id & 0xFFFF), g_popups[level].hmenu);
    }
}

// Модальный цикл меню. Возвращает выбранную команду (0 — отмена).
uint32_t MenuLoop() {
    g_menuDone = false;
    g_menuResult = 0;
    State& s = S();
    if (g_menuOwner) SendMsg(g_menuOwner, WM_ENTERMENULOOP, g_menuBar ? 0 : 1, 0);
    uint32_t savedCapture = s.capture;
    bool buttonWasDown = (s.asyncKeys[1] & 0x80) != 0;
    while (!g_menuDone && !g_guestExitRequested) {
        Msg msg;
        if (!FetchMessage(msg, 0, 0, 0, true, true)) continue;
        if (msg.message == WM_QUIT) { PostQuit(msg.wParam); break; }
        bool mouse = (msg.message >= WM_MOUSEMOVE && msg.message <= WM_MBUTTONDBLCLK) ||
                     (msg.message >= WM_NCMOUSEMOVE && msg.message <= 0x00A9);
        if (mouse) {
            int x = s.cursorX, y = s.cursorY;
            bool down = msg.message == WM_LBUTTONDOWN || msg.message == WM_NCLBUTTONDOWN || msg.message == WM_RBUTTONDOWN ||
                        msg.message == 0x00A4 || msg.message == WM_LBUTTONDBLCLK;
            bool up = msg.message == WM_LBUTTONUP || msg.message == WM_NCLBUTTONUP || msg.message == WM_RBUTTONUP;
            size_t level;
            int index;
            if (HitPopup(x, y, level, index)) {
                HoverPopup(level, index);
                if (up || (down && !buttonWasDown)) {
                    if (up && buttonWasDown && index >= 0 && (M(g_popups[level].hmenu)->items[static_cast<size_t>(index)].flags & MF_POPUP)) {
                        // отпускание над пунктом-подменю: просто держим открытым
                    } else if (up || down) {
                        ActivateItem(level, index);
                    }
                }
            } else if (g_menuBar && HitBar(x, y) >= 0) {
                int i = HitBar(x, y);
                if (down && i == g_barSelected && !g_popups.empty()) { FinishMenu(0, false); }
                else if (i != g_barSelected || down) SelectBarItem(i, true);
            } else if (down) {
                FinishMenu(0, false); // щелчок мимо меню
            }
            if (down) buttonWasDown = true;
            if (up) buttonWasDown = false;
            continue;
        }
        if (msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN) {
            size_t level = g_popups.empty() ? 0 : g_popups.size() - 1;
            Menu* m = g_popups.empty() ? nullptr : M(g_popups[level].hmenu);
            auto step = [&](int dir) {
                if (!m || m->items.empty()) return;
                int n = static_cast<int>(m->items.size());
                int i = g_popups[level].selected;
                for (int k = 0; k < n; ++k) {
                    i = (i + dir + n) % n;
                    if (!(m->items[static_cast<size_t>(i)].flags & MF_SEPARATOR)) break;
                }
                HoverPopup(level, i);
            };
            switch (msg.wParam) {
            case 0x1B: // Esc
                if (g_popups.size() > 1) ClosePopupsFrom(g_popups.size() - 1);
                else FinishMenu(0, false);
                break;
            case 0x26: step(-1); break;
            case 0x28: step(1); break;
            case 0x0D: if (m) ActivateItem(level, g_popups[level].selected); break;
            case 0x25: case 0x27: // влево/вправо
                if (msg.wParam == 0x27 && m && g_popups[level].selected >= 0 &&
                    (m->items[static_cast<size_t>(g_popups[level].selected)].flags & MF_POPUP)) {
                    ActivateItem(level, g_popups[level].selected);
                } else if (msg.wParam == 0x25 && g_popups.size() > 1) {
                    ClosePopupsFrom(g_popups.size() - 1);
                } else if (g_menuBar) {
                    Menu* bar = M(g_menuBar);
                    int n = static_cast<int>(bar->items.size());
                    if (n) SelectBarItem((g_barSelected + (msg.wParam == 0x27 ? 1 : -1) + n) % n, true);
                }
                break;
            case 0x12: case 0x79: // Alt, F10
                FinishMenu(0, false);
                break;
            }
            continue;
        }
        if (msg.message == WM_CHAR || msg.message == WM_SYSCHAR || msg.message == WM_KEYUP || msg.message == WM_SYSKEYUP) {
            if ((msg.message == WM_CHAR || msg.message == WM_SYSCHAR) && !g_popups.empty()) {
                // мнемоника: &X
                size_t level = g_popups.size() - 1;
                Menu* m = M(g_popups[level].hmenu);
                char16_t want = static_cast<char16_t>(msg.wParam);
                if (want >= 'a' && want <= 'z') want = static_cast<char16_t>(want - 32);
                for (size_t i = 0; m && i < m->items.size(); ++i) {
                    const std::u16string& t = m->items[i].text;
                    size_t amp = t.find(u'&');
                    if (amp == std::u16string::npos || amp + 1 >= t.size()) continue;
                    char16_t ch = t[amp + 1];
                    if (ch >= 'a' && ch <= 'z') ch = static_cast<char16_t>(ch - 32);
                    if (ch == want) { HoverPopup(level, static_cast<int>(i)); ActivateItem(level, static_cast<int>(i)); break; }
                }
            }
            continue;
        }
        // остальное (перерисовка, таймеры) — как обычно
        if (msg.message == WM_PAINT || msg.message == WM_TIMER || msg.hwnd) {
            if (msg.message == WM_TIMER && msg.lParam) CallStdcall(*G().c, *G().m, msg.lParam, {msg.hwnd, WM_TIMER, msg.wParam, NowMs()});
            else SendMsg(msg.hwnd, msg.message, msg.wParam, msg.lParam);
        }
    }
    ClosePopupsFrom(0);
    g_barSelected = -1;
    RedrawBar();
    s.capture = savedCapture;
    if (g_menuOwner) SendMsg(g_menuOwner, WM_EXITMENULOOP, g_menuBar ? 0 : 1, 0);
    return g_menuResult;
}

// --- Ускорители ---

struct AccelEntry { uint8_t fVirt; uint16_t key, cmd; };
std::map<uint32_t, std::vector<AccelEntry>>& Accels() {
    static std::map<uint32_t, std::vector<AccelEntry>> a;
    return a;
}
uint32_t g_nextAccel = 0x00070010;

} // namespace

void DrawMenuBarInto(Window* w, uint32_t hdc, const Rect& bar) {
    Menu* m = M(w->menu);
    if (!m) return;
    LayoutBar(m, bar.Width());
    uint32_t oldFont = ui::g_gdi.SelectObject(hdc, MenuFont());
    for (size_t i = 0; i < m->items.size(); ++i) {
        const MenuItem& it = m->items[i];
        Rect r = it.rect.Offset(bar.left, bar.top);
        bool sel = g_menuBar == w->menu && g_menuOwner == w->hwnd && static_cast<int>(i) == g_barSelected;
        if (sel) {
            // классическая тема: нажатый пункт — утопленная рамка
            ui::g_gdi.FillRect(hdc, Rect(r.left, r.top, r.right - 1, r.top + 1), SysColorBrush(16));
            ui::g_gdi.FillRect(hdc, Rect(r.left, r.top, r.left + 1, r.bottom - 1), SysColorBrush(16));
            ui::g_gdi.FillRect(hdc, Rect(r.left, r.bottom - 1, r.right, r.bottom), SysColorBrush(20));
            ui::g_gdi.FillRect(hdc, Rect(r.right - 1, r.top, r.right, r.bottom), SysColorBrush(20));
        }
        Rect tr = r.Offset(sel ? 1 : 0, sel ? 1 : 0);
        tr.left += 8;
        DrawItemText(hdc, it, tr, false, false, 0);
    }
    ui::g_gdi.SelectObject(hdc, oldFont);
}

uint32_t LoadMenuFromMemory(const uint8_t* p, size_t size) { return p ? LoadMenuTemplate(p, size) : 0; }

// Вызывается из DefWindowProc: щелчок по полосе меню / Alt / F10
void StartMenuBar(uint32_t hwnd, int screenX, int screenY, bool keyboard) {
    Window* w = Win(hwnd);
    if (!w || !M(w->menu) || !g_popups.empty()) return;
    g_menuOwner = hwnd;
    g_menuBar = w->menu;
    g_returnCmd = false;
    SendMsg(hwnd, WM_INITMENU, w->menu, 0);
    int i = keyboard ? 0 : HitBar(screenX, screenY);
    if (i < 0) { g_menuBar = 0; g_menuOwner = 0; return; }
    SelectBarItem(i, true);
    MenuLoop();
    g_menuBar = 0;
    g_menuOwner = 0;
}

namespace {

#define FN(name) HostCallOutcome name(cpu::CpuState& c, cpu::IGuestMemory& m)
#define ENTER (G().c = &c, G().m = &m)
#define AW(name)                                       \
    FN(name##A) { ENTER; return name##Impl(c, m, false); } \
    FN(name##W) { ENTER; return name##Impl(c, m, true); }
inline uint32_t A(cpu::CpuState& c, cpu::IGuestMemory& m, int i) { return Arg(c, m, i); }

rt::Module* ModuleOf(uint32_t hInstance) {
    if (!rt::g_process) return nullptr;
    if (!hInstance) return rt::g_process->MainModule();
    return rt::g_process->FindByHandle(hInstance);
}
rt::ResId ResName(uint32_t p, bool unicode) {
    if (p < 0x10000) return rt::ResId::Num(static_cast<uint16_t>(p));
    std::u16string s = ReadText(p, unicode);
    if (!s.empty() && s[0] == '#') {
        uint32_t v = 0;
        for (size_t i = 1; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i) v = v * 10 + (s[i] - '0');
        return rt::ResId::Num(static_cast<uint16_t>(v));
    }
    for (char16_t& ch : s) if (ch >= 'a' && ch <= 'z') ch = static_cast<char16_t>(ch - 32);
    return rt::ResId::Str(s);
}

FN(CreateMenu) { ENTER; return Ret(NewMenu(false)); }
FN(CreatePopupMenu) { ENTER; return Ret(NewMenu(true)); }
FN(DestroyMenu) { ENTER; bool ok = M(A(c, m, 0)) != nullptr; DestroyMenuTree(A(c, m, 0)); return Ret(ok ? 1 : 0); }
FN(IsMenu) { ENTER; return Ret(M(A(c, m, 0)) ? 1 : 0); }

HostCallOutcome LoadMenuImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool unicode) {
    rt::Module* mod = ModuleOf(A(c, m, 0));
    rt::ResourceData d;
    if (!rt::FindResource(mod, rt::ResId::Num(rt::RT_MENU), ResName(A(c, m, 1), unicode), d)) { SetLastError(m, 1814); return Ret(0); }
    return Ret(LoadMenuTemplate(rt::ResourcePtr(mod, d), d.size));
}
AW(LoadMenu)
HostCallOutcome LoadMenuIndirectImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool) {
    uint32_t p = A(c, m, 0);
    std::vector<uint8_t> buf(65536);
    m.ReadBytes(p, buf.data(), buf.size());
    return Ret(LoadMenuTemplate(buf.data(), buf.size()));
}
AW(LoadMenuIndirect)

FN(GetMenu) { ENTER; Window* w = Win(A(c, m, 0)); return Ret(w && !(w->style & WS_CHILD) ? w->menu : 0); }
FN(SetMenu) {
    ENTER;
    uint32_t hwnd = A(c, m, 0);
    Window* w = Win(hwnd);
    if (!w || (w->style & WS_CHILD)) return Ret(0);
    w->menu = A(c, m, 1);
    if (Menu* mm = M(w->menu)) mm->owner = hwnd;
    // клиентская область меняется
    SetWindowPosInternal(hwnd, 0, 0, 0, 0, 0, 0x1 | 0x2 | 0x4 | 0x10 | 0x20);
    return Ret(1);
}
FN(DrawMenuBar) { ENTER; Window* w = Win(A(c, m, 0)); if (w) { w->ncDirty = true; if (IsVisibleChain(w)) DrawNonClient(w); } return Ret(w ? 1 : 0); }
FN(GetSubMenu) {
    ENTER;
    MenuItem* it = FindItem(A(c, m, 0), A(c, m, 1), true);
    return Ret(it && (it->flags & MF_POPUP) ? it->subMenu : 0);
}
FN(GetMenuItemCount) { ENTER; Menu* mm = M(A(c, m, 0)); return Ret(mm ? static_cast<uint32_t>(mm->items.size()) : 0xFFFFFFFFu); }
FN(GetMenuItemID) {
    ENTER;
    MenuItem* it = FindItem(A(c, m, 0), A(c, m, 1), true);
    if (!it) return Ret(0xFFFFFFFFu);
    return Ret((it->flags & MF_POPUP) ? 0xFFFFFFFFu : it->id);
}
FN(GetMenuState) {
    ENTER;
    MenuItem* it = FindItem(A(c, m, 0), A(c, m, 1), (A(c, m, 2) & MF_BYPOSITION) != 0);
    if (!it) return Ret(0xFFFFFFFFu);
    uint32_t st = it->flags & ~MF_POPUP;
    if (it->flags & MF_POPUP) { Menu* sub = M(it->subMenu); st |= MF_POPUP | ((sub ? static_cast<uint32_t>(sub->items.size()) : 0) << 8); }
    return Ret(st);
}
HostCallOutcome GetMenuStringImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool unicode) {
    MenuItem* it = FindItem(A(c, m, 0), A(c, m, 1), (A(c, m, 4) & MF_BYPOSITION) != 0);
    if (!it) return Ret(0);
    if (!A(c, m, 2)) return Ret(static_cast<uint32_t>(it->text.size()));
    return Ret(WriteText(A(c, m, 2), it->text, A(c, m, 3), unicode));
}
AW(GetMenuString)
FN(CheckMenuItem) {
    ENTER;
    MenuItem* it = FindItem(A(c, m, 0), A(c, m, 1), (A(c, m, 2) & MF_BYPOSITION) != 0);
    if (!it) return Ret(0xFFFFFFFFu);
    uint32_t old = it->flags & MF_CHECKED;
    if (A(c, m, 2) & MF_CHECKED) it->flags |= MF_CHECKED; else it->flags &= ~MF_CHECKED;
    return Ret(old);
}
FN(CheckMenuRadioItem) {
    ENTER;
    uint32_t hm = A(c, m, 0), first = A(c, m, 1), last = A(c, m, 2), check = A(c, m, 3);
    bool byPos = (A(c, m, 4) & MF_BYPOSITION) != 0;
    Menu* mm = M(hm);
    if (!mm) return Ret(0);
    for (size_t i = 0; i < mm->items.size(); ++i) {
        MenuItem& it = mm->items[i];
        uint32_t key = byPos ? static_cast<uint32_t>(i) : it.id;
        if (key < first || key > last) continue;
        if (key == check) it.flags |= MF_CHECKED | MF_RADIOCHECK;
        else it.flags &= ~MF_CHECKED;
    }
    return Ret(1);
}
FN(EnableMenuItem) {
    ENTER;
    MenuItem* it = FindItem(A(c, m, 0), A(c, m, 1), (A(c, m, 2) & MF_BYPOSITION) != 0);
    if (!it) return Ret(0xFFFFFFFFu);
    uint32_t old = it->flags & (MF_GRAYED | MF_DISABLED);
    it->flags = (it->flags & ~(MF_GRAYED | MF_DISABLED)) | (A(c, m, 2) & (MF_GRAYED | MF_DISABLED));
    return Ret(old);
}
FN(HiliteMenuItem) { ENTER; return Ret(1); }

// Добавление/изменение пунктов
void FillItem(cpu::IGuestMemory& m, MenuItem& it, uint32_t flags, uint32_t idOrSub, uint32_t content, bool unicode) {
    it.flags = flags & ~(MF_BYPOSITION | 0x80u);
    if (flags & MF_POPUP) { it.subMenu = idOrSub; it.id = idOrSub; }
    else it.id = idOrSub & 0xFFFF;
    it.text.clear();
    if (flags & MF_BITMAP) it.bitmap = content;
    else if (flags & MF_OWNERDRAW) it.data = content;
    else if (!(flags & MF_SEPARATOR) && content) it.text = ReadText(content, unicode);
    (void)m;
}
HostCallOutcome AppendMenuImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool unicode) {
    Menu* mm = M(A(c, m, 0));
    if (!mm) return Ret(0);
    MenuItem it;
    FillItem(m, it, A(c, m, 1), A(c, m, 2), A(c, m, 3), unicode);
    mm->items.push_back(it);
    return Ret(1);
}
AW(AppendMenu)
HostCallOutcome InsertMenuImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool unicode) {
    uint32_t hm = A(c, m, 0), pos = A(c, m, 1), flags = A(c, m, 2);
    Menu* mm = M(hm);
    if (!mm) return Ret(0);
    MenuItem it;
    FillItem(m, it, flags, A(c, m, 3), A(c, m, 4), unicode);
    size_t at = mm->items.size();
    if (flags & MF_BYPOSITION) { if (pos < at) at = pos; }
    else for (size_t i = 0; i < mm->items.size(); ++i) if (mm->items[i].id == pos) { at = i; break; }
    mm->items.insert(mm->items.begin() + static_cast<long>(at), it);
    return Ret(1);
}
AW(InsertMenu)
HostCallOutcome ModifyMenuImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool unicode) {
    MenuItem* it = FindItem(A(c, m, 0), A(c, m, 1), (A(c, m, 2) & MF_BYPOSITION) != 0);
    if (!it) return Ret(0);
    FillItem(m, *it, A(c, m, 2), A(c, m, 3), A(c, m, 4), unicode);
    return Ret(1);
}
AW(ModifyMenu)
HostCallOutcome RemoveCommon(cpu::CpuState& c, cpu::IGuestMemory& m, bool destroy) {
    uint32_t owner = 0;
    MenuItem* it = FindItem(A(c, m, 0), A(c, m, 1), (A(c, m, 2) & MF_BYPOSITION) != 0, &owner);
    Menu* mm = M(owner);
    if (!it || !mm) return Ret(0);
    uint32_t sub = it->subMenu;
    mm->items.erase(mm->items.begin() + (it - mm->items.data()));
    if (destroy && sub) DestroyMenuTree(sub);
    return Ret(1);
}
FN(RemoveMenu) { ENTER; return RemoveCommon(c, m, false); }
FN(DeleteMenu) { ENTER; return RemoveCommon(c, m, true); }

// MENUITEMINFO: cbSize 0, fMask 4, fType 8, fState 12, wID 16, hSubMenu 20,
// hbmpChecked 24, hbmpUnchecked 28, dwItemData 32, dwTypeData 36, cch 40, hbmpItem 44
void ApplyItemInfo(cpu::IGuestMemory& m, uint32_t p, MenuItem& it, bool unicode) {
    uint32_t mask = m.Read32(p + 4);
    if (mask & 0x10) { // MIIM_TYPE
        uint32_t type = m.Read32(p + 8);
        it.flags = (it.flags & ~(MF_BITMAP | MF_OWNERDRAW | MF_SEPARATOR | MF_RADIOCHECK | MF_MENUBREAK | MF_MENUBARBREAK | MF_RIGHTJUSTIFY)) | type;
        if (!(type & (MF_BITMAP | MF_OWNERDRAW | MF_SEPARATOR))) it.text = ReadText(m.Read32(p + 36), unicode);
    }
    if (mask & 0x100) { uint32_t type = m.Read32(p + 8); it.flags = (it.flags & ~(MF_SEPARATOR | MF_RADIOCHECK | MF_OWNERDRAW | MF_RIGHTJUSTIFY)) | (type & ~MF_BITMAP); } // MIIM_FTYPE
    if (mask & 0x40) it.text = ReadText(m.Read32(p + 36), unicode); // MIIM_STRING
    if (mask & 0x1) { uint32_t st = m.Read32(p + 12); it.flags = (it.flags & ~(MF_GRAYED | MF_DISABLED | MF_CHECKED | MF_HILITE | MF_DEFAULT)) | (st & (MF_GRAYED | MF_DISABLED | MF_CHECKED | MF_HILITE | MF_DEFAULT)); }
    if (mask & 0x2) it.id = m.Read32(p + 16);
    if (mask & 0x4) { it.subMenu = m.Read32(p + 20); if (it.subMenu) it.flags |= MF_POPUP; else it.flags &= ~MF_POPUP; }
    if (mask & 0x20) it.data = m.Read32(p + 32);
    if (mask & 0x80) it.bitmap = m.Read32(p + 44);
}
HostCallOutcome InsertMenuItemImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool unicode) {
    Menu* mm = M(A(c, m, 0));
    if (!mm) return Ret(0);
    MenuItem it;
    ApplyItemInfo(m, A(c, m, 3), it, unicode);
    size_t at = mm->items.size();
    uint32_t pos = A(c, m, 1);
    if (A(c, m, 2)) { if (pos < at) at = pos; }
    else for (size_t i = 0; i < mm->items.size(); ++i) if (mm->items[i].id == pos) { at = i; break; }
    mm->items.insert(mm->items.begin() + static_cast<long>(at), it);
    return Ret(1);
}
AW(InsertMenuItem)
HostCallOutcome SetMenuItemInfoImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool unicode) {
    MenuItem* it = FindItem(A(c, m, 0), A(c, m, 1), A(c, m, 2) != 0);
    if (!it) return Ret(0);
    ApplyItemInfo(m, A(c, m, 3), *it, unicode);
    return Ret(1);
}
AW(SetMenuItemInfo)
HostCallOutcome GetMenuItemInfoImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool unicode) {
    MenuItem* it = FindItem(A(c, m, 0), A(c, m, 1), A(c, m, 2) != 0);
    uint32_t p = A(c, m, 3);
    if (!it || !p) return Ret(0);
    uint32_t mask = m.Read32(p + 4);
    if (mask & (0x10 | 0x100)) m.Write32(p + 8, it->flags & (MF_BITMAP | MF_OWNERDRAW | MF_SEPARATOR | MF_RADIOCHECK | MF_MENUBREAK | MF_MENUBARBREAK | MF_RIGHTJUSTIFY));
    if (mask & 0x1) m.Write32(p + 12, it->flags & (MF_GRAYED | MF_DISABLED | MF_CHECKED | MF_HILITE | MF_DEFAULT));
    if (mask & 0x2) m.Write32(p + 16, it->id);
    if (mask & 0x4) m.Write32(p + 20, (it->flags & MF_POPUP) ? it->subMenu : 0);
    if (mask & 0x20) m.Write32(p + 32, it->data);
    if (mask & 0x80) m.Write32(p + 44, it->bitmap);
    if (mask & (0x10 | 0x40)) {
        uint32_t buf = m.Read32(p + 36), cch = m.Read32(p + 40);
        if (buf && cch) m.Write32(p + 40, WriteText(buf, it->text, cch, unicode));
        else m.Write32(p + 40, static_cast<uint32_t>(it->text.size()));
    }
    return Ret(1);
}
AW(GetMenuItemInfo)
FN(SetMenuDefaultItem) {
    ENTER;
    Menu* mm = M(A(c, m, 0));
    if (!mm) return Ret(0);
    for (size_t i = 0; i < mm->items.size(); ++i) {
        MenuItem& it = mm->items[i];
        bool match = A(c, m, 2) ? i == A(c, m, 1) : it.id == A(c, m, 1);
        if (match) it.flags |= MF_DEFAULT; else it.flags &= ~MF_DEFAULT;
    }
    return Ret(1);
}
FN(GetMenuDefaultItem) {
    ENTER;
    Menu* mm = M(A(c, m, 0));
    if (!mm) return Ret(0xFFFFFFFFu);
    for (size_t i = 0; i < mm->items.size(); ++i)
        if (mm->items[i].flags & MF_DEFAULT) return Ret(A(c, m, 1) ? static_cast<uint32_t>(i) : mm->items[i].id);
    return Ret(0xFFFFFFFFu);
}
FN(SetMenuItemBitmaps) { ENTER; return Ret(1); }
FN(GetMenuCheckMarkDimensions) { ENTER; return Ret(13 | (13 << 16)); }
FN(GetMenuItemRect) {
    ENTER;
    Window* w = Win(A(c, m, 0));
    Menu* mm = M(A(c, m, 1));
    uint32_t i = A(c, m, 2), out = A(c, m, 3);
    if (!mm || i >= mm->items.size() || !out) return Ret(0);
    Rect r;
    if (w && !mm->popup) {
        int b = BorderWidth(w->style, w->exStyle);
        LayoutBar(mm, w->rect.Width() - 2 * b);
        r = BarItemScreenRect(w, mm, static_cast<int>(i));
    } else {
        for (auto& p : g_popups) if (p.hmenu == A(c, m, 1)) if (Window* pw = Win(p.hwnd)) r = mm->items[i].rect.Offset(ScreenRect(pw).left + kPopupBorder, ScreenRect(pw).top + kPopupBorder);
    }
    m.Write32(out, static_cast<uint32_t>(r.left));
    m.Write32(out + 4, static_cast<uint32_t>(r.top));
    m.Write32(out + 8, static_cast<uint32_t>(r.right));
    m.Write32(out + 12, static_cast<uint32_t>(r.bottom));
    return Ret(1);
}
FN(GetMenuInfo) { ENTER; return Ret(1); }
FN(SetMenuInfo) { ENTER; return Ret(1); }
FN(GetSystemMenu) {
    ENTER;
    static std::map<uint32_t, uint32_t> sys;
    uint32_t hwnd = A(c, m, 0);
    if (A(c, m, 1)) { sys.erase(hwnd); return Ret(0); }
    auto it = sys.find(hwnd);
    if (it != sys.end() && M(it->second)) return Ret(it->second);
    uint32_t h = NewMenu(true);
    Menu* mm = M(h);
    const struct { uint32_t id; const char16_t* t; } items[] = {
        {0xF120, u"&Restore"}, {0xF010, u"&Move"}, {0xF000, u"&Size"}, {0xF020, u"Mi&nimize"}, {0xF030, u"Ma&ximize"},
        {0, nullptr}, {0xF060, u"&Close\tAlt+F4"}};
    for (auto& x : items) {
        MenuItem it2;
        if (!x.t) it2.flags = MF_SEPARATOR;
        else { it2.id = x.id; it2.text = x.t; }
        mm->items.push_back(it2);
    }
    sys[hwnd] = h;
    return Ret(h);
}
FN(TrackPopupMenu) {
    ENTER;
    uint32_t hm = A(c, m, 0), flags = A(c, m, 1), hwnd = A(c, m, 6);
    int x = static_cast<int32_t>(A(c, m, 2)), y = static_cast<int32_t>(A(c, m, 3));
    if (!M(hm) || !g_popups.empty()) return Ret(0);
    g_menuOwner = hwnd;
    g_menuBar = 0;
    g_returnCmd = (flags & 0x100) != 0;
    SendMsg(hwnd, WM_INITMENU, hm, 0);
    int w, h;
    LayoutPopup(M(hm), w, h);
    if (flags & 0x8) x -= w;            // TPM_RIGHTALIGN
    else if (flags & 0x4) x -= w / 2;   // TPM_CENTERALIGN
    if (flags & 0x20) y -= h;           // TPM_BOTTOMALIGN
    else if (flags & 0x10) y -= h / 2;  // TPM_VCENTERALIGN
    OpenPopup(hm, x, y);
    uint32_t r = MenuLoop();
    g_menuOwner = 0;
    return Ret(g_returnCmd ? r : (r ? 1 : 0));
}
FN(TrackPopupMenuEx) {
    ENTER;
    uint32_t hm = A(c, m, 0), flags = A(c, m, 1), hwnd = A(c, m, 4);
    int x = static_cast<int32_t>(A(c, m, 2)), y = static_cast<int32_t>(A(c, m, 3));
    if (!M(hm) || !g_popups.empty()) return Ret(0);
    g_menuOwner = hwnd;
    g_menuBar = 0;
    g_returnCmd = (flags & 0x100) != 0;
    SendMsg(hwnd, WM_INITMENU, hm, 0);
    int w, h;
    LayoutPopup(M(hm), w, h);
    if (flags & 0x8) x -= w;
    if (flags & 0x20) y -= h;
    OpenPopup(hm, x, y);
    uint32_t r = MenuLoop();
    g_menuOwner = 0;
    return Ret(g_returnCmd ? r : (r ? 1 : 0));
}
FN(EndMenu) { ENTER; if (!g_popups.empty()) FinishMenu(0, false); return Ret(1); }
FN(MenuItemFromPoint) {
    ENTER;
    Window* w = Win(A(c, m, 0));
    Menu* mm = M(A(c, m, 1));
    int x = static_cast<int32_t>(A(c, m, 2)), y = static_cast<int32_t>(A(c, m, 3));
    if (!w || !mm) return Ret(0xFFFFFFFFu);
    for (size_t i = 0; i < mm->items.size(); ++i)
        if (BarItemScreenRect(w, mm, static_cast<int>(i)).Contains(x, y)) return Ret(static_cast<uint32_t>(i));
    return Ret(0xFFFFFFFFu);
}

// --- Ускорители ---

HostCallOutcome LoadAcceleratorsImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool unicode) {
    rt::Module* mod = ModuleOf(A(c, m, 0));
    rt::ResourceData d;
    if (!rt::FindResource(mod, rt::ResId::Num(rt::RT_ACCELERATOR), ResName(A(c, m, 1), unicode), d)) return Ret(0);
    const uint8_t* p = rt::ResourcePtr(mod, d);
    std::vector<AccelEntry> v;
    for (uint32_t off = 0; off + 8 <= d.size; off += 8) {
        AccelEntry e;
        e.fVirt = p[off];
        e.key = static_cast<uint16_t>(p[off + 2] | (p[off + 3] << 8));
        e.cmd = static_cast<uint16_t>(p[off + 4] | (p[off + 5] << 8));
        v.push_back(e);
        if (p[off] & 0x80) break;
    }
    uint32_t h = g_nextAccel;
    g_nextAccel += 4;
    Accels()[h] = v;
    return Ret(h);
}
AW(LoadAccelerators)
HostCallOutcome CreateAcceleratorTableImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool) {
    uint32_t p = A(c, m, 0), n = A(c, m, 1);
    std::vector<AccelEntry> v;
    for (uint32_t i = 0; i < n && i < 1000; ++i)
        v.push_back(AccelEntry{m.Read8(p + i * 6), m.Read16(p + i * 6 + 2), m.Read16(p + i * 6 + 4)});
    uint32_t h = g_nextAccel;
    g_nextAccel += 4;
    Accels()[h] = v;
    return Ret(h);
}
AW(CreateAcceleratorTable)
FN(DestroyAcceleratorTable) { ENTER; return Ret(Accels().erase(A(c, m, 0)) ? 1 : 0); }
HostCallOutcome CopyAcceleratorTableImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool) {
    auto it = Accels().find(A(c, m, 0));
    if (it == Accels().end()) return Ret(0);
    uint32_t out = A(c, m, 1), n = A(c, m, 2);
    if (!out) return Ret(static_cast<uint32_t>(it->second.size()));
    uint32_t k = 0;
    for (; k < n && k < it->second.size(); ++k) {
        m.Write8(out + k * 6, it->second[k].fVirt & 0x7F);
        m.Write8(out + k * 6 + 1, 0);
        m.Write16(out + k * 6 + 2, it->second[k].key);
        m.Write16(out + k * 6 + 4, it->second[k].cmd);
    }
    return Ret(k);
}
AW(CopyAcceleratorTable)
HostCallOutcome TranslateAcceleratorImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool) {
    uint32_t hwnd = A(c, m, 0), msgP = A(c, m, 2);
    auto it = Accels().find(A(c, m, 1));
    if (it == Accels().end() || !msgP) return Ret(0);
    uint32_t message = m.Read32(msgP + 4), wp = m.Read32(msgP + 8);
    bool keyMsg = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
    bool charMsg = message == WM_CHAR || message == WM_SYSCHAR;
    if (!keyMsg && !charMsg) return Ret(0);
    const uint8_t* k = Q().keys;
    bool shift = (k[0x10] & 0x80) != 0, ctrl = (k[0x11] & 0x80) != 0, alt = (k[0x12] & 0x80) != 0;
    for (const AccelEntry& e : it->second) {
        bool virt = (e.fVirt & 0x01) != 0;
        if (virt != keyMsg) continue;
        if (virt) {
            if (e.key != wp) continue;
            if (((e.fVirt & 0x04) != 0) != shift || ((e.fVirt & 0x08) != 0) != ctrl || ((e.fVirt & 0x10) != 0) != alt) continue;
        } else {
            if (e.key != wp) continue;
            if ((e.fVirt & 0x10) && !alt) continue;
        }
        // пункт меню отключён — команда не посылается
        if (Window* w = Win(hwnd)) if (MenuItem* mi = FindItem(w->menu, e.cmd, false)) if (mi->flags & (MF_GRAYED | MF_DISABLED)) return Ret(1);
        SendMsg(hwnd, WM_COMMAND, 0x10000u | e.cmd, 0);
        return Ret(1);
    }
    return Ret(0);
}
AW(TranslateAccelerator)

#define E(name, bytes) {"USER32.dll", #name, bytes, CallConv::Stdcall, &name}
#define EAW(name, bytes) E(name##A, bytes), E(name##W, bytes)
const HostFunctionEntry kEntries[] = {
    E(CreateMenu, 0), E(CreatePopupMenu, 0), E(DestroyMenu, 4), E(IsMenu, 4), EAW(LoadMenu, 8), EAW(LoadMenuIndirect, 4),
    E(GetMenu, 4), E(SetMenu, 8), E(DrawMenuBar, 4), E(GetSubMenu, 8), E(GetMenuItemCount, 4), E(GetMenuItemID, 8),
    E(GetMenuState, 12), EAW(GetMenuString, 20), E(CheckMenuItem, 12), E(CheckMenuRadioItem, 20), E(EnableMenuItem, 12),
    E(HiliteMenuItem, 16), EAW(AppendMenu, 16), EAW(InsertMenu, 20), EAW(ModifyMenu, 20), E(RemoveMenu, 12), E(DeleteMenu, 12),
    EAW(InsertMenuItem, 16), EAW(SetMenuItemInfo, 16), EAW(GetMenuItemInfo, 16), E(SetMenuDefaultItem, 12),
    E(GetMenuDefaultItem, 12), E(SetMenuItemBitmaps, 20), E(GetMenuCheckMarkDimensions, 0), E(GetMenuItemRect, 16),
    E(GetMenuInfo, 8), E(SetMenuInfo, 8), E(GetSystemMenu, 8), E(TrackPopupMenu, 28), E(TrackPopupMenuEx, 24), E(EndMenu, 0),
    E(MenuItemFromPoint, 16),
    EAW(LoadAccelerators, 8), EAW(CreateAcceleratorTable, 8), E(DestroyAcceleratorTable, 4), EAW(CopyAcceleratorTable, 12),
    EAW(TranslateAccelerator, 12),
};
const bool kRegistered = RegisterHostFunctionsLayer(kEntries, sizeof(kEntries) / sizeof(kEntries[0]), kUiLayer);

} // namespace

} // namespace user
} // namespace win32
