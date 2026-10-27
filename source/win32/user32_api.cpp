// user32_api.cpp — функции USER32 для гостя (слой "ui2"): разбор
// аргументов и вызов ядра оконной подсистемы (user32_window.cpp,
// user32_message.cpp).

#include <algorithm>
#include <map>
#include <vector>
#include <cstdio>
#include <cstring>

#include "../runtime/clock.h"
#include "../runtime/process.h"
#include "../runtime/resources.h"
#include "msvcrt_format.h"
#include "user32.h"

namespace win32 {
namespace user {

using namespace hle;

namespace {

#define FN(name) HostCallOutcome name(cpu::CpuState& c, cpu::IGuestMemory& m)
#define ENTER (G().c = &c, G().m = &m, (void)Q())
#define AW(name)                                       \
    FN(name##A) { ENTER; return name##Impl(c, m, false); } \
    FN(name##W) { ENTER; return name##Impl(c, m, true); }

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

// Имя класса/ресурса: указатель на строку или число (MAKEINTATOM)
void ReadNameOrAtom(uint32_t p, bool unicode, std::u16string& name, uint16_t& atom) {
    if (p < 0x10000) { atom = static_cast<uint16_t>(p); name.clear(); }
    else { atom = 0; name = ReadText(p, unicode); }
}

rt::Module* ModuleOf(uint32_t hInstance) {
    if (!rt::g_process) return nullptr;
    if (!hInstance) return rt::g_process->MainModule();
    return rt::g_process->FindByHandle(hInstance);
}

// --- Классы ---

HostCallOutcome RegisterClassImplCommon(cpu::CpuState& c, cpu::IGuestMemory& m, bool ex, bool unicode) {
    uint32_t p = A(c, m, 0);
    WndClass wc;
    uint32_t o = ex ? 4 : 0;
    wc.style = m.Read32(p + o);
    wc.wndProc = m.Read32(p + o + 4);
    wc.cbClsExtra = static_cast<int32_t>(m.Read32(p + o + 8));
    wc.cbWndExtra = static_cast<int32_t>(m.Read32(p + o + 12));
    wc.hInstance = m.Read32(p + o + 16);
    wc.hIcon = m.Read32(p + o + 20);
    wc.hCursor = m.Read32(p + o + 24);
    wc.hbrBackground = m.Read32(p + o + 28);
    wc.menuName = m.Read32(p + o + 32);
    uint32_t namePtr = m.Read32(p + o + 36);
    if (ex) wc.hIconSm = m.Read32(p + 44);
    wc.unicode = unicode;
    uint16_t atom;
    ReadNameOrAtom(namePtr, unicode, wc.name, atom);
    if (wc.name.empty() && atom) { wc.name = u"#" + std::u16string(1, static_cast<char16_t>('0' + atom % 10)); }
    uint32_t r = RegisterClassInternal(wc);
    if (!r) SetLastError(m, 1410); // ERROR_CLASS_ALREADY_EXISTS
    return Ret(r);
}
HostCallOutcome RegisterClassImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool u) { return RegisterClassImplCommon(c, m, false, u); }
HostCallOutcome RegisterClassExImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool u) { return RegisterClassImplCommon(c, m, true, u); }
AW(RegisterClass)
AW(RegisterClassEx)

HostCallOutcome UnregisterClassImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool unicode) {
    std::u16string name;
    uint16_t atom;
    ReadNameOrAtom(A(c, m, 0), unicode, name, atom);
    WndClass* cls = FindClass(name, atom, A(c, m, 1));
    if (!cls || cls->system) return Ret(0);
    for (auto& kv : S().windows) if (kv.second->cls == cls) return Ret(0); // ERROR_CLASS_HAS_WINDOWS
    auto& v = S().classes;
    for (auto it=v.begin();it!=v.end();++it) if (it->get()==cls) {
        S().retiredClasses.push_back(std::move(*it));v.erase(it);break;
    }
    return Ret(1);
}
AW(UnregisterClass)

void WriteClassInfo(cpu::IGuestMemory& m, uint32_t p, const WndClass* cls, bool ex, uint32_t namePtr) {
    uint32_t o = ex ? 4 : 0;
    if (ex) m.Write32(p, 48);
    m.Write32(p + o, cls->style);
    m.Write32(p + o + 4, cls->wndProc);
    m.Write32(p + o + 8, static_cast<uint32_t>(cls->cbClsExtra));
    m.Write32(p + o + 12, static_cast<uint32_t>(cls->cbWndExtra));
    m.Write32(p + o + 16, cls->hInstance);
    m.Write32(p + o + 20, cls->hIcon);
    m.Write32(p + o + 24, cls->hCursor);
    m.Write32(p + o + 28, cls->hbrBackground);
    m.Write32(p + o + 32, cls->menuName);
    m.Write32(p + o + 36, namePtr);
    if (ex) m.Write32(p + 44, cls->hIconSm);
}

HostCallOutcome GetClassInfoCommon(cpu::CpuState& c, cpu::IGuestMemory& m, bool ex, bool unicode) {
    std::u16string name;
    uint16_t atom;
    ReadNameOrAtom(A(c, m, 1), unicode, name, atom);
    WndClass* cls = FindClass(name, atom, A(c, m, 0));
    if (!cls) { SetLastError(m, 1411); return Ret(0); } // ERROR_CLASS_DOES_NOT_EXIST
    WriteClassInfo(m, A(c, m, 2), cls, ex, A(c, m, 1));
    return Ret(ex ? cls->atom : 1);
}
HostCallOutcome GetClassInfoImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool u) { return GetClassInfoCommon(c, m, false, u); }
HostCallOutcome GetClassInfoExImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool u) { return GetClassInfoCommon(c, m, true, u); }
AW(GetClassInfo)
AW(GetClassInfoEx)

HostCallOutcome GetClassNameImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool unicode) {
    Window* w = Win(A(c, m, 0));
    if (!w || !w->cls) return Ret(0);
    return Ret(WriteText(A(c, m, 1), w->cls->name, A(c, m, 2), unicode));
}
AW(GetClassName)

uint32_t ClassLong(WndClass* cls, int idx, uint32_t* set, uint32_t value) {
    auto field = [&](uint32_t& f) { uint32_t old = f; if (set) f = value; return old; };
    switch (idx) {
    case -10: return field(cls->hbrBackground);
    case -12: return field(cls->hCursor);
    case -14: return field(cls->hIcon);
    case -34: return field(cls->hIconSm);
    case -26: return field(cls->style);
    case -24: return field(cls->wndProc);
    case -16: return field(cls->hInstance);
    case -8: return field(cls->menuName);
    case -18: return static_cast<uint32_t>(cls->cbWndExtra);
    case -20: return static_cast<uint32_t>(cls->cbClsExtra);
    case -32: return cls->atom;
    }
    if (idx >= 0 && static_cast<size_t>(idx) + 4 <= cls->clsExtra.size()) {
        uint32_t old;
        std::memcpy(&old, &cls->clsExtra[static_cast<size_t>(idx)], 4);
        if (set) std::memcpy(&cls->clsExtra[static_cast<size_t>(idx)], &value, 4);
        return old;
    }
    return 0;
}
FN(GetClassLongA) { ENTER; Window* w = Win(A(c, m, 0)); return Ret(w && w->cls ? ClassLong(w->cls, SA(c, m, 1), nullptr, 0) : 0); }
FN(SetClassLongA) {
    ENTER;
    Window* w = Win(A(c, m, 0));
    uint32_t dummy = 0;
    return Ret(w && w->cls ? ClassLong(w->cls, SA(c, m, 1), &dummy, A(c, m, 2)) : 0);
}

// --- Окна ---

HostCallOutcome CreateWindowExImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool unicode) {
    std::u16string cls, title;
    uint16_t atom;
    ReadNameOrAtom(A(c, m, 1), unicode, cls, atom);
    uint32_t titlePtr = A(c, m, 2);
    if (titlePtr >= 0x10000) title = ReadText(titlePtr, unicode);
    uint32_t h = CreateWindowInternal(A(c, m, 0), cls, atom, title, A(c, m, 3), SA(c, m, 4), SA(c, m, 5), SA(c, m, 6),
                                      SA(c, m, 7), A(c, m, 8), A(c, m, 9), A(c, m, 10), A(c, m, 11), unicode,
                                      titlePtr >= 0x10000 ? titlePtr : 0, A(c, m, 1));
    return Ret(h);
}
AW(CreateWindowEx)

FN(DestroyWindow) {
    ENTER;
    const auto* window=Win(A(c,m,0));
    if (!window || window->threadId!=CurrentGuiThread()) { SetLastError(m,5); return Ret(0); }
    return Ret(DestroyWindowInternal(window->hwnd) ? 1 : 0);
}
FN(ShowWindow) { ENTER; return Ret(ShowWindowInternal(A(c, m, 0), SA(c, m, 1)) ? 1 : 0); }
FN(SetWindowPos) {
    ENTER;
    return Ret(SetWindowPosInternal(A(c, m, 0), A(c, m, 1), SA(c, m, 2), SA(c, m, 3), SA(c, m, 4), SA(c, m, 5), A(c, m, 6)) ? 1 : 0);
}
// DeferWindowPos: позиции копятся и применяются разом в EndDeferWindowPos
struct DeferredPos { uint32_t hwnd, after; int x, y, cx, cy; uint32_t flags; };
struct DeferredState {std::map<uint32_t,std::vector<DeferredPos>> positions;uint32_t next=0x00D00000;};
std::map<uint32_t,std::vector<DeferredPos>>& Deferred(){return rt::LocalState<DeferredState>().positions;}
FN(BeginDeferWindowPos) {
    (void)c;(void)m;
    auto& next=rt::LocalState<DeferredState>().next;next+=4;
    Deferred()[next];return Ret(next);
}
FN(DeferWindowPos) {
    ENTER;
    auto it = Deferred().find(A(c, m, 0));
    if (it == Deferred().end() || !Win(A(c, m, 1))) return Ret(0);
    it->second.push_back(DeferredPos{A(c, m, 1), A(c, m, 2), SA(c, m, 3), SA(c, m, 4), SA(c, m, 5), SA(c, m, 6), A(c, m, 7)});
    return Ret(A(c, m, 0));
}
FN(EndDeferWindowPos) {
    ENTER;
    auto it = Deferred().find(A(c, m, 0));
    if (it == Deferred().end()) return Ret(0);
    std::vector<DeferredPos> list = std::move(it->second);
    Deferred().erase(it);
    for (const DeferredPos& d : list) SetWindowPosInternal(d.hwnd, d.after, d.x, d.y, d.cx, d.cy, d.flags);
    return Ret(1);
}
FN(MoveWindow) {
    ENTER;
    uint32_t flags = 0x4 | 0x10 | (A(c, m, 5) ? 0 : 0x8);
    return Ret(SetWindowPosInternal(A(c, m, 0), 0, SA(c, m, 1), SA(c, m, 2), SA(c, m, 3), SA(c, m, 4), flags) ? 1 : 0);
}
FN(BringWindowToTop) { ENTER; return Ret(SetWindowPosInternal(A(c, m, 0), 0, 0, 0, 0, 0, 0x1 | 0x2) ? 1 : 0); }

FN(GetWindowRect) {
    ENTER;
    Window* w = Win(A(c, m, 0));
    if (!w) return Ret(0);
    WriteRect(m, A(c, m, 1), ScreenRect(w));
    return Ret(1);
}
FN(GetClientRect) {
    ENTER;
    Window* w = Win(A(c, m, 0));
    if (!w) return Ret(0);
    WriteRect(m, A(c, m, 1), Rect(0, 0, w->client.Width(), w->client.Height()));
    return Ret(1);
}

