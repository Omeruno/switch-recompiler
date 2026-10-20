// user32_dialog.cpp — диалоги USER32 (слой "ui2"): шаблоны DLGTEMPLATE и
// DLGTEMPLATEEX из ресурсов и памяти, диалоговые единицы по шрифту,
// модальные и немодальные диалоги, DefDlgProc, клавиатурная навигация
// (IsDialogMessage: Tab, стрелки, Enter, Esc, мнемоники), функции
// Get/SetDlgItem*, CheckDlgButton и т.п.

#include <algorithm>
#include <cstdio>

#include "../runtime/process.h"
#include "../runtime/resources.h"
#include "user32.h"

namespace win32 {
namespace user {

using namespace hle;

struct DialogInfo {
    uint32_t dlgProc = 0;
    bool unicode = false;
    bool ended = false;
    uint32_t result = 0;
    uint32_t font = 0;
    int baseX = 6, baseY = 13;
    uint32_t defId = 1; // IDOK
    uint32_t lastFocus = 0;
};

namespace {

constexpr uint32_t DS_ABSALIGN = 0x01, DS_SETFONT = 0x40, DS_MODALFRAME = 0x80, DS_CONTROL = 0x400, DS_CENTER = 0x800,
                   DS_CENTERMOUSE = 0x1000, DS_SHELLFONT = 0x48;
constexpr uint32_t DLGC_WANTARROWS = 0x1, DLGC_WANTTAB = 0x2, DLGC_WANTALLKEYS = 0x4, DLGC_WANTMESSAGE = 0x4,
                   DLGC_HASSETSEL = 0x8, DLGC_DEFPUSHBUTTON = 0x10, DLGC_UNDEFPUSHBUTTON = 0x20, DLGC_RADIOBUTTON = 0x40,
                   DLGC_WANTCHARS = 0x80, DLGC_STATIC = 0x100, DLGC_BUTTON = 0x2000;
constexpr uint32_t WS_GROUP = 0x00020000, WS_TABSTOP = 0x00010000;
constexpr uint32_t DM_GETDEFID = 0x400, DM_SETDEFID = 0x401, WM_NEXTDLGCTL = 0x28;



DialogInfo* g_pendingInfo = nullptr; // диалог в процессе создания (до появления хендла)

std::map<uint32_t, DialogInfo>& Dialogs() {
    static std::map<uint32_t, DialogInfo> d;
    return d;
}
DialogInfo* Dlg(uint32_t hwnd) {
    auto it = Dialogs().find(hwnd);
    return it == Dialogs().end() ? nullptr : &it->second;
}

// --- Разбор шаблона ---

struct TplReader {
    const uint8_t* p;
    size_t size, off = 0;
    uint16_t U16() { if (off + 2 > size) { off = size; return 0; } uint16_t v = static_cast<uint16_t>(p[off] | (p[off + 1] << 8)); off += 2; return v; }
    uint32_t U32() { uint32_t lo = U16(); return lo | (static_cast<uint32_t>(U16()) << 16); }
    uint8_t U8() { return off < size ? p[off++] : 0; }
    std::u16string Str() { std::u16string s; while (off < size) { char16_t c = static_cast<char16_t>(U16()); if (!c) break; s.push_back(c); } return s; }
    // sz_Or_Ord: возвращает строку или ordinal (ord != 0)
    void SzOrOrd(std::u16string& s, uint16_t& ord) {
        ord = 0;
        s.clear();
        uint16_t first = U16();
        if (first == 0) return;
        if (first == 0xFFFF) { ord = U16(); return; }
        s.push_back(static_cast<char16_t>(first));
        while (off < size) { char16_t c = static_cast<char16_t>(U16()); if (!c) break; s.push_back(c); }
    }
    void Align4() { off = (off + 3) & ~static_cast<size_t>(3); }
};

struct DlgItem {
    uint32_t style = 0, exStyle = 0, helpId = 0;
    int x = 0, y = 0, cx = 0, cy = 0;
    uint32_t id = 0;
    std::u16string cls, title;
    uint16_t clsOrd = 0, titleOrd = 0;
    std::vector<uint8_t> extra;
};
struct DlgTemplate {
    uint32_t style = 0, exStyle = 0;
    int x = 0, y = 0, cx = 0, cy = 0;
    std::u16string menu, cls, title, face;
    uint16_t menuOrd = 0, clsOrd = 0;
    int pointSize = 0, weight = 400;
    bool italic = false;
    std::vector<DlgItem> items;
};

bool ParseTemplate(const uint8_t* data, size_t size, DlgTemplate& t) {
    TplReader r{data, size};
    bool ex = size >= 4 && data[0] == 1 && data[1] == 0 && data[2] == 0xFF && data[3] == 0xFF;
    uint16_t count;
    if (ex) {
        r.U16(); r.U16();
        r.U32(); // helpID
        t.exStyle = r.U32();
        t.style = r.U32();
    } else {
        t.style = r.U32();
        t.exStyle = r.U32();
    }
    count = r.U16();
    t.x = static_cast<int16_t>(r.U16());
    t.y = static_cast<int16_t>(r.U16());
    t.cx = static_cast<int16_t>(r.U16());
    t.cy = static_cast<int16_t>(r.U16());
    r.SzOrOrd(t.menu, t.menuOrd);
    r.SzOrOrd(t.cls, t.clsOrd);
    t.title = r.Str();
    if (t.style & DS_SETFONT) {
        t.pointSize = r.U16();
        if (ex) { t.weight = r.U16(); t.italic = r.U8() != 0; r.U8(); }
        t.face = r.Str();
    }
    for (uint16_t i = 0; i < count && r.off < size; ++i) {
        r.Align4();
        DlgItem it;
        if (ex) {
            it.helpId = r.U32();
            it.exStyle = r.U32();
            it.style = r.U32();
        } else {
            it.style = r.U32();
            it.exStyle = r.U32();
        }
        it.x = static_cast<int16_t>(r.U16());
        it.y = static_cast<int16_t>(r.U16());
        it.cx = static_cast<int16_t>(r.U16());
        it.cy = static_cast<int16_t>(r.U16());
        it.id = ex ? r.U32() : r.U16();
        r.SzOrOrd(it.cls, it.clsOrd);
        r.SzOrOrd(it.title, it.titleOrd);
        uint16_t extra = r.U16();
        if (extra) {
            size_t n = ex ? extra : extra; // в старом формате длина включает само слово
            if (!ex && n >= 2) n -= 2;
            for (size_t k = 0; k < n && r.off < size; ++k) it.extra.push_back(r.U8());
        }
        t.items.push_back(it);
    }
    return true;
}

std::u16string OrdClass(uint16_t ord) {
    switch (ord) {
    case 0x80: return u"Button";
    case 0x81: return u"Edit";
    case 0x82: return u"Static";
    case 0x83: return u"ListBox";
    case 0x84: return u"ScrollBar";
    case 0x85: return u"ComboBox";
    }
    return u"Static";
}

// Диалоговые единицы шрифта: средняя ширина латиницы и высота
void BaseUnits(uint32_t font, int& bx, int& by) {
    uint32_t hdc = ui::g_gdi.CreateDc(ui::DcKind::Memory);
    uint32_t old = ui::g_gdi.SelectObject(hdc, font);
    int w, h;
    ui::g_gdi.TextExtent(hdc, u"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz", w, h);
    ui::g_gdi.SelectObject(hdc, old);
    ui::g_gdi.DeleteDc(hdc);
    bx = (w / 26 + 1) / 2;
    by = h;
    if (bx < 1) bx = 6;
    if (by < 1) by = 13;
}

int MulDiv(int a, int b, int c) { return c ? static_cast<int>((static_cast<int64_t>(a) * b + (c / 2)) / c) : 0; }

uint32_t CreateFromTemplate(const DlgTemplate& t, uint32_t hInst, uint32_t owner, uint32_t dlgProc, uint32_t param, bool unicode) {
    State& s = S();
    // шрифт
    uint32_t font = ui::g_gdi.StockObject(13);
    if (t.style & DS_SETFONT) {
        ui::FontDesc fd;
        fd.face = WideToUtf8(t.face);
        if (fd.face == "MS Shell Dlg" || fd.face == "MS Shell Dlg 2" || fd.face == "MS Sans Serif") fd.face = "Tahoma";
        fd.height = -MulDiv(t.pointSize ? t.pointSize : 8, 96, 72);
        fd.weight = t.weight ? t.weight : 400;
        fd.italic = t.italic;
        font = ui::g_gdi.CreateFont(fd);
    }
    int bx, by;
    BaseUnits(font, bx, by);
    auto px = [&](int du) { return MulDiv(du, bx, 4); };
    auto py = [&](int du) { return MulDiv(du, by, 8); };

    uint32_t style = t.style & ~WS_VISIBLE;
    if (style & DS_MODALFRAME) style |= WS_DLGFRAME;
    uint32_t exStyle = t.exStyle | ((t.style & DS_MODALFRAME) ? WS_EX_DLGMODALFRAME : 0);
    Rect in;
    bool menu = !t.menu.empty() || t.menuOrd;
    NonClientInsets(style, exStyle, menu && !(style & WS_CHILD), in);
    int cw = px(t.cx), ch = py(t.cy);
    int w = cw + in.left + in.right, h = ch + in.top + in.bottom;
    int x = px(t.x), y = py(t.y);
    Window* ow = Win(owner);
    if (style & WS_CHILD) {
        // координаты в клиентской области родителя
    } else if (t.style & DS_CENTER) {
        Rect area = (ow && (ow->style & WS_VISIBLE) && !(ow->style & WS_MINIMIZE)) ? ScreenRect(ow) : Rect(0, 0, s.screenW, s.screenH);
        x = area.left + (area.Width() - w) / 2;
        y = area.top + (area.Height() - h) / 2;
    } else if (!(t.style & DS_ABSALIGN) && ow) {
        x += ClientOriginX(ow);
        y += ClientOriginY(ow);
    }
    if (!(style & WS_CHILD)) {
        x = std::max(0, std::min(x, s.screenW - w));
        y = std::max(0, std::min(y, s.screenH - h));
    }
    uint32_t hMenu = 0;
    if (menu && !(style & WS_CHILD)) hMenu = LoadMenuForDialog(hInst, t.menu, t.menuOrd);
    std::u16string cls = t.cls.empty() && !t.clsOrd ? u"#32770" : t.cls;
    // окно диалога; его процедура — DefDlgProc, процедура программы — в DialogInfo
    DialogInfo info;
    info.dlgProc = dlgProc;
    info.unicode = unicode;
    info.font = font;
    info.baseX = bx;
    info.baseY = by;
    // DialogInfo нужна уже при WM_NCCREATE/WM_CREATE, а хендла ещё нет
    g_pendingInfo = &info;
    uint32_t hwnd = CreateWindowInternal(exStyle, cls, t.clsOrd, t.title, style, x, y, w, h, owner, hMenu, hInst, param, unicode, 0, 0);
    g_pendingInfo = nullptr;
    if (!hwnd) return 0;
    Dialogs()[hwnd] = info;
    Window* dw = Win(hwnd);
    if (dw && cls == u"#32770") {
        dw->wndProc = DefDlgProcAddress(unicode);
        dw->unicode = unicode;
    }
    if (t.style & DS_SETFONT) SendMsg(hwnd, WM_SETFONT, font, 0);
    // элементы управления
    for (const DlgItem& it : t.items) {
        std::u16string icls = it.cls.empty() ? OrdClass(it.clsOrd) : it.cls;
        std::u16string title = it.title;
        if (it.titleOrd) { title = u"\xFFFF"; title.push_back(static_cast<char16_t>(it.titleOrd)); }
        StackBuffer extra(static_cast<uint32_t>(it.extra.size() + 4));
        uint32_t extraPtr = 0;
        if (!it.extra.empty()) {
            G().m->LoadBytes(extra.addr(), it.extra.data(), it.extra.size());
            extraPtr = extra.addr();
        }
        uint32_t ctl = CreateWindowInternal(it.exStyle | 0x4 /* WS_EX_NOPARENTNOTIFY */, icls, 0, title, it.style | WS_CHILD,
                                            px(it.x), py(it.y), px(it.cx), py(it.cy), hwnd, it.id, hInst, extraPtr, true, 0, 0);
        if (!Win(hwnd)) return 0;
        if (ctl && (t.style & DS_SETFONT)) SendMsg(ctl, WM_SETFONT, font, 0);
    }
    // WM_INITDIALOG: фокус на первый элемент с WS_TABSTOP
    uint32_t first = 0;
    if ((dw = Win(hwnd)))
        for (auto it = dw->children.rbegin(); it != dw->children.rend(); ++it) // в порядке создания
            if (Window* cw2 = Win(*it)) if ((cw2->style & WS_TABSTOP) && (cw2->style & WS_VISIBLE) && !(cw2->style & 0x08000000)) { first = *it; break; }
    uint32_t r = CallProc(dlgProc, unicode, hwnd, WM_INITDIALOG, first, param, unicode);
    if (!Win(hwnd)) return 0;
    if (r && first) SetFocusInternal(first);
    if (t.style & WS_VISIBLE) ShowWindowInternal(hwnd, 5);
    return hwnd;
}

} // namespace

uint32_t LoadMenuForDialog(uint32_t hInst, const std::u16string& name, uint16_t ord) {
    if (!rt::g_process) return 0;
    rt::Module* mod = hInst ? rt::g_process->FindByHandle(hInst) : rt::g_process->MainModule();
    rt::ResourceData d;
    rt::ResId id = ord ? rt::ResId::Num(ord) : rt::ResId::Str(name);
    if (!rt::FindResource(mod, rt::ResId::Num(rt::RT_MENU), id, d)) return 0;
    return LoadMenuFromMemory(rt::ResourcePtr(mod, d), d.size);
}

namespace {

// Следующий элемент для Tab/стрелок
std::vector<uint32_t> Controls(uint32_t dlg) {
    std::vector<uint32_t> v;
    if (Window* d = Win(dlg)) for (uint32_t c : d->children) v.push_back(c);
    std::reverse(v.begin(), v.end()); // порядок создания: дети хранятся сверху вниз
    return v;
}
bool Usable(uint32_t h) {
    Window* w = Win(h);
    return w && (w->style & WS_VISIBLE) && !(w->style & WS_DISABLED);
}

uint32_t NextTab(uint32_t dlg, uint32_t from, bool prev) {
    std::vector<uint32_t> v = Controls(dlg);
    if (v.empty()) return 0;
    int n = static_cast<int>(v.size());
    int start = static_cast<int>(std::find(v.begin(), v.end(), from) - v.begin());
    if (start >= n) start = prev ? 0 : n - 1;
    for (int k = 1; k <= n; ++k) {
        int i = ((start + (prev ? -k : k)) % n + n) % n;
        Window* w = Win(v[static_cast<size_t>(i)]);
        if (w && Usable(v[static_cast<size_t>(i)]) && (w->style & WS_TABSTOP)) return v[static_cast<size_t>(i)];
    }
    return from;
}

uint32_t NextGroup(uint32_t dlg, uint32_t from, bool prev) {
    std::vector<uint32_t> v = Controls(dlg);
    int n = static_cast<int>(v.size());
    int i = static_cast<int>(std::find(v.begin(), v.end(), from) - v.begin());
    if (i >= n) return from;
    // границы группы: от элемента с WS_GROUP до следующего с WS_GROUP
    int gs = i;
    while (gs > 0 && !(Win(v[static_cast<size_t>(gs)])->style & WS_GROUP)) --gs;
    int ge = i + 1;
    while (ge < n && !(Win(v[static_cast<size_t>(ge)])->style & WS_GROUP)) ++ge;
    int len = ge - gs;
    for (int k = 1; k < len; ++k) {
        int j = gs + (((i - gs) + (prev ? -k : k)) % len + len) % len;
        if (Usable(v[static_cast<size_t>(j)])) return v[static_cast<size_t>(j)];
    }
    return from;
}

uint32_t DefButton(uint32_t dlg) {
    DialogInfo* di = Dlg(dlg);
    uint32_t id = di ? di->defId : 1;
    for (uint32_t c : Controls(dlg)) if (Window* w = Win(c)) if ((w->id & 0xFFFF) == id) return c;
    return 0;
}

void Command(uint32_t dlg, uint32_t id, uint32_t ctl, uint32_t code = 0) {
    SendMsg(dlg, WM_COMMAND, (id & 0xFFFF) | (code << 16), ctl);
}

} // namespace

// DefDlgProc: сначала процедура программы, затем поведение по умолчанию
uint32_t DefDlgProc(uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp, bool unicode) {
    DialogInfo* di = Dlg(hwnd);
    if (!di && g_pendingInfo) di = g_pendingInfo; // создание: WM_NCCREATE, WM_CREATE
    Window* w = Win(hwnd);
    if (!w) return 0;
    if (di && di->dlgProc) {
        uint32_t proc = di->dlgProc;
        bool pu = di->unicode;
        uint32_t r = CallProc(proc, pu, hwnd, msg, wp, lp, unicode);
        if (!(w = Win(hwnd))) return r;
        switch (msg) {
        case WM_CTLCOLORMSGBOX: case WM_CTLCOLOREDIT: case WM_CTLCOLORLISTBOX: case WM_CTLCOLORBTN:
        case WM_CTLCOLORDLG: case WM_CTLCOLORSCROLLBAR: case WM_CTLCOLORSTATIC: case 0x39 /* WM_COMPAREITEM */:
        case 0x2E /* WM_VKEYTOITEM */: case 0x2F /* WM_CHARTOITEM */: case 0x37 /* WM_QUERYDRAGICON */: case WM_INITDIALOG:
            if (r) return r;
            break;
        default:
            if (r) {
                uint32_t res = 0;
                if (w->extra.size() >= 4) std::memcpy(&res, w->extra.data(), 4); // DWL_MSGRESULT
                return res;
            }
        }
        di = Dlg(hwnd);
    }
    switch (msg) {
    case WM_ERASEBKGND: {
        uint32_t brush = SendMsg(hwnd, WM_CTLCOLORDLG, wp, hwnd);
        if (!brush) brush = SysColorBrush(15);
        ui::g_gdi.FillRect(wp, Rect(0, 0, w->client.Width(), w->client.Height()), brush);
        return 1;
    }
    case WM_CLOSE:
        if (uint32_t cancel = 0; true) {
            for (uint32_t c : Controls(hwnd)) if (Window* x = Win(c)) if ((x->id & 0xFFFF) == 2) cancel = c;
            if (!cancel || Usable(cancel)) Command(hwnd, 2, cancel);
        }
        return 0;
    case WM_SETFONT:
        if (di) di->font = wp;
        return 0;
    case WM_GETFONT:
        return di ? di->font : 0;
    case DM_GETDEFID:
        return di ? (0x534B0000u | di->defId) : 0;
    case DM_SETDEFID: {
        if (!di) return 0;
        uint32_t old = DefButton(hwnd);
        di->defId = wp;
        uint32_t now = DefButton(hwnd);
        if (old && old != now) SendMsg(old, 0xF4 /* BM_SETSTYLE */, 0, 1);
        if (now) SendMsg(now, 0xF4, 1, 1);
        return 1;
    }
    case WM_NEXTDLGCTL: {
        uint32_t target = lp ? wp : NextTab(hwnd, S().focus, wp != 0);
        if (target) SetFocusInternal(target);
        return 0;
    }
    case WM_ACTIVATE:
        if ((wp & 0xFFFF) && di && di->lastFocus && Win(di->lastFocus)) SetFocusInternal(di->lastFocus);
        else if (!(wp & 0xFFFF) && di) di->lastFocus = S().focus;
        return 0;
    case WM_SETFOCUS: {
        uint32_t t = NextTab(hwnd, 0, false);
        if (t) SetFocusInternal(t);
        return 0;
    }
    case WM_SHOWWINDOW:
        return DefWindowProc(hwnd, msg, wp, lp, unicode);
    case WM_NCDESTROY:
        Dialogs().erase(hwnd);
        return DefWindowProc(hwnd, msg, wp, lp, unicode);
    }
    return DefWindowProc(hwnd, msg, wp, lp, unicode);
}

uint32_t DefDlgProcAddress(bool unicode) {
    static uint32_t a = 0, w = 0;
    if (!a) {
        int ia = FindHostFunction("USER32.dll", "DefDlgProcA"), iw = FindHostFunction("USER32.dll", "DefDlgProcW");
        a = ia >= 0 ? IndexToHostAddress(ia) : 0;
        w = iw >= 0 ? IndexToHostAddress(iw) : 0;
        if (a) RegisterHostWndProc(a, &DefDlgProc, false);
        if (w) RegisterHostWndProc(w, &DefDlgProc, true);
    }
    return unicode ? w : a;
}

// Клавиатура в диалоге. true — сообщение обработано.
bool DialogMessage(uint32_t dlg, Msg& msg) {
    Window* d = Win(dlg);
    if (!d) return false;
    // сообщение для самого диалога или его потомка?
    bool ours = msg.hwnd == dlg;
    for (Window* x = Win(msg.hwnd); x && !ours && (x->style & WS_CHILD); x = Win(x->parent)) if (x->parent == dlg) ours = true;
    if (!ours) return false;
    if (msg.message == WM_KEYDOWN || msg.message == WM_CHAR || msg.message == WM_SYSCHAR) {
        uint32_t code = msg.hwnd ? SendMsg(msg.hwnd, 0x87 /* WM_GETDLGCODE */, msg.wParam, 0) : 0;
        if (msg.message == WM_KEYDOWN) {
            switch (msg.wParam) {
            case 0x09: // Tab
                if (!(code & (DLGC_WANTTAB | DLGC_WANTALLKEYS))) {
                    bool shift = (S().keys[0x10] & 0x80) != 0;
                    uint32_t t = NextTab(dlg, S().focus, shift);
                    if (t) {
                        SetFocusInternal(t);
                        if (SendMsg(t, 0x87, 0, 0) & DLGC_HASSETSEL) SendMsg(t, 0xB1 /* EM_SETSEL */, 0, 0xFFFFFFFFu);
                    }
                    return true;
                }
                break;
            case 0x25: case 0x26: case 0x27: case 0x28: // стрелки — по группе
                if (!(code & (DLGC_WANTARROWS | DLGC_WANTALLKEYS))) {
                    uint32_t t = NextGroup(dlg, S().focus, msg.wParam == 0x25 || msg.wParam == 0x26);
                    if (t && t != S().focus) {
                        SetFocusInternal(t);
                        if (SendMsg(t, 0x87, 0, 0) & DLGC_RADIOBUTTON) SendMsg(t, 0xF5 /* BM_CLICK */, 0, 0);
                    }
                    return true;
                }
                break;
            case 0x0D: // Enter
                if (!(code & DLGC_WANTALLKEYS)) {
                    uint32_t f = S().focus;
                    uint32_t fc = f ? SendMsg(f, 0x87, 0, 0) : 0;
                    if (fc & DLGC_DEFPUSHBUTTON) { Command(dlg, Win(f)->id, f); return true; }
                    if (fc & DLGC_UNDEFPUSHBUTTON) { Command(dlg, Win(f)->id, f); return true; }
                    uint32_t def = DefButton(dlg);
                    if (def && !Usable(def)) return true;
                    DialogInfo* di = Dlg(dlg);
                    Command(dlg, di ? di->defId : 1, def);
                    return true;
                }
                break;
            case 0x1B: // Esc
                if (!(code & DLGC_WANTALLKEYS)) {
                    uint32_t cancel = 0;
                    for (uint32_t c : Controls(dlg)) if (Window* x = Win(c)) if ((x->id & 0xFFFF) == 2) cancel = c;
                    if (!cancel || Usable(cancel)) Command(dlg, 2, cancel);
                    return true;
                }
                break;
            }
        }
        if ((msg.message == WM_CHAR && !(code & (DLGC_WANTCHARS | DLGC_WANTALLKEYS))) || msg.message == WM_SYSCHAR) {
            if (msg.message == WM_CHAR && (msg.wParam == 9 || msg.wParam == 13 || msg.wParam == 27)) return true;
            // мнемоника &X: ближайший элемент с таким символом
            char16_t want = static_cast<char16_t>(msg.wParam);
            if (want >= 'a' && want <= 'z') want = static_cast<char16_t>(want - 32);
            std::vector<uint32_t> v = Controls(dlg);
            for (size_t i = 0; i < v.size(); ++i) {
                Window* x = Win(v[i]);
                if (!x || !Usable(v[i])) continue;
                size_t amp = x->text.find(u'&');
                if (amp == std::u16string::npos || amp + 1 >= x->text.size()) continue;
                char16_t ch = x->text[amp + 1];
                if (ch >= 'a' && ch <= 'z') ch = static_cast<char16_t>(ch - 32);
                if (ch != want) continue;
                uint32_t code2 = SendMsg(v[i], 0x87, 0, 0);
                if (code2 & DLGC_STATIC) { // надпись — фокус следующему элементу
                    for (size_t k = i + 1; k < v.size(); ++k) if (Usable(v[k]) && (Win(v[k])->style & WS_TABSTOP)) { SetFocusInternal(v[k]); break; }
                } else if (code2 & DLGC_BUTTON) {
                    SetFocusInternal(v[i]);
                    SendMsg(v[i], 0xF5, 0, 0);
                } else {
                    SetFocusInternal(v[i]);
                }
                return true;
            }
            if (msg.message == WM_SYSCHAR) return false;
        }
    }
    TranslateKey(msg);
    SendMsg(msg.hwnd, msg.message, msg.wParam, msg.lParam);
    return true;
}

namespace {

uint32_t RunModal(uint32_t dlg, uint32_t owner) {
    Window* ow = Win(owner);
    bool ownerWasEnabled = ow && !(ow->style & WS_DISABLED);
    if (ownerWasEnabled) { ow->style |= WS_DISABLED; SendMsg(owner, WM_ENABLE, 0, 0); }
    if (Window* d = Win(dlg); d && !(d->style & WS_VISIBLE)) {
        DialogInfo* di = Dlg(dlg);
        if (di && !di->ended) ShowWindowInternal(dlg, 5);
    }
    while (DialogInfo* di = Dlg(dlg)) {
        if (di->ended || g_guestExitRequested) break;
        Msg msg;
        FetchMessage(msg, 0, 0, 0, true, true);
        if (msg.message == WM_QUIT) { S().quit = true; S().quitCode = msg.wParam; break; }
        if (DialogMessage(dlg, msg)) continue;
        if (msg.message == WM_TIMER && msg.lParam) { CallStdcall(*G().c, *G().m, msg.lParam, {msg.hwnd, WM_TIMER, msg.wParam, NowMs()}); continue; }
        TranslateKey(msg);
        if (msg.hwnd) SendMsg(msg.hwnd, msg.message, msg.wParam, msg.lParam);
    }
    uint32_t result = Dlg(dlg) ? Dlg(dlg)->result : 0;
    if ((ow = Win(owner)) && ownerWasEnabled) { ow->style &= ~WS_DISABLED; SendMsg(owner, WM_ENABLE, 1, 0); }
    DestroyWindowInternal(dlg);
    if (Win(owner) && !(Win(owner)->style & WS_CHILD)) SetActive(owner);
    return result;
}

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

uint32_t OwnerRoot(uint32_t owner) {
    Window* w = Win(owner);
    while (w && (w->style & WS_CHILD) && Win(w->parent)) w = Win(w->parent);
    return w ? w->hwnd : 0;
}

bool LoadTemplate(uint32_t hInst, uint32_t name, bool unicode, DlgTemplate& t) {
    rt::Module* mod = ModuleOf(hInst);
    rt::ResourceData d;
    if (!rt::FindResource(mod, rt::ResId::Num(rt::RT_DIALOG), ResName(name, unicode), d)) return false;
    return ParseTemplate(rt::ResourcePtr(mod, d), d.size, t);
}
bool TemplateFromMemory(cpu::IGuestMemory& m, uint32_t p, DlgTemplate& t) {
    std::vector<uint8_t> buf(65536);
    m.ReadBytes(p, buf.data(), buf.size());
    return ParseTemplate(buf.data(), buf.size(), t);
}

HostCallOutcome DialogBoxParamImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool unicode) {
    DlgTemplate t;
    if (!LoadTemplate(A(c, m, 0), A(c, m, 1), unicode, t)) { SetLastError(m, 1812); return Ret(0xFFFFFFFFu); }
    uint32_t owner = OwnerRoot(A(c, m, 2));
    t.style &= ~0x40000000u; // модальный диалог — всегда окно верхнего уровня
    uint32_t dlg = CreateFromTemplate(t, A(c, m, 0), owner, A(c, m, 3), A(c, m, 4), unicode);
    if (!dlg) return Ret(0xFFFFFFFFu);
    return Ret(RunModal(dlg, owner));
}
AW(DialogBoxParam)
HostCallOutcome DialogBoxIndirectParamImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool unicode) {
    DlgTemplate t;
    TemplateFromMemory(m, A(c, m, 1), t);
    uint32_t owner = OwnerRoot(A(c, m, 2));
    t.style &= ~0x40000000u;
    uint32_t dlg = CreateFromTemplate(t, A(c, m, 0), owner, A(c, m, 3), A(c, m, 4), unicode);
    if (!dlg) return Ret(0xFFFFFFFFu);
    return Ret(RunModal(dlg, owner));
}
AW(DialogBoxIndirectParam)
HostCallOutcome CreateDialogParamImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool unicode) {
    DlgTemplate t;
    if (!LoadTemplate(A(c, m, 0), A(c, m, 1), unicode, t)) { SetLastError(m, 1812); return Ret(0); }
    return Ret(CreateFromTemplate(t, A(c, m, 0), A(c, m, 2), A(c, m, 3), A(c, m, 4), unicode));
}
AW(CreateDialogParam)
HostCallOutcome CreateDialogIndirectParamImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool unicode) {
    DlgTemplate t;
    TemplateFromMemory(m, A(c, m, 1), t);
    return Ret(CreateFromTemplate(t, A(c, m, 0), A(c, m, 2), A(c, m, 3), A(c, m, 4), unicode));
}
AW(CreateDialogIndirectParam)
FN(EndDialog) {
    ENTER;
    DialogInfo* di = Dlg(A(c, m, 0));
    if (!di) return Ret(0);
    di->ended = true;
    di->result = A(c, m, 1);
    ShowWindowInternal(A(c, m, 0), 0);
    return Ret(1);
}
HostCallOutcome DefDlgProcImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool unicode) {
    return Ret(DefDlgProc(A(c, m, 0), A(c, m, 1), A(c, m, 2), A(c, m, 3), unicode));
}
AW(DefDlgProc)
HostCallOutcome IsDialogMessageImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool) {
    uint32_t p = A(c, m, 1);
    Msg msg;
    msg.hwnd = m.Read32(p);
    msg.message = m.Read32(p + 4);
    msg.wParam = m.Read32(p + 8);
    msg.lParam = m.Read32(p + 12);
    return Ret(DialogMessage(A(c, m, 0), msg) ? 1 : 0);
}
AW(IsDialogMessage)

