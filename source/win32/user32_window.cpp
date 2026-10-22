// user32_window.cpp — окна USER32: классы, создание и уничтожение,
// положение и порядок, координаты, видимость, области обновления, рамка
// окна и DefWindowProc (см. user32.h).

#include <algorithm>
#include <cstring>

#include "../runtime/clock.h"
#include "../runtime/guest_execution.h"
#include "user32.h"

namespace win32 {
namespace user {

using namespace hle;

Platform g_platform;

State& S() {
    static State s;
    return s;
}

GuestCtx& G() {
    static GuestCtx g;
    return g;
}

uint32_t RegisterMessage(std::u16string name) {
    static std::map<std::u16string,uint32_t> messages;
    for(char16_t& c:name)if(c>=u'a' && c<=u'z')c=static_cast<char16_t>(c-32);
    auto it=messages.find(name);if(it!=messages.end())return it->second;
    const uint32_t id=0xC100+static_cast<uint32_t>(messages.size());
    if(id>0xFFFF)return 0;
    messages.emplace(std::move(name),id);return id;
}

uint32_t NowMs() { return rt::Clock::NowMs(); }

namespace {

std::map<uint32_t, std::pair<HostWndProc, bool>>& HostProcs() {
    static std::map<uint32_t, std::pair<HostWndProc, bool>> m;
    return m;
}

bool EqualNoCase(const std::u16string& a, const std::u16string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        char16_t x = a[i], y = b[i];
        if (x >= 'a' && x <= 'z') x = static_cast<char16_t>(x - 32);
        if (y >= 'a' && y <= 'z') y = static_cast<char16_t>(y - 32);
        if (x != y) return false;
    }
    return true;
}

// Видимая часть окна для контекстов устройства (см. ui::WindowDcProvider)
bool DcProvider(uint32_t hwnd, bool wholeWindow, ui::WindowDcInfo& out) {
    Window* w = Win(hwnd);
    if (!w) return false;
    Window* desk = Desktop();
    Rect base = w == desk ? Rect(0, 0, S().screenW, S().screenH) : (wholeWindow ? ScreenRect(w) : ScreenClientRect(w));
    out.originX = base.left;
    out.originY = base.top;
    if (!IsVisibleChain(w)) { out.visible = Region(); return true; }
    Region vis(base);
    for (Window* cur = w; cur && cur != desk;) {
        Window* p = ParentWin(cur);
        if (!p) break;
        if (p != desk) vis.IntersectRect(ScreenClientRect(p));
        for (uint32_t sib : p->children) {
            if (sib == cur->hwnd) break;
            Window* sw = Win(sib);
            if (sw && (sw->style & WS_VISIBLE)) vis.SubtractRect(ScreenRect(sw));
        }
        cur = p;
    }
    if (w == desk) {
        for (uint32_t c : desk->children) {
            Window* cw = Win(c);
            if (cw && (cw->style & WS_VISIBLE)) vis.SubtractRect(ScreenRect(cw));
        }
    } else if (w->style & WS_CLIPCHILDREN) {
        for (uint32_t c : w->children) {
            Window* cw = Win(c);
            if (cw && (cw->style & WS_VISIBLE)) vis.SubtractRect(ScreenRect(cw));
        }
    }
    vis.IntersectRect(Rect(0, 0, S().screenW, S().screenH));
    out.visible = vis;
    return true;
}

uint32_t AllocHandle() {
    uint32_t h = S().nextHandle;
    S().nextHandle += 4;
    return h;
}

void RemoveFromParent(Window* w) {
    Window* p = ParentWin(w);
    if (!p) return;
    auto& v = p->children;
    v.erase(std::remove(v.begin(), v.end(), w->hwnd), v.end());
}

// Вставка в список детей родителя по правилам SetWindowPos
void InsertZ(Window* w, uint32_t after) {
    Window* p = ParentWin(w);
    if (!p) return;
    auto& v = p->children;
    v.erase(std::remove(v.begin(), v.end(), w->hwnd), v.end());
    if (after == 1) { v.push_back(w->hwnd); return; }           // HWND_BOTTOM
    if (after == 0 || after == 0xFFFFFFFFu || after == 0xFFFFFFFEu) {
        // HWND_TOP/TOPMOST/NOTOPMOST: наверх, но не выше окон «поверх всех»
        size_t pos = 0;
        if (!(w->exStyle & WS_EX_TOPMOST))
            while (pos < v.size()) { Window* o = Win(v[pos]); if (!o || !(o->exStyle & WS_EX_TOPMOST)) break; ++pos; }
        v.insert(v.begin() + static_cast<long>(pos), w->hwnd);
        return;
    }
    auto it = std::find(v.begin(), v.end(), after);
    if (it == v.end()) v.insert(v.begin(), w->hwnd);
    else v.insert(it + 1, w->hwnd);
}

void WriteRect(uint32_t addr, const Rect& r) {
    cpu::IGuestMemory& m = *G().m;
    m.Write32(addr, static_cast<uint32_t>(r.left));
    m.Write32(addr + 4, static_cast<uint32_t>(r.top));
    m.Write32(addr + 8, static_cast<uint32_t>(r.right));
    m.Write32(addr + 12, static_cast<uint32_t>(r.bottom));
}
Rect ReadRect(uint32_t addr) {
    cpu::IGuestMemory& m = *G().m;
    return Rect(static_cast<int32_t>(m.Read32(addr)), static_cast<int32_t>(m.Read32(addr + 4)),
                static_cast<int32_t>(m.Read32(addr + 8)), static_cast<int32_t>(m.Read32(addr + 12)));
}

// Клиентская область через WM_NCCALCSIZE (программа может задать свою рамку)
void RecalcClient(Window* w) {
    uint32_t hwnd = w->hwnd;
    StackBuffer buf(16);
    WriteRect(buf.addr(), w->rect);
    SendMsg(hwnd, WM_NCCALCSIZE, 0, buf.addr());
    w = Win(hwnd);
    if (!w) return;
    Rect c = ReadRect(buf.addr());
    c = c.Offset(-w->rect.left, -w->rect.top);
    if (c.right < c.left) c.right = c.left;
    if (c.bottom < c.top) c.bottom = c.top;
    w->client = c;
}

void SendSizeMove(Window* w) {
    uint32_t hwnd = w->hwnd;
    uint32_t sizeType = (w->style & WS_MAXIMIZE) ? 2 : (w->style & WS_MINIMIZE) ? 1 : 0;
    uint32_t cw = static_cast<uint32_t>(w->client.Width()) & 0xFFFF, ch = static_cast<uint32_t>(w->client.Height()) & 0xFFFF;
    SendMsg(hwnd, WM_SIZE, sizeType, cw | (ch << 16));
    w = Win(hwnd);
    if (!w) return;
    uint32_t mx = static_cast<uint32_t>(w->rect.left + w->client.left) & 0xFFFF;
    uint32_t my = static_cast<uint32_t>(w->rect.top + w->client.top) & 0xFFFF;
    SendMsg(hwnd, WM_MOVE, 0, mx | (my << 16));
}

uint32_t RootOf(uint32_t hwnd) {
    Window* w = Win(hwnd);
    while (w && (w->style & WS_CHILD) && Win(w->parent)) w = Win(w->parent);
    return w ? w->hwnd : 0;
}

void ActivateNextTopLevel(uint32_t except) {
    for (uint32_t c : Desktop()->children) {
        Window* w = Win(c);
        if (w && c != except && (w->style & WS_VISIBLE) && !(w->style & WS_DISABLED) && !(w->exStyle & WS_EX_TOOLWINDOW)) {
            SetActive(c);
            return;
        }
    }
    if (S().foreground == except) {S().foreground = 0;++S().foregroundSerial;}
}

// --- Рисование рамки ---

void Fill(uint32_t hdc, const Rect& r, int sysColor) {
    uint32_t b = ui::g_gdi.CreateSolidBrush(ui::SysColor(sysColor));
    ui::g_gdi.FillRect(hdc, r, b);
    ui::g_gdi.DeleteObject(b);
}

void FillColor(uint32_t hdc, const Rect& r, uint32_t colorref) {
    uint32_t b = ui::g_gdi.CreateSolidBrush(colorref);
    ui::g_gdi.FillRect(hdc, r, b);
    ui::g_gdi.DeleteObject(b);
}

// Объёмная рамка: два цвета снаружи, два внутри (как DrawEdge)
void Edge(uint32_t hdc, Rect r, int tlOuter, int brOuter, int tlInner, int brInner) {
    auto frame = [&](const Rect& q, int tl, int br) {
        Fill(hdc, Rect(q.left, q.top, q.right - 1, q.top + 1), tl);
        Fill(hdc, Rect(q.left, q.top, q.left + 1, q.bottom - 1), tl);
        Fill(hdc, Rect(q.left, q.bottom - 1, q.right, q.bottom), br);
        Fill(hdc, Rect(q.right - 1, q.top, q.right, q.bottom), br);
    };
    frame(r, tlOuter, brOuter);
    if (tlInner >= 0) frame(Rect(r.left + 1, r.top + 1, r.right - 1, r.bottom - 1), tlInner, brInner);
}

void CaptionButtons(Window* w, int bw, int captionH, Rect* closeR, Rect* maxR, Rect* minR) {
    Rect wr(0, 0, w->rect.Width(), w->rect.Height());
    int bh = captionH - 5, bwid = bh + 2;
    int top = bw + 2;
    Rect close(wr.right - bw - 2 - bwid, top, wr.right - bw - 2, top + bh);
    if (closeR) *closeR = close;
    Rect mx(close.left - 2 - bwid, top, close.left - 2, top + bh);
    Rect mn(mx.left - bwid, top, mx.left, top + bh);
    if (maxR) *maxR = (w->style & (WS_MAXIMIZEBOX | WS_MINIMIZEBOX)) ? mx : Rect();
    if (minR) *minR = (w->style & (WS_MAXIMIZEBOX | WS_MINIMIZEBOX)) ? mn : Rect();
}

void DrawCaptionButton(uint32_t hdc, const Rect& r, int kind, bool enabled) {
    if (r.Empty()) return;
    Fill(hdc, r, 15);
    Edge(hdc, r, 22, 21, 20, 16);
    uint32_t pen = ui::g_gdi.CreatePen(0, 1, enabled ? ui::SysColor(18) : ui::SysColor(17));
    uint32_t old = ui::g_gdi.SelectObject(hdc, pen);
    int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
    auto line = [&](int x0, int y0, int x1, int y1) {
        ui::g_gdi.MoveTo(hdc, x0, y0, nullptr);
        ui::g_gdi.LineTo(hdc, x1, y1);
    };
    if (kind == 0) { // крестик
        for (int d = 0; d < 2; ++d) {
            line(cx - 4 + d, cy - 3, cx + 3 + d, cy + 4);
            line(cx + 2 + d, cy - 3, cx - 5 + d, cy + 4);
        }
    } else if (kind == 1) { // развернуть
        line(cx - 4, cy - 4, cx + 5, cy - 4);
        line(cx - 4, cy - 3, cx + 5, cy - 3);
        line(cx - 4, cy - 4, cx - 4, cy + 4);
        line(cx + 4, cy - 4, cx + 4, cy + 4);
        line(cx - 4, cy + 4, cx + 5, cy + 4);
    } else { // свернуть
        line(cx - 4, cy + 2, cx + 2, cy + 2);
        line(cx - 4, cy + 3, cx + 2, cy + 3);
    }
    ui::g_gdi.SelectObject(hdc, old);
    ui::g_gdi.DeleteObject(pen);
}

} // namespace

// Отрисовка меню в полосе меню: реализована в user32_menu.cpp
void DrawMenuBarInto(Window* w, uint32_t hdc, const Rect& bar);

void RegisterHostWndProc(uint32_t address, HostWndProc fn, bool unicode) { HostProcs()[address] = {fn, unicode}; }

HostWndProc FindHostWndProc(uint32_t address, bool& unicode) {
    auto it = HostProcs().find(address);
    if (it == HostProcs().end()) return nullptr;
    unicode = it->second.second;
    return it->second.first;
}

Window* Win(uint32_t hwnd) {
    if (!hwnd) return nullptr;
    auto it = S().windows.find(hwnd);
    return it == S().windows.end() ? nullptr : it->second.get();
}

Window* Desktop() { return Win(S().desktop); }

Window* ParentWin(Window* w) {
    if (!w || w->hwnd == S().desktop) return nullptr;
    if (w->style & WS_CHILD) { if (Window* p = Win(w->parent)) return p; }
    return Desktop();
}

int ClientOriginX(Window* w) {
    if (!w || w->hwnd == S().desktop) return 0;
    return ClientOriginX(ParentWin(w)) + w->rect.left + w->client.left;
}
int ClientOriginY(Window* w) {
    if (!w || w->hwnd == S().desktop) return 0;
    return ClientOriginY(ParentWin(w)) + w->rect.top + w->client.top;
}

Rect ScreenRect(Window* w) {
    if (!w) return Rect();
    if (w->hwnd == S().desktop) return Rect(0, 0, S().screenW, S().screenH);
    Window* p = ParentWin(w);
    return w->rect.Offset(ClientOriginX(p), ClientOriginY(p));
}

Rect ScreenClientRect(Window* w) {
    if (!w) return Rect();
    int x = ClientOriginX(w), y = ClientOriginY(w);
    return Rect(x, y, x + w->client.Width(), y + w->client.Height());
}

bool IsVisibleChain(Window* w) {
    for (; w && w->hwnd != S().desktop; w = ParentWin(w))
        if (!(w->style & WS_VISIBLE)) return false;
    return w != nullptr;
}

int BorderWidth(uint32_t style, uint32_t exStyle) {
    if (style & WS_THICKFRAME) return kFrame;
    if ((style & WS_DLGFRAME) || (exStyle & WS_EX_DLGMODALFRAME)) return kDlgFrame;
    if (style & WS_BORDER) return 1;
    return 0;
}

void NonClientInsets(uint32_t style, uint32_t exStyle, bool hasMenu, Rect& in) {
    int b = BorderWidth(style, exStyle);
    in = Rect(b, b, b, b);
    if ((style & WS_CAPTION) == WS_CAPTION) in.top += (exStyle & WS_EX_TOOLWINDOW) ? kSmCaptionH : kCaptionH;
    if (hasMenu) in.top += kMenuH;
    int e = (exStyle & WS_EX_CLIENTEDGE) ? kEdge : 0;
    if (exStyle & WS_EX_STATICEDGE) e += 1;
    in.left += e; in.top += e; in.right += e; in.bottom += e;
    if (style & WS_VSCROLL) in.right += kScrollW;
    if (style & WS_HSCROLL) in.bottom += kScrollW;
}

Window* WindowFromPoint(int x, int y, bool skipDisabled) {
    Window* cur = Desktop();
    for (;;) {
        Window* hit = nullptr;
        for (uint32_t c : cur->children) {
            Window* w = Win(c);
            if (!w || !(w->style & WS_VISIBLE)) continue;
            if (!ScreenRect(w).Contains(x, y)) continue;
            hit = w;
            break;
        }
        if (!hit) return cur->hwnd == S().desktop ? nullptr : cur;
        if (skipDisabled && (hit->style & WS_DISABLED)) return cur->hwnd == S().desktop ? hit : cur;
        // в дочерние — только если точка в клиентской области
        if (!ScreenClientRect(hit).Contains(x, y)) return hit;
        cur = hit;
    }
}

// --- Классы ---

WndClass* FindClass(const std::u16string& name, uint16_t atom, uint32_t hInstance) {
    WndClass* any = nullptr;
    for (auto& c : S().classes) {
        bool match = atom ? c->atom == atom : EqualNoCase(c->name, name);
        if (!match) continue;
        if (!hInstance || c->hInstance == hInstance) return c.get();
        if (!any || c->system) any = c.get();
    }
    return any;
}

uint32_t RegisterClassInternal(const WndClass& wc) {
    for (auto& c : S().classes)
        if (!c->system && EqualNoCase(c->name, wc.name) && c->hInstance == wc.hInstance) return 0; // уже есть
    std::unique_ptr<WndClass> c(new WndClass(wc));
    c->atom = S().nextAtom++;
    c->clsExtra.assign(static_cast<size_t>(std::max(0, wc.cbClsExtra)), 0);
    uint32_t atom = c->atom;
    S().classes.push_back(std::move(c));
    return atom;
}

// --- Отрисовка ---

void InvalidateClient(Window* w, const Region& rgn, bool erase, bool forceChildren) {
    if (!w) return;
    Region r = rgn;
    r.IntersectRect(Rect(0, 0, w->client.Width(), w->client.Height()));
    if (r.Empty()) return;
    if (!NeedsPaint(w)) w->paintAnnounced=false;
    w->update.Union(r);
    if (erase) w->erase = true;
    if (!forceChildren && (w->style & WS_CLIPCHILDREN)) return;
    for (uint32_t ch : w->children) {
        Window* c = Win(ch);
        if (!c || !(c->style & WS_VISIBLE)) continue;
        Region part = r;
        part.IntersectRect(c->rect);
        if (part.Empty()) continue;
        Region nc = part;
        nc.SubtractRect(c->client.Offset(c->rect.left, c->rect.top));
        if (!nc.Empty()) c->ncDirty = true;
        part.Offset(-(c->rect.left + c->client.left), -(c->rect.top + c->client.top));
        InvalidateClient(c, part, true, forceChildren);
    }
}

void RepaintDesktop(const Region& screenRgn) {
    Window* desk = Desktop();
    if (!desk) return;
    Region r = screenRgn;
    r.IntersectRect(Rect(0, 0, S().screenW, S().screenH));
    for (uint32_t c : desk->children) {
        Window* w = Win(c);
        if (w && (w->style & WS_VISIBLE)) r.SubtractRect(ScreenRect(w));
    }
    uint32_t rgb = ui::ColorrefToRgb(ui::SysColor(1));
    for (const Rect& q : r.Rects()) ui::FillSurface(S().screen.s, Region(q), q, rgb);
    if(!r.Empty())ui::g_gdi.MarkScreenChanged();
}

void InvalidateScreen(const Rect& sr) {
    if (sr.Empty()) return;
    RepaintDesktop(Region(sr));
    for (uint32_t c : Desktop()->children) {
        Window* w = Win(c);
        if (!w || !(w->style & WS_VISIBLE)) continue;
        Rect wr = ScreenRect(w);
        Rect i = wr.Intersect(sr);
        if (i.Empty()) continue;
        Rect cr = ScreenClientRect(w);
        if (i != i.Intersect(cr)) w->ncDirty = true;
        Rect ci = i.Intersect(cr);
        if (!ci.Empty()) InvalidateClient(w, Region(ci.Offset(-cr.left, -cr.top)), true, true);
    }
}

bool NeedsPaint(Window* w) {
    return w && (w->style & WS_VISIBLE) && (w->ncDirty || !w->update.Empty() || w->internalPaint) && IsVisibleChain(w);
}

void UpdateWindowNow(uint32_t hwnd,bool children,bool ownThreadOnly) {
    Window* w=Win(hwnd);
    if(!rt::GuestExecution::Checkpoint() || !w || (ownThreadOnly && w->threadId!=CurrentGuiThread()) || !IsVisibleChain(w))return;
    if(w->ncDirty && w->update.Empty()) {
        SendMsg(hwnd,WM_NCPAINT,1,0);
        if((w=Win(hwnd)))w->ncDirty=false;
    }
    if(!rt::GuestExecution::Checkpoint())return;
    if((w=Win(hwnd)) && (!w->update.Empty() || w->internalPaint))SendMsg(hwnd,WM_PAINT,0,0);
    if(!children || !rt::GuestExecution::Checkpoint() || !(w=Win(hwnd)))return;
    const auto list=w->children;
    for(uint32_t child:list)UpdateWindowNow(child,true,ownThreadOnly);
}

void UpdateExposedWindows(const Rect& screenRect) {
    Window* desktop=Desktop();if(!desktop || screenRect.Empty())return;
    const auto list=desktop->children;
    for(uint32_t hwnd:list) {
        if(!rt::GuestExecution::Checkpoint())return;
        auto* w=Win(hwnd);
        if(w && w->threadId==CurrentGuiThread() && IsVisibleChain(w) && !ScreenRect(w).Intersect(screenRect).Empty())
            UpdateWindowNow(hwnd,true,true);
    }
}

void DrawNonClient(Window* w) {
    if (!w || !IsVisibleChain(w)) return;
    w->ncDirty = false;
    Rect wr(0, 0, w->rect.Width(), w->rect.Height());
    if (w->client == wr) return; // рамки нет
    uint32_t hdc = ui::g_gdi.CreateDc(ui::DcKind::Window, w->hwnd, true);
    // клиентскую область не трогаем
    ui::DcObj* dc = ui::g_gdi.Dc(hdc);
    dc->st.hasClip = true;
    dc->st.clip = Region(wr);
    dc->st.clip.SubtractRect(w->client);
    int b = BorderWidth(w->style, w->exStyle);
    bool caption = (w->style & WS_CAPTION) == WS_CAPTION;
    if (b >= 3) {
        Edge(hdc, wr, 22, 21, 20, 16);
        for (int i = 2; i < b; ++i)
            Edge(hdc, Rect(wr.left + i, wr.top + i, wr.right - i, wr.bottom - i), 15, 15, -1, -1);
    } else if (b == 1) {
        Edge(hdc, wr, 6, 6, -1, -1);
    }
    int y = b;
    if (caption) {
        int ch = (w->exStyle & WS_EX_TOOLWINDOW) ? kSmCaptionH : kCaptionH;
        Rect cap(b, b, wr.right - b, b + ch - 1);
        // градиент заголовка (классическая тема Windows 2000/XP)
        uint32_t c0 = ui::SysColor(w->active ? 2 : 3), c1 = ui::SysColor(w->active ? 27 : 28);
        int width = std::max(1, cap.Width());
        for (int x = cap.left; x < cap.right; ++x) {
            int k = x - cap.left;
            auto mix = [&](int sh) {
                int a = (c0 >> sh) & 0xFF, z = (c1 >> sh) & 0xFF;
                return static_cast<uint32_t>(a + (z - a) * k / width) << sh;
            };
            FillColor(hdc, Rect(x, cap.top, x + 1, cap.bottom), mix(0) | mix(8) | mix(16));
        }
        Fill(hdc, Rect(b, cap.bottom, wr.right - b, cap.bottom + 1), 15);
        // текст заголовка
        ui::FontDesc fd;
        fd.face = "Tahoma";
        fd.height = -11;
        fd.weight = 700;
        uint32_t font = ui::g_gdi.CreateFont(fd);
        uint32_t oldFont = ui::g_gdi.SelectObject(hdc, font);
        dc->st.bkMode = 1;
        dc->st.textColor = ui::SysColor(w->active ? 9 : 19);
        Rect closeR, maxR, minR;
        CaptionButtons(w, b, ch, &closeR, &maxR, &minR);
        int textRight = (w->style & WS_SYSMENU) ? (minR.Empty() ? closeR.left : minR.left) - 2 : cap.right;
        Rect tr(cap.left + 3, cap.top, textRight, cap.bottom);
        ui::g_gdi.DrawText(hdc, w->text, tr, 0x20 | 0x4 | 0x800 | 0x8000);
        ui::g_gdi.SelectObject(hdc, oldFont);
        ui::g_gdi.DeleteObject(font);
        if (w->style & WS_SYSMENU) {
            DrawCaptionButton(hdc, closeR, 0, true);
            DrawCaptionButton(hdc, maxR, 1, (w->style & WS_MAXIMIZEBOX) != 0);
            DrawCaptionButton(hdc, minR, 2, (w->style & WS_MINIMIZEBOX) != 0);
        }
        y += ch;
    }
    if (!(w->style & WS_CHILD) && w->menu) {
        Rect bar(b, y, wr.right - b, y + kMenuH);
        Fill(hdc, bar, 4);
        DrawMenuBarInto(w, hdc, bar);
        y += kMenuH;
    }
    // утопленная кромка клиентской области
    Rect inner = w->client;
    int e = (w->exStyle & WS_EX_CLIENTEDGE) ? kEdge : 0;
    if (w->style & WS_VSCROLL) inner.right += kScrollW;
    if (w->style & WS_HSCROLL) inner.bottom += kScrollW;
    if (e) Edge(hdc, Rect(inner.left - 2, inner.top - 2, inner.right + 2, inner.bottom + 2), 16, 20, 21, 22);
    else if (w->exStyle & WS_EX_STATICEDGE) Edge(hdc, Rect(inner.left - 1, inner.top - 1, inner.right + 1, inner.bottom + 1), 16, 20, -1, -1);
    // полосы прокрутки рисует user32_controls.cpp
    extern void DrawScrollBars(Window * w, uint32_t hdc);
    DrawScrollBars(w, hdc);
    ui::g_gdi.DeleteDc(hdc);
}

// --- Окна ---

uint32_t CreateWindowInternal(uint32_t exStyle, const std::u16string& className, uint16_t classAtom,
                              const std::u16string& title, uint32_t style, int x, int y, int cx, int cy,
                              uint32_t parent, uint32_t menu, uint32_t hInstance, uint32_t param, bool unicode,
                              uint32_t titlePtr, uint32_t classPtr) {
    cpu::IGuestMemory& m = *G().m;
    WndClass* cls = FindClass(className, classAtom, hInstance);
    if (!cls) { SetLastError(m, 1407); return 0; } // ERROR_CANNOT_FIND_WND_CLASS
    Window* parentW = Win(parent);
    if ((style & WS_CHILD) && !parentW) { SetLastError(m, 1406); return 0; } // ERROR_TLW_WITH_WSCHILD
    if (parent == S().desktop) parentW = nullptr;

    constexpr int kDefault = static_cast<int>(0x80000000u);
    bool overlapped = !(style & (WS_CHILD | WS_POPUP));
    if (overlapped) style |= WS_CLIPSIBLINGS | WS_CAPTION;
    static int cascade = 0;
    if (x == kDefault) {
        if (style & WS_CHILD) { x = y = 0; }
        else { x = 24 + (cascade % 8) * 24; y = 24 + (cascade % 8) * 24; ++cascade; }
    }
    if (cx == kDefault) {
        if (style & WS_CHILD) cx = cy = 0;
        else { cx = S().screenW * 3 / 4; cy = S().screenH * 3 / 4; }
    }

    std::unique_ptr<Window> win(new Window);
    Window* w = win.get();
    Q();
    w->threadId=CurrentGuiThread();
    w->hwnd = AllocHandle();
    w->cls = cls;
    w->style = style & ~WS_VISIBLE;
    w->exStyle = exStyle;
    if ((style & WS_CAPTION) == WS_CAPTION || (style & WS_THICKFRAME)) w->exStyle |= WS_EX_WINDOWEDGE;
    if (style & WS_CHILD) { w->parent = parent; w->id = menu; }
    else {
        // владелец — окно верхнего уровня
        w->owner = parentW ? RootOf(parent) : 0;
        w->menu = menu;
    }
    w->rect = Rect(x, y, x + cx, y + cy);
    w->client = Rect(0, 0, cx, cy);
    w->text = title;
    w->wndProc = cls->wndProc;
    w->unicode = cls->unicode;
    w->extra.assign(static_cast<size_t>(std::max(0, cls->cbWndExtra)), 0);
    w->hInstance = hInstance;
    uint32_t hwnd = w->hwnd;
    S().windows[hwnd] = std::move(win);
    InitializeDialogWindow(hwnd);
    // как в Windows: дочернее окно — в конец z-порядка (он же порядок Tab
    // в диалогах и обхода GetWindow), окно верхнего уровня — наверх
    InsertZ(w, (style & WS_CHILD) ? 1 : 0);
    ui::g_gdi.InvalidateLayout();

    // CREATESTRUCT на стеке гостя; строки — в кодировке окна
    std::u16string clsName = cls->name;
    StackBuffer strings(static_cast<uint32_t>((title.size() + clsName.size() + 2) * 2 + 8));
    uint32_t namePtr = titlePtr, classNamePtr = classPtr;
    if (unicode != w->unicode || !titlePtr) {
        namePtr = strings.addr();
        WriteText(namePtr, title, static_cast<uint32_t>(title.size() + 1), w->unicode);
    }
    if (unicode != w->unicode && classPtr >= 0x10000) {
        classNamePtr = strings.addr() + static_cast<uint32_t>((title.size() + 1) * 2);
        WriteText(classNamePtr, clsName, static_cast<uint32_t>(clsName.size() + 1), w->unicode);
    }
    StackBuffer csBuf(48);
    uint32_t cs = csBuf.addr();
    m.Write32(cs + 0, param);
    m.Write32(cs + 4, hInstance);
    m.Write32(cs + 8, menu);
    m.Write32(cs + 12, (style & WS_CHILD) ? parent : (parentW ? parent : 0));
    m.Write32(cs + 16, static_cast<uint32_t>(cy));
    m.Write32(cs + 20, static_cast<uint32_t>(cx));
    m.Write32(cs + 24, static_cast<uint32_t>(y));
    m.Write32(cs + 28, static_cast<uint32_t>(x));
    m.Write32(cs + 32, style);
    m.Write32(cs + 36, namePtr);
    m.Write32(cs + 40, classNamePtr);
    m.Write32(cs + 44, exStyle);

    if (!(style & WS_CHILD) && ((style & WS_THICKFRAME) || overlapped)) {
        StackBuffer mmi(40);
        for (uint32_t i = 0; i < 40; i += 4) m.Write32(mmi.addr() + i, 0);
        SendMsg(hwnd, WM_GETMINMAXINFO, 0, mmi.addr(), w->unicode);
    }
    {
        // WH_CBT, HCBT_CREATEWND: CBT_CREATEWND { CREATESTRUCT*, hwndInsertAfter }
        StackBuffer cbt(8);
        m.Write32(cbt.addr(), cs);
        m.Write32(cbt.addr() + 4, 0);
        if (CallHooks(5, 3, hwnd, cbt.addr())) {
            DestroyWindowInternal(hwnd);
            return 0;
        }
        if (!(w = Win(hwnd))) return 0;
    }
    if (!SendMsg(hwnd, WM_NCCREATE, 0, cs, w->unicode)) {
        DestroyWindowInternal(hwnd);
        return 0;
    }
    if (!(w = Win(hwnd))) return 0;
    RecalcClient(w);
    if (!(w = Win(hwnd))) return 0;
    if (static_cast<int32_t>(SendMsg(hwnd, WM_CREATE, 0, cs, w->unicode)) == -1) {
        DestroyWindowInternal(hwnd);
        return 0;
    }
    if (!(w = Win(hwnd))) return 0;
    SendSizeMove(w);
    if (!(w = Win(hwnd))) return 0;
    if ((style & WS_CHILD) && !(exStyle & 0x4 /* WS_EX_NOPARENTNOTIFY */))
        SendMsg(parent, WM_PARENTNOTIFY, WM_CREATE | (w->id << 16), hwnd);
    if (!(w = Win(hwnd))) return 0;
    if (style & WS_VISIBLE) {
        int cmd = (style & WS_MAXIMIZE) ? 3 : (style & WS_MINIMIZE) ? 7 : (style & WS_CHILD) ? 8 : 5;
        ShowWindowInternal(hwnd, cmd);
    }
    return Win(hwnd) ? hwnd : 0;
}

bool DestroyWindowInternal(uint32_t hwnd) {
    Window* w = Win(hwnd);
    if (!w || hwnd == S().desktop) return false;
    if (w->destroying) return true;
    w->destroying = true;
    ClearWindowFocus(hwnd);
    ClearWindowCapture(hwnd);
    if(!rt::GuestExecution::Checkpoint())return true;
    CallHooks(5, 4, hwnd, 0); // WH_CBT, HCBT_DESTROYWND
    if (!(w = Win(hwnd))) return true;
    // принадлежащие окна — раньше владельца
    std::vector<uint32_t> owned;
    for (auto& kv : S().windows)
        if (kv.second->owner == hwnd && !(kv.second->style & WS_CHILD)) owned.push_back(kv.first);
    for (uint32_t o : owned) DestroyWindowInternal(o);
    if (!(w = Win(hwnd))) return true;

    if (w->style & WS_VISIBLE) {
        Rect sr = ScreenRect(w);
        bool vis = IsVisibleChain(w);
        w->style &= ~WS_VISIBLE;
        ui::g_gdi.InvalidateLayout();
        if (vis) {
            Window* p = ParentWin(w);
            if (p == Desktop()) InvalidateScreen(sr);
            else if (p) {
                Rect pc = ScreenClientRect(p);
                InvalidateClient(p, Region(sr.Intersect(pc).Offset(-pc.left, -pc.top)), true, true);
            }
        }
    }
    if ((w->style & WS_CHILD) && !(w->exStyle & 0x4))
        SendMsg(w->parent, WM_PARENTNOTIFY, WM_DESTROY | (w->id << 16), hwnd);

    // WM_DESTROY: сначала окно, потом дети; WM_NCDESTROY — в обратном порядке
    std::vector<uint32_t> order;
    std::vector<uint32_t> stack{hwnd};
    while (!stack.empty()) {
        uint32_t h = stack.back();
        stack.pop_back();
        order.push_back(h);
        if (Window* x = Win(h)) for (auto it = x->children.rbegin(); it != x->children.rend(); ++it) stack.push_back(*it);
    }
    for (uint32_t h : order) {
        if (Window* x = Win(h)) x->destroying = true;
        if (S().capture == h) ReleaseCaptureInternal();
        SendMsg(h, WM_DESTROY, 0, 0);
    }
    for (auto it = order.rbegin(); it != order.rend(); ++it) SendMsg(*it, WM_NCDESTROY, 0, 0);
    bool wasActive = false;
    for (uint32_t h : order) {
        for (auto& queue:S().queues) {
            auto& tm=queue.second.timers;
            tm.erase(std::remove_if(tm.begin(),tm.end(),[&](const Timer& t) { return t.hwnd==h; }),tm.end());
        }
        if (S().foreground == h) wasActive = true;
        if (Window* x = Win(h)) RemoveFromParent(x);
        RetireWindow(h);
    }
    ui::g_gdi.InvalidateLayout();
    if (wasActive) ActivateNextTopLevel(0);
    return true;
}

bool SetWindowPosInternal(uint32_t hwnd, uint32_t after, int x, int y, int cx, int cy, uint32_t flags) {
    constexpr uint32_t NOSIZE = 1, NOMOVE = 2, NOZORDER = 4, NOREDRAW = 8, NOACTIVATE = 0x10, FRAMECHANGED = 0x20,
                       SHOW = 0x40, HIDE = 0x80, NOSENDCHANGING = 0x400;
    Window* w = Win(hwnd);
    if (!w || hwnd == S().desktop) return false;
    cpu::IGuestMemory& m = *G().m;
    if (flags & NOMOVE) { x = w->rect.left; y = w->rect.top; }
    if (flags & NOSIZE) { cx = w->rect.Width(); cy = w->rect.Height(); }
    if (cx < 0) cx = 0;
    if (cy < 0) cy = 0;
    StackBuffer wp(28);
    auto writeWp = [&]() {
        m.Write32(wp.addr(), hwnd);
        m.Write32(wp.addr() + 4, after);
        m.Write32(wp.addr() + 8, static_cast<uint32_t>(x));
        m.Write32(wp.addr() + 12, static_cast<uint32_t>(y));
        m.Write32(wp.addr() + 16, static_cast<uint32_t>(cx));
        m.Write32(wp.addr() + 20, static_cast<uint32_t>(cy));
        m.Write32(wp.addr() + 24, flags);
    };
    if (!(flags & NOSENDCHANGING) && !(flags & (SHOW | HIDE) && (flags & NOSIZE) && (flags & NOMOVE) && (flags & NOZORDER))) {
        writeWp();
        SendMsg(hwnd, WM_WINDOWPOSCHANGING, 0, wp.addr());
        if (!(w = Win(hwnd))) return false;
        x = static_cast<int32_t>(m.Read32(wp.addr() + 8));
        y = static_cast<int32_t>(m.Read32(wp.addr() + 12));
        cx = static_cast<int32_t>(m.Read32(wp.addr() + 16));
        cy = static_cast<int32_t>(m.Read32(wp.addr() + 20));
        flags = m.Read32(wp.addr() + 24);
    }
    bool wasVisible = IsVisibleChain(w);
    Rect oldScreen = ScreenRect(w);
    Rect oldRect = w->rect;
    Rect newRect(x, y, x + cx, y + cy);
    bool moved = newRect.left != oldRect.left || newRect.top != oldRect.top;
    bool sized = newRect.Width() != oldRect.Width() || newRect.Height() != oldRect.Height();
    w->rect = newRect;
    if (sized || (flags & FRAMECHANGED)) {
        Rect oldClient = w->client;
        RecalcClient(w);
        if (!(w = Win(hwnd))) return false;
        if (w->client != oldClient) sized = true;
    }
    bool zChanged = false;
    if (!(flags & NOZORDER)) {
        if (after == 0xFFFFFFFFu) w->exStyle |= WS_EX_TOPMOST;
        if (after == 0xFFFFFFFEu) w->exStyle &= ~WS_EX_TOPMOST;
        Window* p = ParentWin(w);
        std::vector<uint32_t> before = p ? p->children : std::vector<uint32_t>();
        InsertZ(w, after);
        zChanged = p && before != p->children;
    }
    if (flags & SHOW) w->style |= WS_VISIBLE;
    if (flags & HIDE) w->style &= ~WS_VISIBLE;
    ui::g_gdi.InvalidateLayout();
    bool nowVisible = IsVisibleChain(w);
    bool changed = moved || sized || zChanged || (flags & (SHOW | HIDE | FRAMECHANGED)) || wasVisible != nowVisible;
    if (changed && !(flags & NOREDRAW)) {
        Window* p = ParentWin(w);
        if (wasVisible) {
            if (p == Desktop()) InvalidateScreen(oldScreen);
            else if (p) {
                Rect pc = ScreenClientRect(p);
                // The vacated area may expose siblings even when the parent
                // has WS_CLIPCHILDREN. Repaint those children as well.
                InvalidateClient(p, Region(oldScreen.Intersect(pc).Offset(-pc.left, -pc.top)), true, true);
            }
        }
        if (nowVisible) {
            w->ncDirty = true;
            InvalidateClient(w, Region(Rect(0, 0, w->client.Width(), w->client.Height())), true, true);
        }
    }
    if (!(flags & NOSIZE) && !sized) flags |= NOSIZE;
    if (!(flags & NOMOVE) && !moved) flags |= NOMOVE;
    if (sized) flags &= ~NOSIZE;
    if (moved) flags &= ~NOMOVE;
    writeWp();
    m.Write32(wp.addr() + 24, flags);
    SendMsg(hwnd, WM_WINDOWPOSCHANGED, 0, wp.addr());
    if((flags&HIDE) && Win(hwnd)) {ClearWindowFocus(hwnd);ClearWindowCapture(hwnd);}
    (void)NOACTIVATE;
    return true;
}

bool ShowWindowInternal(uint32_t hwnd, int cmd) {
    Window* w = Win(hwnd);
    if (!w) return false;
    bool wasVisible = (w->style & WS_VISIBLE) != 0;
    constexpr uint32_t base = 0x2 | 0x1; // NOMOVE|NOSIZE
    bool topLevel = !(w->style & WS_CHILD);
    switch (cmd) {
    case 0: // SW_HIDE
        if (!wasVisible) return false;
        SendMsg(hwnd, WM_SHOWWINDOW, 0, 0);
        SetWindowPosInternal(hwnd, 0, 0, 0, 0, 0, base | 0x4 | 0x10 | 0x80);
        if (topLevel && S().foreground == hwnd) ActivateNextTopLevel(hwnd);
        if (Win(hwnd)) {ClearWindowFocus(hwnd);ClearWindowCapture(hwnd);}
        return true;
    case 3: { // SW_SHOWMAXIMIZED
        int b = BorderWidth(w->style, w->exStyle);
        w->style = (w->style | WS_MAXIMIZE) & ~WS_MINIMIZE;
        if (!wasVisible) SendMsg(hwnd, WM_SHOWWINDOW, 1, 0);
        if (topLevel) SetWindowPosInternal(hwnd, 0, -b, -b, S().screenW + 2 * b, S().screenH + 2 * b, 0x40 | 0x20);
        else {
            Window* p = ParentWin(w);
            SetWindowPosInternal(hwnd, 0, -b, -b, p->client.Width() + 2 * b, p->client.Height() + 2 * b, 0x40 | 0x20);
        }
        if (topLevel) SetActive(hwnd);
        return wasVisible;
    }
    case 2: case 6: case 7: case 11: // свернуть: окно уходит за экран, как в Windows
        w->style = (w->style | WS_MINIMIZE) & ~WS_MAXIMIZE;
        SetWindowPosInternal(hwnd, 0, -32000, -32000, 160, 24, 0x4 | 0x10 | 0x40 | 0x20);
        if (topLevel && S().foreground == hwnd && cmd != 2) ActivateNextTopLevel(hwnd);
        return wasVisible;
    case 9: case 1: // SW_RESTORE / SW_SHOWNORMAL
        if (w->style & (WS_MINIMIZE | WS_MAXIMIZE)) {
            w->style &= ~(WS_MINIMIZE | WS_MAXIMIZE);
            if (Window* x = Win(hwnd)) {
                Rect r = x->rect;
                if (r.left <= -32000) r = Rect(40, 40, 40 + S().screenW * 3 / 4, 40 + S().screenH * 3 / 4);
                SetWindowPosInternal(hwnd, 0, r.left, r.top, r.Width(), r.Height(), 0x4 | 0x20);
            }
        }
        [[fallthrough]];
    default: { // SW_SHOW, SW_SHOWNA, SW_SHOWNOACTIVATE, SW_SHOWDEFAULT
        bool activate = topLevel && cmd != 4 && cmd != 8 && !(w->exStyle & WS_EX_NOACTIVATE);
        if (!wasVisible) {
            SendMsg(hwnd, WM_SHOWWINDOW, 1, 0);
            if (!Win(hwnd)) return false;
            SetWindowPosInternal(hwnd, 0, 0, 0, 0, 0, base | 0x40 | (activate ? 0 : 0x4 | 0x10));
        }
        if (activate && Win(hwnd)) SetActive(hwnd);
        return wasVisible;
    }
    }
}

// Queue-local activation is distinct from the desktop foreground window.
// Publish state before callbacks; a nested activation/destruction wins.
void SetActive(uint32_t hwnd,bool foreground) {
    WindowCallScope scope;
    hwnd=RootOf(hwnd);
    Window* target=Win(hwnd);
    const uint32_t owner=target?target->threadId:CurrentGuiThread();
    auto& queue=QueueFor(owner);
    if(queue.closed || (target && (target->destroying || (target->style&WS_DISABLED))))return;
    const uint32_t old=queue.active,oldForeground=S().foreground;
    if(old==hwnd && (!foreground || oldForeground==hwnd))return;
    queue.active=hwnd;
    const uint64_t serial=++queue.activationSerial;
    if(foreground || oldForeground==old) {S().foreground=hwnd;++S().foregroundSerial;}
    const uint64_t desktopSerial=S().foregroundSerial;
    const auto valid=[&]() {
        return rt::GuestExecution::Checkpoint() && queue.activationSerial==serial &&
            S().foregroundSerial==desktopSerial && (!hwnd || (Win(hwnd)&&!Win(hwnd)->destroying));
    };
    const auto deactivate=[&](uint32_t previous) {
        if(!previous || previous==hwnd || !Win(previous))return;
        SendMsg(previous,WM_NCACTIVATE,0,0);
        if(valid() && Win(previous))SendMsg(previous,WM_ACTIVATE,0,hwnd);
    };
    deactivate(old);
    if(!valid())return;
    if(foreground && oldForeground!=old)deactivate(oldForeground);
    if(!valid() || !hwnd)return;
    if(!oldForeground) {SendMsg(hwnd,WM_ACTIVATEAPP,1,0);if(!valid())return;}
    target=Win(hwnd);
    if(S().foreground==hwnd) {
        Window* parent=ParentWin(target);
        const auto previous=parent?parent->children:std::vector<uint32_t>();
        const Rect footprint=ScreenRect(target);
        const bool visible=IsVisibleChain(target);
        InsertZ(target,0);ui::g_gdi.InvalidateLayout();
        // Activation changes visible pixels just like explicit SetWindowPos.
        // Mark damage before activation callbacks may request a synchronous paint.
        if(visible && parent && previous!=parent->children)InvalidateScreen(footprint);
    }
    SendMsg(hwnd,WM_NCACTIVATE,S().foreground==hwnd?1:0,0);
    if(valid())SendMsg(hwnd,WM_ACTIVATE,1,old);
}

uint32_t FocusForWindow(uint32_t hwnd) {
    const auto* window=Win(hwnd);
    if(!window)return 0;
    auto queue=S().queues.find(window->threadId);
    return queue==S().queues.end()?0:queue->second.focus;
}
uint32_t CaptureForThread(uint32_t id) {
    const auto* window=Win(S().capture);
    return window && window->threadId==id?window->hwnd:0;
}
uint32_t SetFocusInternal(uint32_t hwnd) {
    auto& queue=Q();
    const uint32_t old=queue.focus;
    if(hwnd) {
        Window* window=Win(hwnd);
        if(!window || window->destroying) {hle::SetLastError(*G().m,1400);return 0;}
        if(window->threadId!=CurrentGuiThread()) {hle::SetLastError(*G().m,5);return 0;}
        for(auto* parent=window;parent;parent=(parent->style&WS_CHILD)?Win(parent->parent):nullptr) {
            if(parent->threadId!=CurrentGuiThread()) {hle::SetLastError(*G().m,5);return 0;}
            if(parent->style&WS_DISABLED) {hle::SetLastError(*G().m,87);return 0;}
        }
        const uint32_t root=RootOf(hwnd);
        if(queue.active!=root)SetActive(root,false);
        if(!rt::GuestExecution::Checkpoint() || !Win(hwnd) || queue.active!=root)return old;
    }
    if(queue.closed || queue.focus==hwnd)return old;
    const uint32_t previous=queue.focus;
    queue.focus=hwnd;
    const uint64_t serial=++queue.focusSerial;
    if(Win(previous))SendMsg(previous,WM_KILLFOCUS,hwnd,0);
    if(rt::GuestExecution::Checkpoint() && queue.focusSerial==serial && hwnd && Win(hwnd))
        SendMsg(hwnd,WM_SETFOCUS,Win(previous)?previous:0,0);
    return old;
}
uint32_t SetCaptureInternal(uint32_t hwnd) {
    if(hwnd) {
        auto* window=Win(hwnd);
        if(!window || window->destroying) {hle::SetLastError(*G().m,1400);return 0;}
        if(window->threadId!=CurrentGuiThread()) {hle::SetLastError(*G().m,5);return 0;}
    }
    const uint32_t old=S().capture;
    if(!hwnd && old && !CaptureForThread(CurrentGuiThread()))return 0;
    S().capture=hwnd;
    if(old && old!=hwnd && Win(old))SendMsg(old,WM_CAPTURECHANGED,0,hwnd);
    return old;
}
bool ReleaseCaptureInternal() {
    // A peer cannot release another queue's capture.
    if(CaptureForThread(CurrentGuiThread()))SetCaptureInternal(0);
    return true;
}
void ClearWindowFocus(uint32_t hwnd) {
    auto* window=Win(hwnd);if(!window)return;
    auto& queue=QueueFor(window->threadId);
    for(auto* focus=Win(queue.focus);focus;focus=(focus->style&WS_CHILD)?Win(focus->parent):nullptr)
        if(focus->hwnd==hwnd) {
            const uint32_t old=queue.focus;queue.focus=0;++queue.focusSerial;
            SendMsg(old,WM_KILLFOCUS,0,0);break;
        }
}
void ClearWindowCapture(uint32_t hwnd) {
    for(auto* capture=Win(S().capture);capture;capture=(capture->style&WS_CHILD)?Win(capture->parent):nullptr)
        if(capture->hwnd==hwnd) {
            const uint32_t old=S().capture;S().capture=0;
            SendMsg(old,WM_CAPTURECHANGED,0,0);break;
        }
}
void ForgetWindowInput(uint32_t hwnd) {
    for(auto& item:S().queues) {
        auto& queue=item.second;
        if(queue.focus==hwnd) {queue.focus=0;++queue.focusSerial;}
        if(queue.active==hwnd) {queue.active=0;++queue.activationSerial;}
    }
    if(S().foreground==hwnd) {S().foreground=0;++S().foregroundSerial;}
    if(S().capture==hwnd) {S().capture=0;}
}

// --- Сообщения ---

uint32_t SendMsg(uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp, bool fromUnicode) {
    uint32_t result=0;
    SendMsgTimeout(hwnd,msg,wp,lp,fromUnicode,0xffffffffu,0,result);
    return result;
}

uint32_t CallProc(uint32_t proc, bool procUnicode, uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp, bool fromUnicode) {
    WindowCallScope windowScope;
    if (!proc) return 0;
    bool hostUnicode;
    if (HostWndProc fn = FindHostWndProc(proc, hostUnicode)) return fn(hwnd, msg, wp, lp, fromUnicode);
    cpu::CpuState local=*G().c;
    const GuestCtx saved=G();
    struct Restore { GuestCtx saved; ~Restore() { G()=saved; } } restore{saved};
    G()={&local,saved.m};
    cpu::IGuestMemory& m = *G().m;
    // Перекодировка текста, если кодировки вызывающего и процедуры разные
    if (fromUnicode != procUnicode) {
        if ((msg == WM_SETTEXT) && lp) {
            std::u16string t = ReadText(lp, fromUnicode);
            StackBuffer buf(static_cast<uint32_t>(t.size() * 2 + 4));
            WriteText(buf.addr(), t, static_cast<uint32_t>(t.size() + 1), procUnicode);
            return CallGuestFunctionStdcall(*G().c, m, proc, {hwnd, msg, wp, buf.addr()}, g_tebAddr, g_tebAddr, 0x7FFFFFFF);
        }
        if (msg == WM_GETTEXT && lp && wp) {
            StackBuffer buf(wp * 2 + 4);
            CallGuestFunctionStdcall(*G().c, m, proc, {hwnd, msg, wp, buf.addr()}, g_tebAddr, g_tebAddr, 0x7FFFFFFF);
            std::u16string t = ReadText(buf.addr(), procUnicode);
            return WriteText(lp, t, wp, fromUnicode);
        }
    }
    return CallGuestFunctionStdcall(*G().c, m, proc, {hwnd, msg, wp, lp}, g_tebAddr, g_tebAddr, 0x7FFFFFFF);
}

void PostMsg(uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp) {
    if (!hwnd) Q();
    const Window* window=Win(hwnd);
    PostToThread(window && window->threadId ? window->threadId : CurrentGuiThread(),hwnd,msg,wp,lp);
}

// --- Строки и стек ---

std::u16string ReadText(uint32_t ptr, bool unicode) {
    if (!ptr) return std::u16string();
    return unicode ? ReadWide(*G().m, ptr) : AnsiToWide(ReadAnsi(*G().m, ptr));
}

uint32_t WriteText(uint32_t ptr, const std::u16string& s, uint32_t cap, bool unicode) {
    if (unicode) return WriteWide(*G().m, ptr, s, cap);
    std::string a;
    for (char16_t ch : s) a.push_back(ch < 256 ? static_cast<char>(ch) : '?');
    return WriteAnsi(*G().m, ptr, a, cap);
}

StackBuffer::StackBuffer(uint32_t size) {
    cpu::CpuState& c = *G().c;
    savedEsp_ = c.ESP();
    uint32_t sz = (size + 15) & ~15u;
    c.ESP() = (savedEsp_ - sz - 16) & ~15u;
    addr_ = c.ESP();
}
StackBuffer::~StackBuffer() { G().c->ESP() = savedEsp_; }

// --- DefWindowProc ---

namespace {

uint32_t SysBrush(int index) {
    static uint32_t brushes[31] = {};
    if (index < 0 || index >= 31) return 0;
    if (!brushes[index]) {
        ui::BrushObj b;
        b.sysColorIndex = index;
        brushes[index] = ui::g_gdi.CreateBrush(b);
        ui::g_gdi.MarkStock(brushes[index]);
    }
    return brushes[index];
}

uint32_t HitTest(Window* w, int sx, int sy) {
    Rect wr = ScreenRect(w);
    if (!wr.Contains(sx, sy)) return HTNOWHERE;
    int x = sx - wr.left, y = sy - wr.top;
    if (w->client.Contains(x, y)) return HTCLIENT;
    int b = BorderWidth(w->style, w->exStyle);
    bool caption = (w->style & WS_CAPTION) == WS_CAPTION;
    int ch = (w->exStyle & WS_EX_TOOLWINDOW) ? kSmCaptionH : kCaptionH;
    if (caption && y >= b && y < b + ch && x >= b && x < wr.Width() - b) {
        if (w->style & WS_SYSMENU) {
            Rect closeR, maxR, minR;
            CaptionButtons(w, b, ch, &closeR, &maxR, &minR);
            if (closeR.Contains(x, y)) return HTCLOSE;
            if (maxR.Contains(x, y)) return HTMAXBUTTON;
            if (minR.Contains(x, y)) return HTMINBUTTON;
        }
        return HTCAPTION;
    }
    int menuTop = b + (caption ? ch : 0);
    if (!(w->style & WS_CHILD) && w->menu && y >= menuTop && y < menuTop + kMenuH) return HTMENU;
    if ((w->style & WS_VSCROLL) && x >= w->client.right && x < w->client.right + kScrollW && y >= w->client.top && y < w->client.bottom)
        return HTVSCROLL;
    if ((w->style & WS_HSCROLL) && y >= w->client.bottom && y < w->client.bottom + kScrollW && x >= w->client.left && x < w->client.right)
        return HTHSCROLL;
    if (w->style & WS_THICKFRAME) {
        bool l = x < b, r = x >= wr.Width() - b, t = y < b, bt = y >= wr.Height() - b;
        if (t && l) return HTTOPLEFT;
        if (t && r) return HTTOPRIGHT;
        if (bt && l) return HTBOTTOMLEFT;
        if (bt && r) return HTBOTTOMRIGHT;
        if (l) return HTLEFT;
        if (r) return HTRIGHT;
        if (t) return HTTOP;
        if (bt) return HTBOTTOM;
    }
    return HTBORDER;
}

} // namespace

uint32_t SysColorBrush(int index) { return SysBrush(index); }

uint32_t DefWindowProc(uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp, bool unicode) {
    Window* w = Win(hwnd);
    if (!w) return 0;
    cpu::IGuestMemory& m = *G().m;
    switch (msg) {
    case WM_NCCREATE:
        return 1;
    case WM_NCCALCSIZE: {
        if (!lp) return 0;
        Rect r = ReadRect(lp);
        Rect in;
        NonClientInsets(w->style, w->exStyle, !(w->style & WS_CHILD) && w->menu, in);
        r.left += in.left; r.top += in.top; r.right -= in.right; r.bottom -= in.bottom;
        if (r.right < r.left) r.right = r.left;
        if (r.bottom < r.top) r.bottom = r.top;
        WriteRect(lp, r);
        return 0;
    }
    case WM_NCHITTEST:
        return HitTest(w, static_cast<int16_t>(lp & 0xFFFF), static_cast<int16_t>(lp >> 16));
    case WM_NCPAINT:
        DrawNonClient(w);
        return 0;
    case WM_NCACTIVATE:
        w->active = wp != 0;
        if (IsVisibleChain(w)) DrawNonClient(w);
        return 1;
    case WM_ERASEBKGND: {
        uint32_t brush = w->cls ? w->cls->hbrBackground : 0;
        if (!brush) return 0;
        if (brush <= 31) brush = SysBrush(static_cast<int>(brush) - 1);
        ui::g_gdi.FillRect(wp, Rect(0, 0, w->client.Width(), w->client.Height()), brush);
        return 1;
    }
    case WM_PAINT: {
        // проверить область: программа не рисует сама
        if (w->ncDirty) DrawNonClient(w);
        if (w->erase) {
            uint32_t hdc = ui::g_gdi.CreateDc(ui::DcKind::Window, hwnd, false);
            SendMsg(hwnd, WM_ERASEBKGND, hdc, 0);
            ui::g_gdi.DeleteDc(hdc);
        }
        if ((w = Win(hwnd))) { w->update.Clear(); w->paintAnnounced=false; w->erase = false; w->internalPaint = false; }
        return 0;
    }
    case WM_CLOSE:
        DestroyWindowInternal(hwnd);
        return 0;
    case WM_SETTEXT:
        w->text = ReadText(lp, unicode);
        if ((w->style & WS_CAPTION) == WS_CAPTION && IsVisibleChain(w)) DrawNonClient(w);
        return 1;
    case WM_GETTEXT: {
        if (!wp || !lp) return 0;
        return WriteText(lp, w->text, wp, unicode);
    }
    case WM_GETTEXTLENGTH:
        return static_cast<uint32_t>(w->text.size());
    case WM_WINDOWPOSCHANGED: {
        uint32_t flags = lp ? m.Read32(lp + 24) : 0;
        if (!(flags & 2)) {
            uint32_t mx = static_cast<uint32_t>(w->rect.left + w->client.left) & 0xFFFF;
            uint32_t my = static_cast<uint32_t>(w->rect.top + w->client.top) & 0xFFFF;
            SendMsg(hwnd, WM_MOVE, 0, mx | (my << 16));
            if (!(w = Win(hwnd))) return 0;
        }
        if (!(flags & 1)) {
            uint32_t sizeType = (w->style & WS_MAXIMIZE) ? 2 : (w->style & WS_MINIMIZE) ? 1 : 0;
            SendMsg(hwnd, WM_SIZE, sizeType,
                    (static_cast<uint32_t>(w->client.Width()) & 0xFFFF) | (static_cast<uint32_t>(w->client.Height()) << 16));
        }
        return 0;
    }
    case WM_ACTIVATE:
        if ((wp & 0xFFFF) && !(w->style & WS_MINIMIZE) && RootOf(FocusForWindow(hwnd))!=RootOf(hwnd))
            SetFocusInternal(hwnd);
        return 0;
    case WM_MOUSEACTIVATE:
        if ((w->style & WS_CHILD) && Win(w->parent)) {
            uint32_t r = SendMsg(w->parent, msg, wp, lp, unicode);
            if (r) return r;
        }
        return 1; // MA_ACTIVATE
    case WM_SETCURSOR:
        if ((w->style & WS_CHILD) && Win(w->parent)) return SendMsg(w->parent, msg, wp, lp, unicode);
        return 0;
    case WM_MOUSEWHEEL:
        if ((w->style & WS_CHILD) && Win(w->parent)) return SendMsg(w->parent, msg, wp, lp, unicode);
        return 0;
    case WM_NCLBUTTONDOWN:
        if (wp == HTVSCROLL || wp == HTHSCROLL) {
            TrackWindowScrollBar(hwnd, wp == HTVSCROLL, static_cast<int16_t>(lp & 0xFFFF), static_cast<int16_t>(lp >> 16));
            return 0;
        }
        if (wp == HTMENU) {
            if (!(w->style & WS_CHILD)) SetActive(hwnd);
            StartMenuBar(hwnd, static_cast<int16_t>(lp & 0xFFFF), static_cast<int16_t>(lp >> 16), false);
            return 0;
        }
        switch (wp) {
        case HTCLOSE: SendMsg(hwnd, WM_SYSCOMMAND, 0xF060, lp, unicode); break;
        case HTMINBUTTON: SendMsg(hwnd, WM_SYSCOMMAND, 0xF020, lp, unicode); break;
        case HTMAXBUTTON: SendMsg(hwnd, WM_SYSCOMMAND, (w->style & WS_MAXIMIZE) ? 0xF120 : 0xF030, lp, unicode); break;
        case HTCAPTION: case HTCLIENT: break;
        }
        if (!(w->style & WS_CHILD)) SetActive(hwnd);
        return 0;
    case WM_NCLBUTTONDBLCLK:
        if (wp == HTCAPTION && (w->style & WS_MAXIMIZEBOX))
            SendMsg(hwnd, WM_SYSCOMMAND, (w->style & WS_MAXIMIZE) ? 0xF120 : 0xF030, lp, unicode);
        return 0;
    case WM_SYSCOMMAND:
        switch (wp & 0xFFF0) {
        case 0xF060: SendMsg(hwnd, WM_CLOSE, 0, 0, unicode); break;
        case 0xF020: ShowWindowInternal(hwnd, 6); break;
        case 0xF030: ShowWindowInternal(hwnd, 3); break;
        case 0xF120: ShowWindowInternal(hwnd, 9); break;
        case 0xF100: // SC_KEYMENU: Alt/F10 — полоса меню с клавиатуры
            if (!(w->style & WS_CHILD) && w->menu) StartMenuBar(hwnd, 0, 0, true);
            break;
        }
        return 0;
    case WM_SYSKEYDOWN:
        if (wp == 0x73 /* VK_F4 */ && (lp & (1u << 29))) SendMsg(hwnd, WM_SYSCOMMAND, 0xF060, 0, unicode);
        return 0;
    case WM_SYSKEYUP:
        // отпущенный Alt или F10 открывают меню окна верхнего уровня
        if (wp == 0x12 || wp == 0x79) {
            uint32_t root = hwnd;
            for (Window* r = w; r && (r->style & WS_CHILD); r = Win(r->parent)) root = r->parent;
            SendMsg(root, WM_SYSCOMMAND, 0xF100, 0, unicode);
        }
        return 0;
    case WM_CTLCOLORMSGBOX: case WM_CTLCOLOREDIT: case WM_CTLCOLORLISTBOX: case WM_CTLCOLORBTN:
    case WM_CTLCOLORDLG: case WM_CTLCOLORSCROLLBAR: case WM_CTLCOLORSTATIC: {
        bool white = msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORLISTBOX;
        if (ui::DcObj* dc = ui::g_gdi.Dc(wp)) {
            dc->st.textColor = ui::SysColor(white ? 8 : 18);
            dc->st.bkColor = ui::SysColor(white ? 5 : 15);
        }
        if (msg == WM_CTLCOLORSCROLLBAR) return SysBrush(0);
        return SysBrush(white ? 5 : 15);
    }
    case WM_QUERYENDSESSION:
        return 1;
    case WM_GETICON:
        return wp ? w->hIconBig : w->hIconSmall;
    case WM_SETICON: {
        uint32_t old = wp ? w->hIconBig : w->hIconSmall;
        (wp ? w->hIconBig : w->hIconSmall) = lp;
        return old;
    }
    case WM_SETREDRAW:
        w->redraw = wp != 0;
        return 0;
    case 0x001F: // WM_CANCELMODE
        if (S().capture == hwnd) ReleaseCaptureInternal();
        return 0;
    case WM_SHOWWINDOW:
        return 0;
    }
    return 0;
}

// Включение подсистемы: экран, рабочий стол
void Init() {
    State& s = S();
    if (s.initialized) return;
    s.initialized = true;
    s.screen.Allocate(s.screenW, s.screenH, 32);
    ui::g_gdi.SetScreen(&s.screen.s);
    ui::g_gdi.SetWindowDcProvider(&DcProvider);
    std::unique_ptr<Window> desk(new Window);
    desk->hwnd = AllocHandle();
    desk->style = WS_VISIBLE | WS_POPUP | WS_CLIPCHILDREN;
    desk->rect = Rect(0, 0, s.screenW, s.screenH);
    desk->client = desk->rect;
    desk->text = u"";
    s.desktop = desk->hwnd;
    s.windows[desk->hwnd] = std::move(desk);
    RepaintDesktop(Region(Rect(0, 0, s.screenW, s.screenH)));
    s.cursorX = s.screenW / 2;
    s.cursorY = s.screenH / 2;
    extern void RegisterSystemClasses();
    RegisterSystemClasses();
}

} // namespace user
} // namespace win32