HostCallOutcome AdjustCommon(cpu::CpuState& c, cpu::IGuestMemory& m, uint32_t style, bool menu, uint32_t ex) {
    uint32_t p = A(c, m, 0);
    Rect r = ReadRect(m, p), in;
    NonClientInsets(style, ex, menu, in);
    WriteRect(m, p, Rect(r.left - in.left, r.top - in.top, r.right + in.right, r.bottom + in.bottom));
    return Ret(1);
}
FN(AdjustWindowRect) { ENTER; return AdjustCommon(c, m, A(c, m, 1), A(c, m, 2) != 0, 0); }
FN(AdjustWindowRectEx) { ENTER; return AdjustCommon(c, m, A(c, m, 1), A(c, m, 2) != 0, A(c, m, 3)); }

FN(ClientToScreen) {
    ENTER;
    Window* w = Win(A(c, m, 0));
    uint32_t p = A(c, m, 1);
    if (!p) return Ret(0);
    int ox = w ? ClientOriginX(w) : 0, oy = w ? ClientOriginY(w) : 0;
    m.Write32(p, m.Read32(p) + static_cast<uint32_t>(ox));
    m.Write32(p + 4, m.Read32(p + 4) + static_cast<uint32_t>(oy));
    return Ret(w ? 1 : 0);
}
FN(ScreenToClient) {
    ENTER;
    Window* w = Win(A(c, m, 0));
    uint32_t p = A(c, m, 1);
    if (!p) return Ret(0);
    int ox = w ? ClientOriginX(w) : 0, oy = w ? ClientOriginY(w) : 0;
    m.Write32(p, m.Read32(p) - static_cast<uint32_t>(ox));
    m.Write32(p + 4, m.Read32(p + 4) - static_cast<uint32_t>(oy));
    return Ret(w ? 1 : 0);
}
FN(MapWindowPoints) {
    ENTER;
    Window* from = Win(A(c, m, 0));
    Window* to = Win(A(c, m, 1));
    int dx = (from ? ClientOriginX(from) : 0) - (to ? ClientOriginX(to) : 0);
    int dy = (from ? ClientOriginY(from) : 0) - (to ? ClientOriginY(to) : 0);
    uint32_t p = A(c, m, 2), n = A(c, m, 3);
    for (uint32_t i = 0; i < n; ++i) {
        m.Write32(p + i * 8, m.Read32(p + i * 8) + static_cast<uint32_t>(dx));
        m.Write32(p + i * 8 + 4, m.Read32(p + i * 8 + 4) + static_cast<uint32_t>(dy));
    }
    return Ret((static_cast<uint32_t>(dx) & 0xFFFF) | (static_cast<uint32_t>(dy) << 16));
}

uint32_t WindowLong(Window* w, int idx, bool set, uint32_t value, bool unicode) {
    if(set && !ui::g_gdi.PrepareCpuAccess())return 0;
    auto field = [&](uint32_t& f) { uint32_t old = f; if (set) f = value; return old; };
    switch (idx) {
    case -4: { // GWL_WNDPROC
        uint32_t old = w->wndProc;
        if (set) { w->wndProc = value; w->unicode = unicode; }
        return old;
    }
    case -6: return field(w->hInstance);
    case -8: return w->owner;
    case -12: return field(w->id);
    case -16: {
        uint32_t old = w->style;
        if (set) {
            uint32_t vis = w->style & WS_VISIBLE;
            w->style = (value & ~WS_VISIBLE) | vis;
            ui::g_gdi.InvalidateLayout();
        }
        return old;
    }
    case -20: return field(w->exStyle);
    case -21: return field(w->userData);
    }
    if (idx >= 0 && static_cast<size_t>(idx) + 4 <= w->extra.size()) {
        uint32_t old;
        std::memcpy(&old, &w->extra[static_cast<size_t>(idx)], 4);
        if (set) {
            std::memcpy(&w->extra[static_cast<size_t>(idx)], &value, 4);
            if(idx==4)SetDialogProcEncoding(w->hwnd,unicode); // 32-bit DWL_DLGPROC
        }
        return old;
    }
    return 0;
}
HostCallOutcome GetWindowLongImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool unicode) {
    Window* w = Win(A(c, m, 0));
    if (!w) { SetLastError(m, 1400); return Ret(0); }
    return Ret(WindowLong(w, SA(c, m, 1), false, 0, unicode));
}
HostCallOutcome SetWindowLongImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool unicode) {
    Window* w = Win(A(c, m, 0));
    if (!w) { SetLastError(m, 1400); return Ret(0); }
    return Ret(WindowLong(w, SA(c, m, 1), true, A(c, m, 2), unicode));
}
AW(GetWindowLong)
AW(SetWindowLong)
FN(GetWindowWord) {
    ENTER;
    Window* w = Win(A(c, m, 0));
    int idx = SA(c, m, 1);
    if (!w || idx < 0 || static_cast<size_t>(idx) + 2 > w->extra.size()) return Ret(0);
    return Ret(w->extra[static_cast<size_t>(idx)] | (w->extra[static_cast<size_t>(idx) + 1] << 8));
}
FN(SetWindowWord) {
    ENTER;
    Window* w = Win(A(c, m, 0));
    int idx = SA(c, m, 1);
    if (!w || idx < 0 || static_cast<size_t>(idx) + 2 > w->extra.size()) return Ret(0);
    uint32_t old = w->extra[static_cast<size_t>(idx)] | (w->extra[static_cast<size_t>(idx) + 1] << 8);
    w->extra[static_cast<size_t>(idx)] = static_cast<uint8_t>(A(c, m, 2));
    w->extra[static_cast<size_t>(idx) + 1] = static_cast<uint8_t>(A(c, m, 2) >> 8);
    return Ret(old);
}

HostCallOutcome GetWindowTextImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool unicode) {
    uint32_t hwnd = A(c, m, 0), buf = A(c, m, 1), cap = A(c, m, 2);
    if (!Win(hwnd) || !cap) return Ret(0);
    return Ret(SendMsg(hwnd, WM_GETTEXT, cap, buf, unicode));
}
HostCallOutcome SetWindowTextImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool unicode) {
    return Ret(SendMsg(A(c, m, 0), WM_SETTEXT, 0, A(c, m, 1), unicode) ? 1 : 0);
}
HostCallOutcome GetWindowTextLengthImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool unicode) {
    return Ret(SendMsg(A(c, m, 0), WM_GETTEXTLENGTH, 0, 0, unicode));
}
AW(GetWindowText)
AW(SetWindowText)
AW(GetWindowTextLength)