uint32_t Item(cpu::CpuState& c, cpu::IGuestMemory& m) {
    Window* d = Win(A(c, m, 0));
    if (!d) return 0;
    uint32_t id = A(c, m, 1) & 0xFFFF;
    for (uint32_t ch : d->children) if (Window* x = Win(ch)) if ((x->id & 0xFFFF) == id) return ch;
    return 0;
}
HostCallOutcome SetDlgItemTextImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool unicode) {
    uint32_t h = Item(c, m);
    return Ret(h ? SendMsg(h, WM_SETTEXT, 0, A(c, m, 2), unicode) : 0);
}
AW(SetDlgItemText)
HostCallOutcome GetDlgItemTextImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool unicode) {
    uint32_t h = Item(c, m);
    if (!h) { if (A(c, m, 2) && A(c, m, 3)) WriteText(A(c, m, 2), u"", 1, unicode); return Ret(0); }
    return Ret(SendMsg(h, WM_GETTEXT, A(c, m, 3), A(c, m, 2), unicode));
}
AW(GetDlgItemText)
FN(SetDlgItemInt) {
    ENTER;
    uint32_t h = Item(c, m);
    if (!h) return Ret(0);
    std::string s = A(c, m, 3) ? std::to_string(static_cast<int32_t>(A(c, m, 2))) : std::to_string(A(c, m, 2));
    StackBuffer buf(static_cast<uint32_t>(s.size() * 2 + 4));
    WriteText(buf.addr(), AnsiToWide(s), static_cast<uint32_t>(s.size() + 1), true);
    SendMsg(h, WM_SETTEXT, 0, buf.addr(), true);
    return Ret(1);
}
FN(GetDlgItemInt) {
    ENTER;
    uint32_t h = Item(c, m), okP = A(c, m, 2);
    bool isSigned = A(c, m, 3) != 0;
    if (okP) m.Write32(okP, 0);
    Window* w = Win(h);
    if (!w) return Ret(0);
    std::u16string t;
    {
        StackBuffer buf(512);
        uint32_t n = SendMsg(h, WM_GETTEXT, 255, buf.addr(), true);
        t = ReadWide(m, buf.addr(), n);
    }
    size_t i = 0;
    while (i < t.size() && t[i] == ' ') ++i;
    bool neg = false;
    if (i < t.size() && t[i] == '-' && isSigned) { neg = true; ++i; }
    if (i >= t.size() || t[i] < '0' || t[i] > '9') return Ret(0);
    uint64_t v = 0;
    for (; i < t.size() && t[i] >= '0' && t[i] <= '9'; ++i) { v = v * 10 + (t[i] - '0'); if (v > 0xFFFFFFFFull) return Ret(0); }
    while (i < t.size() && t[i] == ' ') ++i;
    if (i != t.size()) return Ret(0);
    if (okP) m.Write32(okP, 1);
    return Ret(neg ? static_cast<uint32_t>(-static_cast<int64_t>(v)) : static_cast<uint32_t>(v));
}
HostCallOutcome SendDlgItemMessageImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool unicode) {
    uint32_t h = Item(c, m);
    return Ret(h ? SendMsg(h, A(c, m, 2), A(c, m, 3), A(c, m, 4), unicode) : 0);
}
AW(SendDlgItemMessage)
FN(CheckDlgButton) { ENTER; uint32_t h = Item(c, m); if (h) SendMsg(h, 0xF1, A(c, m, 2), 0); return Ret(h ? 1 : 0); }
FN(IsDlgButtonChecked) { ENTER; uint32_t h = Item(c, m); return Ret(h ? SendMsg(h, 0xF0, 0, 0) : 0); }
FN(CheckRadioButton) {
    ENTER;
    Window* d = Win(A(c, m, 0));
    if (!d) return Ret(0);
    uint32_t first = A(c, m, 1), last = A(c, m, 2), check = A(c, m, 3);
    for (uint32_t ch : d->children)
        if (Window* x = Win(ch)) {
            uint32_t id = x->id & 0xFFFF;
            if (id >= first && id <= last) SendMsg(ch, 0xF1, id == check ? 1 : 0, 0);
        }
    return Ret(1);
}
FN(GetNextDlgTabItem) { ENTER; return Ret(NextTab(A(c, m, 0), A(c, m, 1), A(c, m, 2) != 0)); }
FN(GetNextDlgGroupItem) { ENTER; return Ret(NextGroup(A(c, m, 0), A(c, m, 1), A(c, m, 2) != 0)); }
FN(MapDialogRect) {
    ENTER;
    DialogInfo* di = Dlg(A(c, m, 0));
    int bx = di ? di->baseX : 6, by = di ? di->baseY : 13;
    uint32_t p = A(c, m, 1);
    m.Write32(p, static_cast<uint32_t>(MulDiv(static_cast<int32_t>(m.Read32(p)), bx, 4)));
    m.Write32(p + 4, static_cast<uint32_t>(MulDiv(static_cast<int32_t>(m.Read32(p + 4)), by, 8)));
    m.Write32(p + 8, static_cast<uint32_t>(MulDiv(static_cast<int32_t>(m.Read32(p + 8)), bx, 4)));
    m.Write32(p + 12, static_cast<uint32_t>(MulDiv(static_cast<int32_t>(m.Read32(p + 12)), by, 8)));
    return Ret(1);
}
FN(GetDialogBaseUnits) { ENTER; return Ret(8 | (16 << 16)); }
FN(WinHelpA) { ENTER; return Ret(1); }