FN(GetParent) {
    ENTER;
    Window* w = Win(A(c, m, 0));
    if (!w) return Ret(0);
    if (w->style & WS_CHILD) return Ret(w->parent);
    return Ret((w->style & WS_POPUP) ? w->owner : 0);
}
FN(SetParent) {
    ENTER;
    if(!ui::g_gdi.PrepareCpuAccess())return Ret(0);
    Window* w = Win(A(c, m, 0));
    if (!w) return Ret(0);
    uint32_t old = (w->style & WS_CHILD) ? w->parent : S().desktop;
    uint32_t np = A(c, m, 1);
    Window* nw = Win(np);
    if (Window* p = ParentWin(w)) { auto& v = p->children; v.erase(std::remove(v.begin(), v.end(), w->hwnd), v.end()); }
    if (nw && np != S().desktop) { w->parent = np; nw->children.insert(nw->children.begin(), w->hwnd); }
    else { w->parent = 0; w->style &= ~WS_CHILD; Desktop()->children.insert(Desktop()->children.begin(), w->hwnd); }
    ui::g_gdi.InvalidateLayout();
    if (w->style & WS_VISIBLE) { w->ncDirty = true; InvalidateClient(w, Region(Rect(0, 0, w->client.Width(), w->client.Height())), true, true); }
    return Ret(old);
}
FN(GetDesktopWindow) { ENTER; return Ret(S().desktop); }
FN(GetWindow) {
    ENTER;
    Window* w = Win(A(c, m, 0));
    if (!w) return Ret(0);
    uint32_t cmd = A(c, m, 1);
    Window* p = ParentWin(w);
    auto& sib = p ? p->children : w->children;
    switch (cmd) {
    case 0: return Ret(sib.empty() ? 0 : sib.front());  // GW_HWNDFIRST
    case 1: return Ret(sib.empty() ? 0 : sib.back());   // GW_HWNDLAST
    case 2: case 3: {                                    // GW_HWNDNEXT / PREV
        auto it = std::find(sib.begin(), sib.end(), w->hwnd);
        if (it == sib.end()) return Ret(0);
        if (cmd == 2) return Ret(it + 1 == sib.end() ? 0 : *(it + 1));
        return Ret(it == sib.begin() ? 0 : *(it - 1));
    }
    case 4: return Ret(w->owner);                        // GW_OWNER
    case 5: return Ret(w->children.empty() ? 0 : w->children.front()); // GW_CHILD
    case 6: { // GW_ENABLEDPOPUP
        for (auto& kv : S().windows)
            if (kv.second->owner == w->hwnd && (kv.second->style & WS_VISIBLE) && !(kv.second->style & WS_DISABLED)) return Ret(kv.first);
        return Ret(0);
    }
    }
    return Ret(0);
}
FN(GetTopWindow) {
    ENTER;
    Window* w = A(c, m, 0) ? Win(A(c, m, 0)) : Desktop();
    return Ret(w && !w->children.empty() ? w->children.front() : 0);
}
FN(GetDlgItem) {
    ENTER;
    Window* w = Win(A(c, m, 0));
    if (!w) return Ret(0);
    uint32_t id = A(c, m, 1);
    for (uint32_t ch : w->children) if (Window* x = Win(ch)) if ((x->id & 0xFFFF) == (id & 0xFFFF)) return Ret(ch);
    return Ret(0);
}
FN(GetDlgCtrlID) { ENTER; Window* w = Win(A(c, m, 0)); return Ret(w ? w->id : 0); }
FN(IsWindow) { ENTER; Window* w = Win(A(c, m, 0)); return Ret(w && !w->destroying ? 1 : 0); }
FN(IsWindowVisible) { ENTER; return Ret(IsVisibleChain(Win(A(c, m, 0))) ? 1 : 0); }
FN(IsWindowEnabled) { ENTER; Window* w = Win(A(c, m, 0)); return Ret(w && !(w->style & WS_DISABLED) ? 1 : 0); }
FN(IsWindowUnicode) { ENTER; Window* w = Win(A(c, m, 0)); return Ret(w && w->unicode ? 1 : 0); }
FN(EnableWindow) {
    ENTER;
    uint32_t hwnd = A(c, m, 0);
    Window* w = Win(hwnd);
    if (!w) return Ret(0);
    bool wasDisabled = (w->style & WS_DISABLED) != 0;
    bool enable = A(c, m, 1) != 0;
    if (enable == !wasDisabled) return Ret(wasDisabled ? 1 : 0);
    if (enable) w->style &= ~WS_DISABLED; else w->style |= WS_DISABLED;
    if (!enable) ClearWindowFocus(hwnd);
    if (!enable) ClearWindowCapture(hwnd);
    SendMsg(hwnd, WM_ENABLE, enable ? 1 : 0, 0);
    if ((w = Win(hwnd)) && (w->style & WS_VISIBLE)) {
        w->ncDirty = true;
        InvalidateClient(w, Region(Rect(0, 0, w->client.Width(), w->client.Height())), true, false);
    }
    return Ret(wasDisabled ? 1 : 0);
}
FN(IsChild) {
    ENTER;
    uint32_t parent = A(c, m, 0);
    for (Window* w = Win(A(c, m, 1)); w && (w->style & WS_CHILD); w = Win(w->parent))
        if (w->parent == parent) return Ret(1);
    return Ret(0);
}
FN(IsIconic) { ENTER; Window* w = Win(A(c, m, 0)); return Ret(w && (w->style & WS_MINIMIZE) ? 1 : 0); }
FN(IsZoomed) { ENTER; Window* w = Win(A(c, m, 0)); return Ret(w && (w->style & WS_MAXIMIZE) ? 1 : 0); }
FN(GetFocus) { ENTER; return Ret(Q().focus); }
FN(SetFocus) { ENTER; return Ret(SetFocusInternal(A(c,m,0))); }
FN(GetActiveWindow) { ENTER; return Ret(Q().active); }
FN(SetActiveWindow) {
    ENTER;
    const uint32_t hwnd=A(c,m,0),old=Q().active;
    const auto* window=Win(hwnd);
    if(hwnd && (!window || window->destroying)) {SetLastError(m,1400);return Ret(0);}
    if(window && window->threadId!=CurrentGuiThread()) {SetLastError(m,5);return Ret(0);}
    if(window && (window->style&WS_CHILD)) {SetLastError(m,87);return Ret(0);}
    SetActive(hwnd,false);return Ret(old);
}
FN(GetForegroundWindow) { ENTER; return Ret(S().foreground); }
FN(SetForegroundWindow) {
    ENTER;const uint32_t hwnd=A(c,m,0);
    const auto* window=Win(hwnd);
    if(!window || window->destroying) {SetLastError(m,1400);return Ret(0);}
    if(window->style&WS_CHILD) {SetLastError(m,87);return Ret(0);}
    SetActive(hwnd);return Ret(S().foreground==hwnd?1:0);
}
FN(GetCapture) { ENTER; return Ret(CaptureForThread(CurrentGuiThread())); }
FN(SetCapture) { ENTER;return Ret(SetCaptureInternal(A(c,m,0))); }
FN(ReleaseCapture) { ENTER;return Ret(ReleaseCaptureInternal()?1:0); }
FN(GetGUIThreadInfo) {
    ENTER;uint32_t id=A(c,m,0),p=A(c,m,1);
    if(!p || !m.IsRangeMapped(p,48)) {SetLastError(m,998);return Ret(0);}
    if(m.Read32(p)!=48) {SetLastError(m,87);return Ret(0);}
    if(!id) {const auto* foreground=Win(S().foreground);id=foreground?foreground->threadId:0;}
    const auto queue=S().queues.find(id);
    if(queue==S().queues.end() || queue->second.closed ||
       (rt::g_process && (!rt::g_process->FindThread(id) || rt::g_process->FindThread(id)->status==rt::GuestThreadStatus::Exited))) {
        SetLastError(m,1444);return Ret(0);
    }
    for(unsigned offset=4;offset<48;offset+=4)m.Write32(p+offset,0);
    m.Write32(p+8,queue->second.active);m.Write32(p+12,queue->second.focus);
    m.Write32(p+16,CaptureForThread(id));return Ret(1);
}
// Shared input queues are a separate contract; never report a fake attachment.
FN(AttachThreadInput) { ENTER;SetLastError(m,120);return Ret(0); }
FN(WindowFromPoint) { ENTER; Window* w = user::WindowFromPoint(SA(c, m, 0), SA(c, m, 1)); return Ret(w ? w->hwnd : 0); }
FN(ChildWindowFromPoint) {
    ENTER;
    Window* w = Win(A(c, m, 0));
    if (!w) return Ret(0);
    int x = SA(c, m, 1) + ClientOriginX(w), y = SA(c, m, 2) + ClientOriginY(w);
    if (!ScreenRect(w).Contains(x, y)) return Ret(0);
    for (uint32_t ch : w->children) if (Window* x2 = Win(ch)) if (ScreenRect(x2).Contains(x, y)) return Ret(ch);
    return Ret(w->hwnd);
}
FN(ChildWindowFromPointEx) {
    ENTER;
    Window* w = Win(A(c, m, 0));
    if (!w) return Ret(0);
    uint32_t flags = A(c, m, 3);
    int x = SA(c, m, 1) + ClientOriginX(w), y = SA(c, m, 2) + ClientOriginY(w);
    if (!ScreenRect(w).Contains(x, y)) return Ret(0);
    for (uint32_t ch : w->children) {
        Window* x2 = Win(ch);
        if (!x2 || !ScreenRect(x2).Contains(x, y)) continue;
        if ((flags & 1) && !(x2->style & WS_VISIBLE)) continue;
        if ((flags & 2) && (x2->style & WS_DISABLED)) continue;
        return Ret(ch);
    }
    return Ret(w->hwnd);
}

HostCallOutcome FindWindowCommon(cpu::CpuState& c, cpu::IGuestMemory& m, bool unicode, bool ex) {
    int o = ex ? 2 : 0;
    std::u16string cls, title;
    uint16_t atom = 0;
    bool anyClass = !A(c, m, o), anyTitle = !A(c, m, o + 1);
    if (!anyClass) ReadNameOrAtom(A(c, m, o), unicode, cls, atom);
    if (!anyTitle) title = ReadText(A(c, m, o + 1), unicode);
    Window* parent = ex && A(c, m, 0) ? Win(A(c, m, 0)) : Desktop();
    if (!parent) return Ret(0);
    bool started = !ex || !A(c, m, 1);
    for (uint32_t h : parent->children) {
        if (!started) { if (h == A(c, m, 1)) started = true; continue; }
        Window* w = Win(h);
        if (!w || !w->cls) continue;
        if (!anyClass && !(atom ? w->cls->atom == atom : (w->cls->name.size() == cls.size() && [&] {
                for (size_t i = 0; i < cls.size(); ++i) if (std::tolower(cls[i]) != std::tolower(w->cls->name[i])) return false;
                return true; }())))
            continue;
        if (!anyTitle && w->text != title) continue;
        return Ret(h);
    }
    return Ret(0);
}
HostCallOutcome FindWindowImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool u) { return FindWindowCommon(c, m, u, false); }
HostCallOutcome FindWindowExImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool u) { return FindWindowCommon(c, m, u, true); }
AW(FindWindow)
AW(FindWindowEx)

FN(EnumWindows) {
    ENTER;
    uint32_t proc = A(c, m, 0), lp = A(c, m, 1);
    std::vector<uint32_t> list = Desktop()->children;
    for (uint32_t h : list) {
        if (!Win(h)) continue;
        if (!CallStdcall(c, m, proc, {h, lp})) break;
    }
    return Ret(1);
}
FN(EnumChildWindows) {
    ENTER;
    Window* w = Win(A(c, m, 0));
    if (!w) w = Desktop();
    uint32_t proc = A(c, m, 1), lp = A(c, m, 2);
    std::vector<uint32_t> list;
    std::vector<uint32_t> stack(w->children.rbegin(), w->children.rend());
    while (!stack.empty()) {
        uint32_t h = stack.back();
        stack.pop_back();
        list.push_back(h);
        if (Window* x = Win(h)) for (auto it = x->children.rbegin(); it != x->children.rend(); ++it) stack.push_back(*it);
    }
    for (uint32_t h : list) {
        if (!Win(h)) continue;
        if (!CallStdcall(c, m, proc, {h, lp})) break;
    }
    return Ret(1);
}
FN(GetWindowThreadProcessId) {
    ENTER;
    if (A(c, m, 1)) m.Write32(A(c, m, 1), 0x1000);
    const auto* window=Win(A(c,m,0));
    return Ret(window ? window->threadId : 0);
}

std::u16string PropName(cpu::IGuestMemory& m, uint32_t p, bool unicode) {
    if (p < 0x10000) return u"#atom" + std::u16string(1, static_cast<char16_t>(p));
    (void)m;
    return ReadText(p, unicode);
}
HostCallOutcome SetPropImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool u) {
    Window* w = Win(A(c, m, 0));
    if (!w) return Ret(0);
    w->props[PropName(m, A(c, m, 1), u)] = A(c, m, 2);
    return Ret(1);
}
HostCallOutcome GetPropImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool u) {
    Window* w = Win(A(c, m, 0));
    if (!w) return Ret(0);
    auto it = w->props.find(PropName(m, A(c, m, 1), u));
    return Ret(it == w->props.end() ? 0 : it->second);
}
HostCallOutcome RemovePropImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool u) {
    Window* w = Win(A(c, m, 0));
    if (!w) return Ret(0);
    auto it = w->props.find(PropName(m, A(c, m, 1), u));
    if (it == w->props.end()) return Ret(0);
    uint32_t v = it->second;
    w->props.erase(it);
    return Ret(v);
}
AW(SetProp)
AW(GetProp)
AW(RemoveProp)

FN(GetWindowPlacement) {
    ENTER;
    Window* w = Win(A(c, m, 0));
    uint32_t p = A(c, m, 1);
    if (!w || !p) return Ret(0);
    m.Write32(p, 44);
    m.Write32(p + 4, 0);
    m.Write32(p + 8, (w->style & WS_MAXIMIZE) ? 3 : (w->style & WS_MINIMIZE) ? 2 : 1);
    m.Write32(p + 12, 0xFFFFFFFFu);
    m.Write32(p + 16, 0xFFFFFFFFu);
    m.Write32(p + 20, 0xFFFFFFFFu);
    m.Write32(p + 24, 0xFFFFFFFFu);
    WriteRect(m, p + 28, (w->hasNormalRect && (w->style&(WS_MINIMIZE|WS_MAXIMIZE)))?w->normalRect:w->rect);
    return Ret(1);
}
FN(SetWindowPlacement) {
    ENTER;
    uint32_t hwnd = A(c, m, 0), p = A(c, m, 1);
    if (!Win(hwnd) || !p) return Ret(0);
    Rect r = ReadRect(m, p + 28);
    // Как в Windows: окно, которое оказалось бы вне экрана, возвращается на
    // экран (Paint без сохранённых настроек передаёт координаты CW_USEDEFAULT)
    constexpr int kDefault = static_cast<int>(0x80000000u);
    Rect screen(0, 0, S().screenW, S().screenH);
    if (r.left == kDefault || r.top == kDefault || r.Width() <= 0 || r.Height() <= 0)
        r = Rect(24, 24, 24 + S().screenW * 3 / 4, 24 + S().screenH * 3 / 4);
    if (r.right <= 0 || r.bottom <= 0 || r.left >= screen.right || r.top >= screen.bottom) {
        int w = std::min(r.Width(), S().screenW), h = std::min(r.Height(), S().screenH);
        r = Rect(0, 0, w, h);
    }
    SetWindowPosInternal(hwnd, 0, r.left, r.top, r.Width(), r.Height(), 0x4 | 0x10);
    ShowWindowInternal(hwnd, static_cast<int>(m.Read32(p + 8)));
    return Ret(1);
}

// --- Сообщения ---

HostCallOutcome DefWindowProcImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool unicode) {
    return Ret(DefWindowProc(A(c, m, 0), A(c, m, 1), A(c, m, 2), A(c, m, 3), unicode));
}
AW(DefWindowProc)

HostCallOutcome CallWindowProcImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool unicode) {
    uint32_t proc = A(c, m, 0);
    // процедура могла быть получена у окна другой кодировки; считаем её
    // кодировкой ту, что у окна, если адрес совпадает
    bool procUnicode = unicode;
    if (Window* w = Win(A(c, m, 1))) if (w->wndProc == proc) procUnicode = w->unicode;
    return Ret(CallProc(proc, procUnicode, A(c, m, 1), A(c, m, 2), A(c, m, 3), A(c, m, 4), unicode));
}
AW(CallWindowProc)

HostCallOutcome SendMessageImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool unicode) {
    uint32_t hwnd = A(c, m, 0);
    if (hwnd == 0xFFFF) { // HWND_BROADCAST
        std::vector<uint32_t> list = Desktop()->children;
        for (uint32_t h : list) SendMsg(h, A(c, m, 1), A(c, m, 2), A(c, m, 3), unicode);
        return Ret(1);
    }
    return Ret(SendMsg(hwnd, A(c, m, 1), A(c, m, 2), A(c, m, 3), unicode));
}
AW(SendMessage)
HostCallOutcome SendMessageTimeoutImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool unicode) {
    uint32_t result=0;
    const uint32_t flags=A(c,m,4), output=A(c,m,6);
    if ((flags & ~3u) || (output && !m.IsRangeMapped(output,4))) { SetLastError(m,87); return Ret(0); }
    bool success=SendMsgTimeout(A(c,m,0),A(c,m,1),A(c,m,2),A(c,m,3),unicode,A(c,m,5),flags,result);
    if (output) m.Write32(output,result);
    return Ret(success ? 1 : 0);
}
AW(SendMessageTimeout)
HostCallOutcome SendNotifyMessageImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool unicode) {
    uint32_t result=0;
    return Ret(SendMsgTimeout(A(c,m,0),A(c,m,1),A(c,m,2),A(c,m,3),unicode,0xffffffffu,0,result,true) ? 1 : 0);
}
AW(SendNotifyMessage)

HostCallOutcome PostMessageImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool) {
    uint32_t hwnd = A(c, m, 0);
    if (hwnd && hwnd != 0xFFFF && !Win(hwnd)) { SetLastError(m, 1400); return Ret(0); }
    if (hwnd == 0xFFFF) {
        for (uint32_t h : Desktop()->children) PostMsg(h, A(c, m, 1), A(c, m, 2), A(c, m, 3));
        return Ret(1);
    }
    PostMsg(hwnd, A(c, m, 1), A(c, m, 2), A(c, m, 3));
    return Ret(1);
}
AW(PostMessage)
HostCallOutcome PostThreadMessageImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool) {
    if (!PostToThread(A(c,m,0),0,A(c,m,1),A(c,m,2),A(c,m,3))) {
        SetLastError(m,1444); return Ret(0);
    }
    return Ret(1);
}
AW(PostThreadMessage)
FN(PostQuitMessage) { ENTER; PostQuit(A(c,m,0)); return Ret(0); }

void WriteMsg(cpu::IGuestMemory& m, uint32_t p, const Msg& msg) {
    m.Write32(p, msg.hwnd);
    m.Write32(p + 4, msg.message);
    m.Write32(p + 8, msg.wParam);
    m.Write32(p + 12, msg.lParam);
    m.Write32(p + 16, msg.time);
    m.Write32(p + 20, static_cast<uint32_t>(msg.x));
    m.Write32(p + 24, static_cast<uint32_t>(msg.y));
}
Msg ReadMsg(cpu::IGuestMemory& m, uint32_t p) {
    Msg msg;
    msg.hwnd = m.Read32(p);
    msg.message = m.Read32(p + 4);
    msg.wParam = m.Read32(p + 8);
    msg.lParam = m.Read32(p + 12);
    msg.time = m.Read32(p + 16);
    msg.x = static_cast<int32_t>(m.Read32(p + 20));
    msg.y = static_cast<int32_t>(m.Read32(p + 24));
    return msg;
}



HostCallOutcome GetMessageImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool) {
    const uint32_t filter=A(c,m,1);
    if (filter && filter!=0xffffffffu && (!Win(filter) || Win(filter)->threadId!=CurrentGuiThread())) {
        SetLastError(m,1400); return Ret(0xffffffffu);
    }
    Msg msg;
    FetchMessage(msg, A(c, m, 1), A(c, m, 2), A(c, m, 3), true, true);
    if (g_guestExitRequested) return Ret(0);
    WriteMsg(m, A(c, m, 0), msg);
    Q().lastMsg = msg;
    return Ret(msg.message == WM_QUIT ? 0 : 1);
}
AW(GetMessage)
HostCallOutcome PeekMessageImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool) {
    const uint32_t filter=A(c,m,1);
    if (filter && filter!=0xffffffffu && (!Win(filter) || Win(filter)->threadId!=CurrentGuiThread())) {
        SetLastError(m,1400); return Ret(0);
    }
    Msg msg;
    bool remove = (A(c, m, 4) & 1) != 0;
    if (!FetchMessage(msg, A(c, m, 1), A(c, m, 2), A(c, m, 3), remove, false)) return Ret(0);
    WriteMsg(m, A(c, m, 0), msg);
    if (remove) Q().lastMsg = msg;
    return Ret(1);
}
AW(PeekMessage)
FN(WaitMessage) {
    ENTER;
    return Ret(WaitForMessages(c,m,0,0,0xffffffffu,0x4ff,0)!=0xffffffffu ? 1 : 0);
}
FN(MsgWaitForMultipleObjects) {
    ENTER;
    return Ret(WaitForMessages(c,m,A(c,m,0),A(c,m,1),A(c,m,3),A(c,m,4),A(c,m,2) ? 1u : 0u));
}
FN(MsgWaitForMultipleObjectsEx) {
    ENTER;
    return Ret(WaitForMessages(c,m,A(c,m,0),A(c,m,1),A(c,m,2),A(c,m,3),A(c,m,4)));
}
FN(TranslateMessage) { ENTER; return Ret(TranslateKey(ReadMsg(m, A(c, m, 0))) ? 1 : 0); }
HostCallOutcome DispatchMessageImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool unicode) {
    Msg msg = ReadMsg(m, A(c, m, 0));
    if (msg.message == WM_TIMER && msg.lParam) {
        return Ret(CallStdcall(c, m, msg.lParam, {msg.hwnd, WM_TIMER, msg.wParam, NowMs()}));
    }
    if (!msg.hwnd) return Ret(0);
    uint32_t r = SendMsg(msg.hwnd, msg.message, msg.wParam, msg.lParam, unicode);
    return Ret(r);
}
AW(DispatchMessage)
FN(GetMessagePos) { ENTER; return Ret((static_cast<uint32_t>(Q().lastMsg.x) & 0xFFFF) | (static_cast<uint32_t>(Q().lastMsg.y) << 16)); }
FN(GetMessageTime) { ENTER; return Ret(Q().lastMsg.time); }
FN(GetQueueStatus) {
    ENTER;
    const uint32_t mask=A(c,m,0);
    const uint32_t available=QueueStatusBits() & mask;
    const uint32_t changed=Q().changed & mask;
    Q().changed&=~mask;
    return Ret((available<<16) | changed);
}
FN(GetInputState) { ENTER; return Ret(Q().input.empty() ? 0 : 1); }
FN(InSendMessage) { ENTER; return Ret(SendMessageState() ? 1 : 0); }
FN(InSendMessageEx) { ENTER; return Ret(SendMessageState()); }
FN(ReplyMessage) { ENTER; return Ret(ReplyToSender(A(c,m,0)) ? 1 : 0); }
HostCallOutcome RegisterWindowMessageImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool unicode) {
    return Ret(RegisterMessage(ReadText(A(c,m,0),unicode)));
}
AW(RegisterWindowMessage)

// --- Таймеры ---

FN(SetTimer) {
    ENTER;
    uint32_t hwnd = A(c, m, 0), id = A(c, m, 1), elapse = A(c, m, 2), proc = A(c, m, 3);
    if (hwnd && !Win(hwnd)) return Ret(0);
    auto& queue=hwnd ? QueueFor(Win(hwnd)->threadId) : Q();
    if (!hwnd) id = queue.nextTimerId--;
    if (elapse < 10) elapse = 10;
    for (Timer& t : queue.timers)
        if (t.hwnd == hwnd && t.id == id) { t.interval = elapse; t.proc = proc; t.due = NowMs() + elapse; t.announced=false; return Ret(hwnd ? 1 : id); }
    Timer t;
    t.hwnd = hwnd;
    t.id = id;
    t.interval = elapse;
    t.proc = proc;
    t.due = NowMs() + elapse;
    queue.timers.push_back(t);
    return Ret(hwnd ? (id ? id : 1) : id);
}
FN(KillTimer) {
    ENTER;
    uint32_t hwnd = A(c, m, 0), id = A(c, m, 1);
    if (hwnd && !Win(hwnd)) return Ret(0);
    auto& queue=hwnd ? QueueFor(Win(hwnd)->threadId) : Q();
    auto& v = queue.timers;
    size_t before = v.size();
    v.erase(std::remove_if(v.begin(), v.end(), [&](const Timer& t) { return t.hwnd == hwnd && t.id == id; }), v.end());
    for (auto it = queue.posted.begin(); it != queue.posted.end();)
        if (it->message == WM_TIMER && it->hwnd == hwnd && it->wParam == id) it = queue.posted.erase(it); else ++it;
    return Ret(v.size() != before ? 1 : 0);
}

// --- Рисование окон ---

FN(BeginPaint) {
    ENTER;
    if(!ui::g_gdi.PrepareCpuAccess())return Ret(0);
    uint32_t hwnd = A(c, m, 0), ps = A(c, m, 1);
    Window* w = Win(hwnd);
    if (!w || !ps) return Ret(0);
    if (w->ncDirty) { SendMsg(hwnd, WM_NCPAINT, 1, 0); if (!(w = Win(hwnd))) return Ret(0); w->ncDirty = false; }
    Region upd = w->update;
    bool erase = w->erase;
    w->update.Clear(); w->paintAnnounced=false;
    w->erase = false;
    w->internalPaint = false;
    uint32_t hdc = ui::g_gdi.CreateDc(ui::DcKind::Window, hwnd, false);
    ui::DcObj* dc = ui::g_gdi.Dc(hdc);
    Rect bounds = upd.Bounds();
    Region clip = upd;
    clip.Offset(ClientOriginX(w), ClientOriginY(w));
    dc->paintDc = true;
    dc->paintClip = clip;
    uint32_t erased = 1;
    if (erase) erased = SendMsg(hwnd, WM_ERASEBKGND, hdc, 0) ? 1 : 0;
    m.Write32(ps, hdc);
    m.Write32(ps + 4, erased ? 0 : 1);
    WriteRect(m, ps + 8, bounds);
    m.Write32(ps + 24, 0);
    m.Write32(ps + 28, 0);
    return Ret(hdc);
}
FN(EndPaint) { ENTER; ui::g_gdi.DeleteDc(m.Read32(A(c, m, 1))); return Ret(1); }
FN(GetDC) {
    ENTER;
    uint32_t hwnd = A(c, m, 0);
    if (!hwnd || hwnd == S().desktop) return Ret(ui::g_gdi.CreateDc(ui::DcKind::Screen));
    if (!Win(hwnd)) return Ret(0);
    return Ret(ui::g_gdi.CreateDc(ui::DcKind::Window, hwnd, false));
}
FN(GetDCEx) {
    ENTER;
    uint32_t hwnd = A(c, m, 0), flags = A(c, m, 2);
    if (!hwnd || hwnd == S().desktop) return Ret(ui::g_gdi.CreateDc(ui::DcKind::Screen));
    if (!Win(hwnd)) return Ret(0);
    uint32_t hdc = ui::g_gdi.CreateDc(ui::DcKind::Window, hwnd, (flags & 1) != 0); // DCX_WINDOW
    if (ui::RegionObj* r = ui::g_gdi.Rgn(A(c, m, 1))) {
        ui::DcObj* dc = ui::g_gdi.Dc(hdc);
        Region clip = r->rgn;
        if (flags & 0x80) { // DCX_INTERSECTRGN
            dc->st.hasClip = true;
            dc->st.clip = clip;
        }
    }
    return Ret(hdc);
}
FN(GetWindowDC) {
    ENTER;
    uint32_t hwnd = A(c, m, 0);
    if (!hwnd || hwnd == S().desktop) return Ret(ui::g_gdi.CreateDc(ui::DcKind::Screen));
    if (!Win(hwnd)) return Ret(0);
    return Ret(ui::g_gdi.CreateDc(ui::DcKind::Window, hwnd, true));
}
FN(ReleaseDC) { ENTER; return Ret(ui::g_gdi.DeleteDc(A(c, m, 1)) ? 1 : 0); }