#define E(name, bytes) {"USER32.dll", #name, bytes, CallConv::Stdcall, &name}
#define EAW(name, bytes) E(name##A, bytes), E(name##W, bytes)
const HostFunctionEntry kEntries[] = {
    EAW(DialogBoxParam, 20), EAW(DialogBoxIndirectParam, 20), EAW(CreateDialogParam, 20), EAW(CreateDialogIndirectParam, 20),
    E(EndDialog, 8), EAW(DefDlgProc, 16), EAW(IsDialogMessage, 8), EAW(SetDlgItemText, 12), EAW(GetDlgItemText, 16),
    E(SetDlgItemInt, 16), E(GetDlgItemInt, 16), EAW(SendDlgItemMessage, 20), E(CheckDlgButton, 12), E(IsDlgButtonChecked, 8),
    E(CheckRadioButton, 16), E(GetNextDlgTabItem, 12), E(GetNextDlgGroupItem, 12), E(MapDialogRect, 8), E(GetDialogBaseUnits, 0),
    E(WinHelpA, 16), {"USER32.dll", "WinHelpW", 16, CallConv::Stdcall, &WinHelpA},
};
const bool kRegistered = RegisterHostFunctionsLayer(kEntries, sizeof(kEntries) / sizeof(kEntries[0]), kUiLayer);

} // namespace

} // namespace user
} // namespace win32