FN(InvalidateRect) {
    ENTER;
    uint32_t hwnd = A(c, m, 0), rp = A(c, m, 1);
    Window* w = hwnd ? Win(hwnd) : nullptr;
    if (!hwnd) { InvalidateScreen(Rect(0, 0, S().screenW, S().screenH)); return Ret(1); }
    if (!w) return Ret(0);
    Rect r = rp ? ReadRect(m, rp) : Rect(0, 0, w->client.Width(), w->client.Height());
    InvalidateClient(w, Region(r), A(c, m, 2) != 0, false);
    return Ret(1);
}
FN(InvalidateRgn) {
    ENTER;
    Window* w = Win(A(c, m, 0));
    if (!w) return Ret(0);
    ui::RegionObj* r = ui::g_gdi.Rgn(A(c, m, 1));
    InvalidateClient(w, r ? r->rgn : Region(Rect(0, 0, w->client.Width(), w->client.Height())), A(c, m, 2) != 0, false);
    return Ret(1);
}
FN(ValidateRect) {
    ENTER;
    Window* w = Win(A(c, m, 0));
    if (!w) return Ret(0);
    if (A(c, m, 1)) w->update.SubtractRect(ReadRect(m, A(c, m, 1)));
    else { w->update.Clear(); w->paintAnnounced=false; w->erase = false; }
    return Ret(1);
}
FN(ValidateRgn) {
    ENTER;
    Window* w = Win(A(c, m, 0));
    if (!w) return Ret(0);
    if (ui::RegionObj* r = ui::g_gdi.Rgn(A(c, m, 1))) w->update.Subtract(r->rgn);
    else { w->update.Clear(); w->paintAnnounced=false; w->erase = false; }
    return Ret(1);
}
void UpdateNow(uint32_t hwnd, bool children) {
    UpdateWindowNow(hwnd,children);
}
FN(UpdateWindow) { ENTER; UpdateNow(A(c, m, 0), false); return Ret(Win(A(c, m, 0)) ? 1 : 0); }
FN(RedrawWindow) {
    ENTER;
    uint32_t hwnd = A(c, m, 0), rp = A(c, m, 1), rgn = A(c, m, 2), flags = A(c, m, 3);
    Window* w = hwnd ? Win(hwnd) : Desktop();
    if (!w) return Ret(0);
    Region r;
    if (rp) r = Region(ReadRect(m, rp));
    else if (ui::RegionObj* ro = ui::g_gdi.Rgn(rgn)) r = ro->rgn;
    else r = Region(Rect(0, 0, w->client.Width(), w->client.Height()));
    if (w == Desktop()) {
        if (flags & 0x1) InvalidateScreen(Rect(0, 0, S().screenW, S().screenH));
        return Ret(1);
    }
    if (flags & 0x1) { // RDW_INVALIDATE
        InvalidateClient(w, r, (flags & 0x4) != 0, (flags & 0x80) != 0);
        if (flags & 0x400) w->ncDirty = true; // RDW_FRAME
    }
    if (flags & 0x2) { w->internalPaint = true; w->paintAnnounced=false; } // RDW_INTERNALPAINT
    if (flags & 0x8) { w->update.Subtract(r); if (flags & 0x10) w->erase = false; } // RDW_VALIDATE
    if (flags & 0x100) UpdateNow(hwnd, (flags & 0x80) != 0); // RDW_UPDATENOW
    else if (flags & 0x200) { // RDW_ERASENOW
        if (w->erase) {
            uint32_t hdc = ui::g_gdi.CreateDc(ui::DcKind::Window, hwnd, false);
            SendMsg(hwnd, WM_ERASEBKGND, hdc, 0);
            ui::g_gdi.DeleteDc(hdc);
            if ((w = Win(hwnd))) w->erase = false;
        }
    }
    return Ret(1);
}
FN(GetUpdateRect) {
    ENTER;
    Window* w = Win(A(c, m, 0));
    if (!w) return Ret(0);
    Rect b = w->update.Bounds();
    if (A(c, m, 1)) WriteRect(m, A(c, m, 1), b);
    if (A(c, m, 2) && w->erase && !b.Empty()) {
        uint32_t hdc = ui::g_gdi.CreateDc(ui::DcKind::Window, w->hwnd, false);
        SendMsg(w->hwnd, WM_ERASEBKGND, hdc, 0);
        ui::g_gdi.DeleteDc(hdc);
        if ((w = Win(A(c, m, 0)))) w->erase = false;
    }
    return Ret(b.Empty() ? 0 : 1);
}
FN(GetUpdateRgn) {
    ENTER;
    Window* w = Win(A(c, m, 0));
    ui::RegionObj* r = ui::g_gdi.Rgn(A(c, m, 1));
    if (!w || !r) return Ret(0);
    r->rgn = w->update;
    return Ret(w->update.Empty() ? 1 : w->update.Rects().size() == 1 ? 2 : 3);
}
// Прокрутка: сдвиг содержимого клиентской области и перерисовка открывшейся полосы
HostCallOutcome ScrollCommon(cpu::CpuState& c, cpu::IGuestMemory& m, uint32_t hwnd, int dx, int dy, uint32_t rectP, uint32_t clipP,
                             uint32_t updRgn, uint32_t updRectP, bool children) {
    if(!ui::g_gdi.PrepareCpuAccess())return Ret(0);
    Window* w = Win(hwnd);
    if (!w) return Ret(0);
    Rect area = rectP ? ReadRect(m, rectP) : Rect(0, 0, w->client.Width(), w->client.Height());
    if (clipP) area = area.Intersect(ReadRect(m, clipP));
    uint32_t hdc = ui::g_gdi.CreateDc(ui::DcKind::Window, hwnd, false);
    Rect src = area.Offset(-dx, -dy).Intersect(area);
    ui::g_gdi.BitBlt(hdc, src.left + dx, src.top + dy, src.Width(), src.Height(), hdc, src.left, src.top, 0xCC0020);
    ui::g_gdi.DeleteDc(hdc);
    Region exposed(area);
    exposed.SubtractRect(src.Offset(dx, dy));
    InvalidateClient(w, exposed, true, false);
    if (children || !rectP) {
        for (uint32_t ch : w->children) {
            Window* x = Win(ch);
            if (!x) continue;
            x->rect = x->rect.Offset(dx, dy);
        }
        ui::g_gdi.InvalidateLayout();
        if (!w->children.empty()) InvalidateClient(w, Region(area), true, true);
    }
    if (ui::RegionObj* r = ui::g_gdi.Rgn(updRgn)) r->rgn = exposed;
    if (updRectP) WriteRect(m, updRectP, exposed.Bounds());
    return Ret(1);
}
FN(ScrollWindow) { ENTER; return ScrollCommon(c, m, A(c, m, 0), SA(c, m, 1), SA(c, m, 2), A(c, m, 3), A(c, m, 4), 0, 0, false); }
FN(ScrollWindowEx) {
    ENTER;
    HostCallOutcome r = ScrollCommon(c, m, A(c, m, 0), SA(c, m, 1), SA(c, m, 2), A(c, m, 3), A(c, m, 4), A(c, m, 5), A(c, m, 6),
                                     (A(c, m, 7) & 1) != 0);
    r.returnValue = 2; // SIMPLEREGION
    return r;
}
FN(LockWindowUpdate) { ENTER; return Ret(1); }

// --- Рисование USER32 ---

uint32_t BrushArg(uint32_t b) { return (b && b <= 31) ? SysColorBrush(static_cast<int>(b) - 1) : b; }
FN(FillRect) { ENTER; return Ret(ui::g_gdi.FillRect(A(c, m, 0), ReadRect(m, A(c, m, 1)), BrushArg(A(c, m, 2))) ? 1 : 0); }
FN(FrameRect) { ENTER; return Ret(ui::g_gdi.FrameRect(A(c, m, 0), ReadRect(m, A(c, m, 1)), BrushArg(A(c, m, 2))) ? 1 : 0); }
FN(InvertRect) { ENTER; return Ret(ui::g_gdi.InvertRect(A(c, m, 0), ReadRect(m, A(c, m, 1))) ? 1 : 0); }
HostCallOutcome DrawTextImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool unicode) {
    uint32_t hdc = A(c, m, 0), str = A(c, m, 1), rp = A(c, m, 3), fmt = A(c, m, 4);
    int len = SA(c, m, 2);
    std::u16string text;
    if (len < 0) text = ReadText(str, unicode);
    else for (int i = 0; i < len; ++i) text.push_back(unicode ? static_cast<char16_t>(m.Read16(str + i * 2)) : static_cast<char16_t>(m.Read8(str + i)));
    Rect r = ReadRect(m, rp);
    int h = ui::g_gdi.DrawText(hdc, text, r, fmt);
    if (fmt & 0x400) WriteRect(m, rp, r);
    return Ret(static_cast<uint32_t>(h));
}
AW(DrawText)
HostCallOutcome DrawTextExImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool unicode) { return DrawTextImpl(c, m, unicode); }
AW(DrawTextEx)

void EdgeLine(uint32_t hdc, int x0, int y0, int x1, int y1, int sysColor) {
    uint32_t b = SysColorBrush(sysColor);
    ui::g_gdi.FillRect(hdc, Rect(std::min(x0, x1), std::min(y0, y1), std::max(x0, x1) + (x0 == x1 ? 1 : 0), std::max(y0, y1) + (y0 == y1 ? 1 : 0)), b);
}
// DrawEdge: рамка из двух слоёв (внешний/внутренний, выпуклый/утопленный)
FN(DrawEdge) {
    ENTER;
    uint32_t hdc = A(c, m, 0), rp = A(c, m, 1), edge = A(c, m, 2), flags = A(c, m, 3);
    Rect r = ReadRect(m, rp);
    auto layer = [&](bool raised, bool outer) {
        int tl, br;
        if (outer) { tl = raised ? 22 : 16; br = raised ? 21 : 20; }
        else { tl = raised ? 20 : 21; br = raised ? 16 : 22; }
        if (flags & 0x8000) { tl = br = 16; } // BF_FLAT
        if (flags & 0x2) EdgeLine(hdc, r.left, r.top, r.right - 1, r.top, tl);        // BF_TOP
        if (flags & 0x1) EdgeLine(hdc, r.left, r.top, r.left, r.bottom - 1, tl);      // BF_LEFT
        if (flags & 0x8) EdgeLine(hdc, r.left, r.bottom - 1, r.right - 1, r.bottom - 1, br); // BF_BOTTOM
        if (flags & 0x4) EdgeLine(hdc, r.right - 1, r.top, r.right - 1, r.bottom - 1, br);  // BF_RIGHT
        if (flags & 0x1) r.left++;
        if (flags & 0x2) r.top++;
        if (flags & 0x4) r.right--;
        if (flags & 0x8) r.bottom--;
    };
    if (edge & 0x3) layer((edge & 0x1) != 0, true);   // BDR_RAISEDOUTER 1 / SUNKENOUTER 2
    if (edge & 0xC) layer((edge & 0x4) != 0, false);  // BDR_RAISEDINNER 4 / SUNKENINNER 8
    if (flags & 0x800) ui::g_gdi.FillRect(hdc, r, SysColorBrush(15)); // BF_MIDDLE
    if (flags & 0x2000) WriteRect(m, rp, r);                          // BF_ADJUST
    return Ret(1);
}
FN(DrawFocusRect) {
    ENTER;
    uint32_t hdc = A(c, m, 0);
    Rect r = ReadRect(m, A(c, m, 1));
    ui::DcObj* dc = ui::g_gdi.Dc(hdc);
    if (!dc) return Ret(0);
    // точечная рамка XOR
    for (int x = r.left; x < r.right; x += 2) {
        ui::g_gdi.PatBlt(hdc, x, r.top, 1, 1, 0x550009);
        ui::g_gdi.PatBlt(hdc, x, r.bottom - 1, 1, 1, 0x550009);
    }
    for (int y = r.top + 2; y < r.bottom - 1; y += 2) {
        ui::g_gdi.PatBlt(hdc, r.left, y, 1, 1, 0x550009);
        ui::g_gdi.PatBlt(hdc, r.right - 1, y, 1, 1, 0x550009);
    }
    return Ret(1);
}
FN(DrawFrameControl) {
    ENTER;
    uint32_t hdc = A(c, m, 0), type = A(c, m, 2), state = A(c, m, 3);
    Rect r = ReadRect(m, A(c, m, 1));
    ui::g_gdi.FillRect(hdc, r, SysColorBrush(15));
    bool pushed = (state & 0x200) != 0; // DFCS_PUSHED
    int tl0 = pushed ? 16 : 22, br0 = pushed ? 20 : 21, tl1 = pushed ? 21 : 20, br1 = pushed ? 22 : 16;
    if (type == 4 /* DFC_BUTTON */ && (state & 0xF) == 0 /* DFCS_BUTTONCHECK */) { tl0 = 16; br0 = 20; tl1 = 21; br1 = 22; ui::g_gdi.FillRect(hdc, Rect(r.left + 2, r.top + 2, r.right - 2, r.bottom - 2), SysColorBrush(5)); }
    EdgeLine(hdc, r.left, r.top, r.right - 1, r.top, tl0);
    EdgeLine(hdc, r.left, r.top, r.left, r.bottom - 1, tl0);
    EdgeLine(hdc, r.left, r.bottom - 1, r.right - 1, r.bottom - 1, br0);
    EdgeLine(hdc, r.right - 1, r.top, r.right - 1, r.bottom - 1, br0);
    EdgeLine(hdc, r.left + 1, r.top + 1, r.right - 2, r.top + 1, tl1);
    EdgeLine(hdc, r.left + 1, r.top + 1, r.left + 1, r.bottom - 2, tl1);
    EdgeLine(hdc, r.left + 1, r.bottom - 2, r.right - 2, r.bottom - 2, br1);
    EdgeLine(hdc, r.right - 2, r.top + 1, r.right - 2, r.bottom - 2, br1);
    if (state & 0x400) { // DFCS_CHECKED
        int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
        for (int i = -3; i <= 3; ++i) EdgeLine(hdc, cx + i, cy + (i < 0 ? -i : i) / 2 - 1, cx + i, cy + (i < 0 ? -i : i) / 2 + 1, 18);
    }
    return Ret(1);
}

// --- Системные параметры ---

FN(GetSysColor) { ENTER; return Ret(ui::SysColor(SA(c, m, 0))); }
FN(GetSysColorBrush) { ENTER; return Ret(SysColorBrush(SA(c, m, 0))); }
FN(SetSysColors) {
    ENTER;
    int n = SA(c, m, 0);
    for (int i = 0; i < n; ++i) ui::SetSysColor(static_cast<int>(m.Read32(A(c, m, 1) + i * 4)), m.Read32(A(c, m, 2) + i * 4));
    return Ret(1);
}
FN(GetSystemMetrics) {
    ENTER;
    int W = S().screenW, H = S().screenH;
    switch (SA(c, m, 0)) {
    case 0: case 16: case 78: return Ret(W);
    case 1: case 79: return Ret(H);
    case 17: return Ret(H - kCaptionH);
    case 2: case 3: case 9: case 10: case 20: case 21: return Ret(kScrollW);
    case 4: return Ret(kCaptionH);
    case 5: case 6: case 83: case 84: return Ret(1);
    case 7: case 8: return Ret(kDlgFrame);
    case 11: case 12: case 13: case 14: return Ret(32);
    case 15: return Ret(kMenuH);
    case 19: case 75: case 81: case 80: return Ret(1);
    case 43: return Ret(3);
    case 28: case 34: return Ret(112);
    case 29: case 35: return Ret(27);
    case 30: case 31: return Ret(18);
    case 32: case 33: return Ret(kFrame);
    case 36: case 37: case 68: case 69: return Ret(4);
    case 38: case 39: return Ret(75);
    case 45: case 46: return Ret(kEdge);
    case 47: case 57: return Ret(160);
    case 48: case 58: return Ret(24);
    case 49: case 50: return Ret(16);
    case 51: return Ret(kSmCaptionH);
    case 52: return Ret(12);
    case 53: return Ret(15);
    case 54: case 55: return Ret(18);
    case 59: return Ret(W + 12);
    case 60: return Ret(H + 12);
    case 61: return Ret(W + 8);
    case 62: return Ret(H + 8);
    case 71: case 72: return Ret(13);
    case 63: return Ret(3);
    }
    return Ret(0);
}
HostCallOutcome SystemParametersInfoImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool unicode) {
    uint32_t action = A(c, m, 0), p = A(c, m, 2);
    auto writeLogFont = [&](uint32_t at, int height, int weight) {
        for (uint32_t i = 0; i < (unicode ? 92u : 60u); i += 4) m.Write32(at + i, 0);
        m.Write32(at, static_cast<uint32_t>(height));
        m.Write32(at + 16, static_cast<uint32_t>(weight));
        WriteText(at + 28, u"Tahoma", 32, unicode);
    };
    switch (action) {
    case 0x30: // SPI_GETWORKAREA
        if (p) WriteRect(m, p, WorkArea());
        return Ret(1);
    case 0x29: { // SPI_GETNONCLIENTMETRICS
        if (!p) return Ret(0);
        uint32_t lf = unicode ? 92 : 60;
        m.Write32(p + 4, 1);               // iBorderWidth
        m.Write32(p + 8, kScrollW);        // iScrollWidth
        m.Write32(p + 12, kScrollW);       // iScrollHeight
        m.Write32(p + 16, 18);             // iCaptionWidth
        m.Write32(p + 20, kCaptionH - 1);  // iCaptionHeight
        writeLogFont(p + 24, -11, 700);    // lfCaptionFont
        m.Write32(p + 24 + lf, 12);
        m.Write32(p + 28 + lf, kSmCaptionH - 1);
        writeLogFont(p + 32 + lf, -11, 400);
        m.Write32(p + 32 + 2 * lf, 18);
        m.Write32(p + 36 + 2 * lf, kMenuH - 1);
        writeLogFont(p + 40 + 2 * lf, -11, 400); // меню
        writeLogFont(p + 40 + 3 * lf, -11, 400); // строка состояния
        writeLogFont(p + 40 + 4 * lf, -11, 400); // сообщения
        return Ret(1);
    }
    case 0x1F: // SPI_GETICONTITLELOGFONT
        if (p) writeLogFont(p, -11, 400);
        return Ret(1);
    case 0x26: case 0x2C: case 0x1006: case 0x100A: case 0x1022: case 0x1016: case 0x1018:
        if (p) m.Write32(p, 0);
        return Ret(1);
    case 0x68: case 0x64: // SPI_GETWHEELSCROLLLINES / GETKEYBOARDDELAY
        if (p) m.Write32(p, 3);
        return Ret(1);
    case 0x10: case 0x0A: // SPI_GETSCREENSAVEACTIVE / GETKEYBOARDSPEED
        if (p) m.Write32(p, action == 0x0A ? 31 : 0);
        return Ret(1);
    case 0x2D: case 0x48: // SPI_GETANIMATION, SPI_GETHIGHCONTRAST
        return Ret(1);
    }
    if (p && action < 0x2000 && (action & 1) == 0) m.Write32(p, 0);
    return Ret(1);
}
AW(SystemParametersInfo)

// --- Курсор, клавиатура ---

struct CursorState {uint32_t cursor=0;int show=0;};
CursorState& CursorData(){return rt::LocalState<CursorState>();}
FN(SetCursor) { ENTER; uint32_t old = CursorData().cursor; CursorData().cursor = A(c, m, 0); return Ret(old); }
FN(GetCursor) { ENTER; return Ret(CursorData().cursor); }
FN(ShowCursor) { ENTER; CursorData().show += A(c, m, 0) ? 1 : -1; return Ret(static_cast<uint32_t>(CursorData().show)); }
FN(GetCursorPos) {
    ENTER;
    if (A(c, m, 0)) { m.Write32(A(c, m, 0), static_cast<uint32_t>(S().cursorX)); m.Write32(A(c, m, 0) + 4, static_cast<uint32_t>(S().cursorY)); }
    return Ret(1);
}
FN(SetCursorPos) { ENTER; S().cursorX = SA(c, m, 0); S().cursorY = SA(c, m, 1); return Ret(1); }
FN(ClipCursor) { ENTER; return Ret(1); }
FN(GetKeyState) { ENTER; uint8_t k = Q().keys[A(c, m, 0) & 0xFF]; return Ret(static_cast<uint32_t>(static_cast<int16_t>((k & 0x80 ? 0xFF80 : 0) | (k & 1)))); }
FN(GetAsyncKeyState) { ENTER; return Ret(S().asyncKeys[A(c, m, 0) & 0xFF] & 0x80 ? 0xFFFF8000u : 0); }
FN(GetKeyboardState) { ENTER; for (int i = 0; i < 256; ++i) m.Write8(A(c, m, 0) + i, Q().keys[i]); return Ret(1); }
FN(SetKeyboardState) { ENTER; for (int i = 0; i < 256; ++i) Q().keys[i] = m.Read8(A(c, m, 0) + i); return Ret(1); }
FN(MapVirtualKeyA) {
    ENTER;
    uint32_t code = A(c, m, 0), type = A(c, m, 1);
    if (type == 2) return Ret(code >= 'A' && code <= 'Z' ? code : (code >= '0' && code <= '9' ? code : 0));
    return Ret(code);
}
FN(GetKeyboardLayout) { ENTER; return Ret(0x04090409); }
FN(GetKeyboardType) { ENTER; uint32_t t = A(c, m, 0); return Ret(t == 0 ? 4 : t == 2 ? 12 : 0); }
FN(GetDoubleClickTime) { ENTER; return Ret(500); }
FN(MessageBeep) { ENTER; return Ret(1); }

// --- Прямоугольники ---

FN(SetRect) { ENTER; WriteRect(m, A(c, m, 0), Rect(SA(c, m, 1), SA(c, m, 2), SA(c, m, 3), SA(c, m, 4))); return Ret(1); }
FN(SetRectEmpty) { ENTER; WriteRect(m, A(c, m, 0), Rect()); return Ret(1); }
FN(CopyRect) { ENTER; WriteRect(m, A(c, m, 0), ReadRect(m, A(c, m, 1))); return Ret(1); }
FN(IsRectEmpty) { ENTER; return Ret(ReadRect(m, A(c, m, 0)).Empty() ? 1 : 0); }
FN(EqualRect) { ENTER; return Ret(ReadRect(m, A(c, m, 0)) == ReadRect(m, A(c, m, 1)) ? 1 : 0); }
FN(InflateRect) {
    ENTER;
    Rect r = ReadRect(m, A(c, m, 0));
    int dx = SA(c, m, 1), dy = SA(c, m, 2);
    WriteRect(m, A(c, m, 0), Rect(r.left - dx, r.top - dy, r.right + dx, r.bottom + dy));
    return Ret(1);
}
FN(OffsetRect) { ENTER; WriteRect(m, A(c, m, 0), ReadRect(m, A(c, m, 0)).Offset(SA(c, m, 1), SA(c, m, 2))); return Ret(1); }
FN(IntersectRect) {
    ENTER;
    Rect r = ReadRect(m, A(c, m, 1)).Intersect(ReadRect(m, A(c, m, 2)));
    WriteRect(m, A(c, m, 0), r);
    return Ret(r.Empty() ? 0 : 1);
}
FN(UnionRect) {
    ENTER;
    Rect r = ReadRect(m, A(c, m, 1)).Union(ReadRect(m, A(c, m, 2)));
    WriteRect(m, A(c, m, 0), r);
    return Ret(r.Empty() ? 0 : 1);
}
FN(SubtractRect) {
    ENTER;
    Rect a = ReadRect(m, A(c, m, 1)), b = ReadRect(m, A(c, m, 2));
    Region r(a);
    r.SubtractRect(b);
    Rect res = r.Rects().size() == 1 ? r.Rects()[0] : (r.Empty() ? Rect() : a);
    WriteRect(m, A(c, m, 0), res);
    return Ret(res.Empty() ? 0 : 1);
}
FN(PtInRect) { ENTER; return Ret(ReadRect(m, A(c, m, 0)).Contains(SA(c, m, 1), SA(c, m, 2)) ? 1 : 0); }

// --- Строки ---

HostCallOutcome LoadStringImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool unicode) {
    uint32_t inst = A(c, m, 0), id = A(c, m, 1), buf = A(c, m, 2);
    int cap = SA(c, m, 3);
    std::u16string s;
    rt::Module* mod = ModuleOf(inst);
    if (!rt::LoadStringResource(mod, id, s)) { if (buf && cap > 0) WriteText(buf, u"", 1, unicode); return Ret(0); }
    if (unicode && cap == 0) { // указатель на строку в ресурсе
        rt::ResourceData d;
        rt::FindResource(mod, rt::ResId::Num(rt::RT_STRING), rt::ResId::Num(static_cast<uint16_t>((id >> 4) + 1)), d);
        const uint8_t* p = rt::ResourcePtr(mod, d);
        uint32_t off = 0;
        for (uint32_t i = 0; i < (id & 15); ++i) off += 2 + 2u * (p[off] | (p[off + 1] << 8));
        m.Write32(buf, mod->base + d.rva + off + 2);
        return Ret(static_cast<uint32_t>(s.size()));
    }
    if (!buf || cap <= 0) return Ret(0);
    return Ret(WriteText(buf, s, static_cast<uint32_t>(cap), unicode));
}
AW(LoadString)

HostCallOutcome WsprintfCommon(cpu::CpuState& c, cpu::IGuestMemory& m, bool unicode, uint32_t args) {
    uint32_t out = A(c, m, 0), fmt = A(c, m, 1);
    crt::GuestVaList va{m, args};
    if (unicode) {
        std::u16string s = crt::FormatW(m, fmt, va);
        if (s.size() > 1023) s.resize(1023);
        return Ret(WriteWide(m, out, s, 1024));
    }
    std::string s = crt::FormatA(m, fmt, va);
    if (s.size() > 1023) s.resize(1023);
    return Ret(WriteAnsi(m, out, s, 1024));
}
FN(wsprintfA) { ENTER; return WsprintfCommon(c, m, false, c.ESP() + 12); }
FN(wsprintfW) { ENTER; return WsprintfCommon(c, m, true, c.ESP() + 12); }
FN(wvsprintfA) { ENTER; return WsprintfCommon(c, m, false, A(c, m, 2)); }
FN(wvsprintfW) { ENTER; return WsprintfCommon(c, m, true, A(c, m, 2)); }

char16_t Upper(char16_t ch) {
    if (ch >= 'a' && ch <= 'z') return static_cast<char16_t>(ch - 32);
    if (ch >= 0xE0 && ch <= 0xFE && ch != 0xF7) return static_cast<char16_t>(ch - 32);
    if (ch >= 0x430 && ch <= 0x44F) return static_cast<char16_t>(ch - 32);
    return ch;
}
char16_t Lower(char16_t ch) {
    if (ch >= 'A' && ch <= 'Z') return static_cast<char16_t>(ch + 32);
    if (ch >= 0xC0 && ch <= 0xDE && ch != 0xD7) return static_cast<char16_t>(ch + 32);
    if (ch >= 0x410 && ch <= 0x42F) return static_cast<char16_t>(ch + 32);
    return ch;
}
HostCallOutcome CaseCommon(cpu::IGuestMemory& m, uint32_t p, uint32_t len, bool unicode, bool upper, bool isBuff) {
    if (!isBuff && p < 0x10000) { // один символ в младшем слове
        char16_t ch = static_cast<char16_t>(p & 0xFFFF);
        return Ret(upper ? Upper(ch) : Lower(ch));
    }
    if (!isBuff) len = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < len; ++i) {
        if (unicode) {
            char16_t ch = static_cast<char16_t>(m.Read16(p + i * 2));
            if (!ch && !isBuff) break;
            m.Write16(p + i * 2, upper ? Upper(ch) : Lower(ch));
        } else {
            char16_t ch = m.Read8(p + i);
            if (!ch && !isBuff) break;
            m.Write8(p + i, static_cast<uint8_t>(upper ? Upper(ch) : Lower(ch)));
        }
    }
    return Ret(isBuff ? len : p);
}
FN(CharUpperA) { ENTER; return CaseCommon(m, A(c, m, 0), 0, false, true, false); }
FN(CharUpperW) { ENTER; return CaseCommon(m, A(c, m, 0), 0, true, true, false); }
FN(CharLowerA) { ENTER; return CaseCommon(m, A(c, m, 0), 0, false, false, false); }
FN(CharLowerW) { ENTER; return CaseCommon(m, A(c, m, 0), 0, true, false, false); }
FN(CharUpperBuffA) { ENTER; return CaseCommon(m, A(c, m, 0), A(c, m, 1), false, true, true); }
FN(CharUpperBuffW) { ENTER; return CaseCommon(m, A(c, m, 0), A(c, m, 1), true, true, true); }
FN(CharLowerBuffA) { ENTER; return CaseCommon(m, A(c, m, 0), A(c, m, 1), false, false, true); }
FN(CharLowerBuffW) { ENTER; return CaseCommon(m, A(c, m, 0), A(c, m, 1), true, false, true); }
FN(CharNextA) { ENTER; uint32_t p = A(c, m, 0); return Ret(m.Read8(p) ? p + 1 : p); }
FN(CharNextW) { ENTER; uint32_t p = A(c, m, 0); return Ret(m.Read16(p) ? p + 2 : p); }
FN(CharPrevA) { ENTER; uint32_t s = A(c, m, 0), p = A(c, m, 1); return Ret(p > s ? p - 1 : s); }
FN(CharPrevW) { ENTER; uint32_t s = A(c, m, 0), p = A(c, m, 1); return Ret(p > s ? p - 2 : s); }
bool IsAlpha(char16_t ch) { return Upper(ch) != Lower(ch) || (ch >= 0xDF && ch <= 0xFF && ch != 0xF7); }
FN(IsCharAlphaA) { ENTER; return Ret(IsAlpha(static_cast<char16_t>(A(c, m, 0) & 0xFF)) ? 1 : 0); }
FN(IsCharAlphaW) { ENTER; return Ret(IsAlpha(static_cast<char16_t>(A(c, m, 0) & 0xFFFF)) ? 1 : 0); }
FN(IsCharAlphaNumericA) { ENTER; char16_t ch = static_cast<char16_t>(A(c, m, 0) & 0xFF); return Ret(IsAlpha(ch) || (ch >= '0' && ch <= '9') ? 1 : 0); }
FN(IsCharAlphaNumericW) { ENTER; char16_t ch = static_cast<char16_t>(A(c, m, 0) & 0xFFFF); return Ret(IsAlpha(ch) || (ch >= '0' && ch <= '9') ? 1 : 0); }
FN(IsCharUpperA) { ENTER; char16_t ch = static_cast<char16_t>(A(c, m, 0) & 0xFF); return Ret(Lower(ch) != ch ? 1 : 0); }
FN(IsCharUpperW) { ENTER; char16_t ch = static_cast<char16_t>(A(c, m, 0) & 0xFFFF); return Ret(Lower(ch) != ch ? 1 : 0); }
FN(IsCharLowerA) { ENTER; char16_t ch = static_cast<char16_t>(A(c, m, 0) & 0xFF); return Ret(Upper(ch) != ch ? 1 : 0); }
FN(IsCharLowerW) { ENTER; char16_t ch = static_cast<char16_t>(A(c, m, 0) & 0xFFFF); return Ret(Upper(ch) != ch ? 1 : 0); }

// --- Окно сообщения (пока без своего окна: в журнал и ответ по умолчанию) ---

HostCallOutcome MessageBoxImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool unicode) {
    std::u16string text = ReadText(A(c, m, 1), unicode), caption = ReadText(A(c, m, 2), unicode);
    if (cpu::g_consoleAvailable)
        std::printf("[MessageBox] %s: %s\n", WideToUtf8(caption).c_str(), WideToUtf8(text).c_str());
    uint32_t type = A(c, m, 3) & 0xF;
    switch (type) {
    case 1: case 0: return Ret(1);     // MB_OK / OKCANCEL -> IDOK
    case 3: case 4: return Ret(6);     // YESNO(CANCEL) -> IDYES
    case 2: return Ret(5);             // ABORTRETRYIGNORE -> IDIGNORE
    case 5: return Ret(2);             // RETRYCANCEL -> IDCANCEL
    }
    return Ret(1);
}
AW(MessageBox)
HostCallOutcome MessageBoxExImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool unicode) { return MessageBoxImpl(c, m, unicode); }
AW(MessageBoxEx)

// --- Мониторы и экран ---

FN(MonitorFromWindow) { ENTER; return Ret(0x12340042); }
HostCallOutcome GetMonitorInfoImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool unicode) {
    uint32_t p = A(c, m, 1);
    uint32_t size = m.Read32(p);
    WriteRect(m, p + 4, Rect(0, 0, S().screenW, S().screenH));
    WriteRect(m, p + 20, WorkArea());
    m.Write32(p + 36, 1); // MONITORINFOF_PRIMARY
    if (size >= 72) WriteText(p + 40, u"\\\\.\\DISPLAY1", 32, unicode);
    return Ret(1);
}
AW(GetMonitorInfo)
HostCallOutcome EnumDisplaySettingsImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool unicode) {
    uint32_t mode = A(c, m, 1), p = A(c, m, 2);
    if (mode != 0xFFFFFFFFu && mode != 0xFFFFFFFEu && mode > 0) return Ret(0);
    uint32_t o = unicode ? 64 : 32; // после dmDeviceName
    m.Write16(p + o + 4, static_cast<uint16_t>(unicode ? 220 : 156)); // dmSize
    m.Write32(p + o + 8, 0x5C0000);                                     // dmFields
    uint32_t tail = unicode ? 168 : 104; // смещение dmBitsPerPel
    m.Write32(p + tail, 32);
    m.Write32(p + tail + 4, static_cast<uint32_t>(S().screenW));
    m.Write32(p + tail + 8, static_cast<uint32_t>(S().screenH));
    m.Write32(p + tail + 16, 60);
    return Ret(1);
}
AW(EnumDisplaySettings)
HostCallOutcome ChangeDisplaySettingsImpl(cpu::CpuState&, cpu::IGuestMemory&, bool) { return Ret(0); } // DISP_CHANGE_SUCCESSFUL
AW(ChangeDisplaySettings)

// --- Прочее ---

FN(GetLastActivePopup) { ENTER; return Ret(A(c, m, 0)); }
FN(ShowOwnedPopups) { ENTER; return Ret(1); }
FN(FlashWindow) { ENTER; return Ret(0); }
FN(AnyPopup) { ENTER; return Ret(0); }
FN(SetWindowRgn) { ENTER; ui::g_gdi.DeleteObject(A(c, m, 1)); return Ret(1); }
FN(GetWindowRgn) { ENTER; return Ret(0); }
FN(OpenClipboard) { ENTER; return Ret(1); }
FN(CloseClipboard) { ENTER; return Ret(1); }
FN(EmptyClipboard) { ENTER; return Ret(1); }
FN(IsClipboardFormatAvailable) { ENTER; return Ret(0); }
FN(GetClipboardData) { ENTER; return Ret(0); }
FN(SetClipboardData) { ENTER; return Ret(A(c, m, 1)); }
HostCallOutcome RegisterClipboardFormatImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool u) { return RegisterWindowMessageImpl(c, m, u); }
AW(RegisterClipboardFormat)
FN(GetGuiResources) { ENTER; return Ret(10); }
FN(GetProcessWindowStation) { ENTER; return Ret(0x12340050); }
FN(GetUserObjectInformationW) { ENTER; if (A(c, m, 4)) m.Write32(A(c, m, 4), 0); return Ret(1); }
FN(SetWindowContextHelpId) { ENTER; return Ret(1); }

#define E(name, bytes) {"USER32.dll", #name, bytes, CallConv::Stdcall, &name}
#define EAW(name, bytes) E(name##A, bytes), E(name##W, bytes)
const HostFunctionEntry kEntries[] = {
    EAW(RegisterClass, 4), EAW(RegisterClassEx, 4), EAW(UnregisterClass, 8), EAW(GetClassInfo, 12), EAW(GetClassInfoEx, 12),
    EAW(GetClassName, 12),
    {"USER32.dll", "GetClassLongA", 8, CallConv::Stdcall, &GetClassLongA}, {"USER32.dll", "GetClassLongW", 8, CallConv::Stdcall, &GetClassLongA},
    {"USER32.dll", "SetClassLongA", 12, CallConv::Stdcall, &SetClassLongA}, {"USER32.dll", "SetClassLongW", 12, CallConv::Stdcall, &SetClassLongA},
    EAW(CreateWindowEx, 48), E(DestroyWindow, 4), E(ShowWindow, 8), E(SetWindowPos, 28), E(MoveWindow, 24),
    E(BeginDeferWindowPos, 4), E(DeferWindowPos, 32), E(EndDeferWindowPos, 4),
    E(BringWindowToTop, 4), E(GetWindowRect, 8), E(GetClientRect, 8), E(AdjustWindowRect, 12), E(AdjustWindowRectEx, 16),
    E(ClientToScreen, 8), E(ScreenToClient, 8), E(MapWindowPoints, 16),
    EAW(GetWindowLong, 8), EAW(SetWindowLong, 12), E(GetWindowWord, 8), E(SetWindowWord, 12),
    EAW(GetWindowText, 12), EAW(SetWindowText, 8), EAW(GetWindowTextLength, 4),
    E(GetParent, 4), E(SetParent, 8), E(GetDesktopWindow, 0), E(GetWindow, 8), E(GetTopWindow, 4), E(GetDlgItem, 8),
    E(GetDlgCtrlID, 4), E(IsWindow, 4), E(IsWindowVisible, 4), E(IsWindowEnabled, 4), E(IsWindowUnicode, 4),
    E(EnableWindow, 8), E(IsChild, 8), E(IsIconic, 4), E(IsZoomed, 4), E(GetFocus, 0), E(SetFocus, 4),
    E(GetActiveWindow, 0), E(SetActiveWindow, 4), E(GetForegroundWindow, 0), E(SetForegroundWindow, 4),
    E(GetGUIThreadInfo, 8), E(AttachThreadInput, 12), E(GetCapture, 0), E(SetCapture, 4), E(ReleaseCapture, 0), E(WindowFromPoint, 8), E(ChildWindowFromPoint, 12),
    E(ChildWindowFromPointEx, 16), EAW(FindWindow, 8), EAW(FindWindowEx, 16), E(EnumWindows, 8), E(EnumChildWindows, 12),
    E(GetWindowThreadProcessId, 8), EAW(SetProp, 12), EAW(GetProp, 8), EAW(RemoveProp, 8),
    E(GetWindowPlacement, 8), E(SetWindowPlacement, 8),
    EAW(DefWindowProc, 16), EAW(CallWindowProc, 20), EAW(SendMessage, 16), EAW(SendMessageTimeout, 28),
    EAW(SendNotifyMessage, 16), EAW(PostMessage, 16), EAW(PostThreadMessage, 16), E(PostQuitMessage, 4),
    EAW(GetMessage, 16), EAW(PeekMessage, 20), E(WaitMessage, 0), E(MsgWaitForMultipleObjects, 20),
    E(MsgWaitForMultipleObjectsEx, 20), E(TranslateMessage, 4), EAW(DispatchMessage, 4), E(GetMessagePos, 0),
    E(GetMessageTime, 0), E(GetQueueStatus, 4), E(GetInputState, 0), E(InSendMessage, 0), E(InSendMessageEx, 4), E(ReplyMessage, 4),
    EAW(RegisterWindowMessage, 4), E(SetTimer, 16), E(KillTimer, 8),
    E(BeginPaint, 8), E(EndPaint, 8), E(GetDC, 4), E(GetDCEx, 12), E(GetWindowDC, 4), E(ReleaseDC, 8),
    E(InvalidateRect, 12), E(InvalidateRgn, 12), E(ValidateRect, 8), E(ValidateRgn, 8), E(UpdateWindow, 4),
    E(RedrawWindow, 16), E(GetUpdateRect, 12), E(GetUpdateRgn, 12), E(ScrollWindow, 20), E(ScrollWindowEx, 32),
    E(LockWindowUpdate, 4), E(FillRect, 12), E(FrameRect, 12), E(InvertRect, 8), EAW(DrawText, 20), EAW(DrawTextEx, 24),
    E(DrawEdge, 16), E(DrawFocusRect, 8), E(DrawFrameControl, 16),
    E(GetSysColor, 4), E(GetSysColorBrush, 4), E(SetSysColors, 12), E(GetSystemMetrics, 4), EAW(SystemParametersInfo, 16),
    E(SetCursor, 4), E(GetCursor, 0), E(ShowCursor, 4), E(GetCursorPos, 4),
    E(SetCursorPos, 8), E(ClipCursor, 4),
    E(GetKeyState, 4), E(GetAsyncKeyState, 4), E(GetKeyboardState, 4), E(SetKeyboardState, 4), E(MapVirtualKeyA, 8),
    {"USER32.dll", "MapVirtualKeyW", 8, CallConv::Stdcall, &MapVirtualKeyA}, E(GetKeyboardLayout, 4), E(GetKeyboardType, 4),
    E(GetDoubleClickTime, 0), E(MessageBeep, 4),
    E(SetRect, 20), E(SetRectEmpty, 4), E(CopyRect, 8), E(IsRectEmpty, 4), E(EqualRect, 8), E(InflateRect, 12),
    E(OffsetRect, 12), E(IntersectRect, 12), E(UnionRect, 12), E(SubtractRect, 12), E(PtInRect, 12),
    EAW(LoadString, 16),
    {"USER32.dll", "wsprintfA", 0, CallConv::Cdecl, &wsprintfA}, {"USER32.dll", "wsprintfW", 0, CallConv::Cdecl, &wsprintfW},
    E(wvsprintfA, 12), E(wvsprintfW, 12),
    E(CharUpperA, 4), E(CharUpperW, 4), E(CharLowerA, 4), E(CharLowerW, 4), E(CharUpperBuffA, 8), E(CharUpperBuffW, 8),
    E(CharLowerBuffA, 8), E(CharLowerBuffW, 8), E(CharNextA, 4), E(CharNextW, 4), E(CharPrevA, 8), E(CharPrevW, 8),
    E(IsCharAlphaA, 4), E(IsCharAlphaW, 4), E(IsCharAlphaNumericA, 4), E(IsCharAlphaNumericW, 4), E(IsCharUpperA, 4),
    E(IsCharUpperW, 4), E(IsCharLowerA, 4), E(IsCharLowerW, 4),
    EAW(MessageBox, 16), EAW(MessageBoxEx, 20),
    E(MonitorFromWindow, 8), {"USER32.dll", "MonitorFromPoint", 12, CallConv::Stdcall, &MonitorFromWindow},
    {"USER32.dll", "MonitorFromRect", 8, CallConv::Stdcall, &MonitorFromWindow}, EAW(GetMonitorInfo, 8),
    EAW(EnumDisplaySettings, 12), EAW(ChangeDisplaySettings, 8),
    E(GetLastActivePopup, 4), E(ShowOwnedPopups, 8), E(FlashWindow, 8), E(AnyPopup, 0), E(SetWindowRgn, 12), E(GetWindowRgn, 8),
    E(OpenClipboard, 4), E(CloseClipboard, 0), E(EmptyClipboard, 0), E(IsClipboardFormatAvailable, 4),
    E(GetClipboardData, 4), E(SetClipboardData, 8), EAW(RegisterClipboardFormat, 4), E(GetGuiResources, 8),
    E(GetProcessWindowStation, 0), E(GetUserObjectInformationW, 20), E(SetWindowContextHelpId, 8),
};
const bool kRegistered = RegisterHostFunctionsLayer(kEntries, sizeof(kEntries) / sizeof(kEntries[0]), kUiLayer);

} // namespace

void EnableUi(int screenW, int screenH) {
    EnableHostLayer(kUiLayer, true);
    S().screenW = screenW;
    S().screenH = screenH;
    Init();
}

} // namespace user
} // namespace win32
