// comdlg32_module.cpp — basic Windows XP-style common file dialogs for ui2.
// Implements OPENFILENAMEA/W, guest VFS browsing, cancellation and extended errors.
// No Paint-specific offsets or UI assumptions.
#include <algorithm>
#include <cctype>
#include <cstring>
#include <dirent.h>
#include <map>
#include <string>
#include <sys/stat.h>
#include <vector>
#include "../runtime/files.h"
#include "user32_controls.h"

namespace win32 {
namespace {
using namespace hle;
using namespace user;

constexpr uint32_t FNERR_BUFFERTOOSMALL = 0x3003;
constexpr uint32_t CDERR_STRUCTSIZE = 0x0001;
constexpr uint32_t CDERR_INITIALIZATION = 0x0002;
constexpr uint32_t OFN_FILEMUSTEXIST = 0x1000;
constexpr uint32_t OFN_PATHMUSTEXIST = 0x0800;
constexpr uint32_t OFN_OVERWRITEPROMPT = 0x0002;
constexpr uint32_t OFN_NOCHANGEDIR = 0x0008;
constexpr uint32_t WS_CHILD_VISIBLE = WS_CHILD | WS_VISIBLE;
constexpr uint32_t BTN_ID_OK = 1001, BTN_ID_CANCEL = 1002, LIST_ID = 1003, EDIT_ID = 1004;
uint32_t g_extendedError = 0;

struct Request {
    cpu::IGuestMemory* memory = nullptr;
    uint32_t ofn = 0, owner = 0, filePtr = 0, maxFile = 0, flags = 0;
    uint32_t fileTitlePtr = 0, maxFileTitle = 0, defExtPtr = 0;
    bool wide = false, save = false, done = false, accepted = false;
    uint32_t hwnd = 0, list = 0, edit = 0;
    std::string directory;
    std::u16string selected;
    std::vector<std::string> listNames;
};
std::map<uint32_t, Request*>& Dialogs() {
    static std::map<uint32_t, Request*> dialogs;
    return dialogs;
}
std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
bool Exists(const std::string& path, bool directory) {
    struct stat st;
    return stat(rt::FileSystem().ToHost(path).c_str(), &st) == 0 &&
           (directory ? S_ISDIR(st.st_mode) : S_ISREG(st.st_mode));
}
std::string NormalizeSlashes(std::string s) {
    for (char& c : s) if (c == '/') c = '\\';
    return s;
}
std::string Join(const std::string& dir, const std::string& item) {
    if (item.size() >= 2 && item[1] == ':') return rt::FileSystem().FullGuestPath(item);
    if (!item.empty() && (item[0] == '\\' || item[0] == '/')) return rt::FileSystem().FullGuestPath(item);
    return rt::FileSystem().FullGuestPath(dir + "\\" + item);
}
std::u16string Text(const std::string& a) { return AnsiToWide(a); }
std::string Narrow(const std::u16string& s) { return WideToUtf8(s); }

void UpdateList(Request& r) {
    Window* w = Win(r.list);
    if (!w) return;
    Ctl* c = C(w);
    c->items.clear(); c->cur = -1; c->top = 0;
    r.listNames.clear();
    // ".." is the parent-folder entry. Use the VFS for all paths.
    if (r.directory.size() > 3) {
        c->items.push_back({u"[..]", 0, false, 0});
        r.listNames.push_back("..");
    }
    const std::string host = rt::FileSystem().ToHost(r.directory);
    DIR* d = opendir(host.c_str());
    if (d) {
        std::vector<std::pair<std::string, bool>> files;
        while (dirent* e = readdir(d)) {
            std::string name(e->d_name);
            if (name == "." || name == "..") continue;
            struct stat st;
            if (stat((host + "/" + name).c_str(), &st) != 0) continue;
            if (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode)) continue;
            files.push_back({name, S_ISDIR(st.st_mode)});
        }
        closedir(d);
        std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) {
            if (a.second != b.second) return a.second > b.second;
            return Lower(a.first) < Lower(b.first);
        });
        for (const auto& f : files) {
            c->items.push_back({(f.second ? u"[" : u"") + Text(f.first) + (f.second ? u"]" : u""), 0, false, 0});
            r.listNames.push_back(f.first);
        }
    }
    if (Window* top = Win(r.hwnd)) top->text = (r.save ? u"Save As — " : u"Open — ") + Text(r.directory);
    Redraw(r.list);
}
bool PutResult(Request& r, std::string full) {
    auto& m = *r.memory;
    full = NormalizeSlashes(full);
    const std::u16string wfull = Text(full);
    const uint32_t len = r.wide ? static_cast<uint32_t>(wfull.size()) : static_cast<uint32_t>(full.size());
    if (len + 1 > r.maxFile) {
        // OFN contract: required character count in first WORD of lpstrFile.
        if (r.maxFile >= 2 && r.filePtr) m.Write16(r.filePtr, static_cast<uint16_t>(std::min<uint32_t>(len + 1, 0xFFFF)));
        g_extendedError = FNERR_BUFFERTOOSMALL;
        return false;
    }
    if (r.wide) WriteWide(m, r.filePtr, wfull, r.maxFile);
    else WriteAnsi(m, r.filePtr, full, r.maxFile);
    size_t slash = full.find_last_of("\\/");
    std::string title = slash == std::string::npos ? full : full.substr(slash + 1);
    size_t dot = title.find_last_of('.');
    uint32_t base = slash == std::string::npos ? 0u : static_cast<uint32_t>(slash + 1);
    // Offset in characters (not bytes), including for W.
    m.Write16(r.ofn + 56, static_cast<uint16_t>(std::min<uint32_t>(base, 0xFFFF)));
    m.Write16(r.ofn + 58, static_cast<uint16_t>(dot == std::string::npos ? 0 : std::min<uint32_t>(base + dot + 1, 0xFFFF)));
    if (r.fileTitlePtr && r.maxFileTitle) {
        if (r.wide) WriteWide(m, r.fileTitlePtr, Text(title), r.maxFileTitle);
        else WriteAnsi(m, r.fileTitlePtr, title, r.maxFileTitle);
    }
    return true;
}
void Accept(Request& r) {
    Window* edit = Win(r.edit);
    std::string chosen = edit ? Narrow(edit->text) : "";
    if (chosen.empty()) return;
    std::string full = Join(r.directory, chosen);
    if (Exists(full, true)) {
        r.directory = full;
        if (edit) edit->text.clear();
        UpdateList(r);
        return;
    }
    if (r.save && r.defExtPtr) {
        std::string ext = r.wide ? Narrow(ReadWide(*r.memory, r.defExtPtr)) : ReadAnsi(*r.memory, r.defExtPtr);
        std::string name = full.substr(full.find_last_of("\\/") + 1);
        if (!ext.empty() && name.find('.') == std::string::npos) full += "." + ext;
    }
    if (!r.save && (r.flags & OFN_FILEMUSTEXIST) && !Exists(full, false)) return;
    if ((r.flags & OFN_PATHMUSTEXIST) && !Exists(full.substr(0, full.find_last_of("\\/")), true)) return;
    // OVERWRITEPROMPT is deferred until the confirmation UI is implemented:
    // refuse overwriting instead of silently destroying files.
    if (r.save && (r.flags & OFN_OVERWRITEPROMPT) && Exists(full, false)) return;
    r.accepted = PutResult(r, full);
    if (r.accepted || g_extendedError) r.done = true;
}
uint32_t DialogProc(uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t, bool) {
    auto it = Dialogs().find(hwnd);
    if (it == Dialogs().end()) return DefWindowProc(hwnd, msg, wp, 0, true);
    Request& r = *it->second;
    if (msg == user::WM_CLOSE) { r.done = true; return 0; }
    if (msg == user::WM_COMMAND) {
        const uint32_t id = wp & 0xFFFF, code = wp >> 16;
        if (id == BTN_ID_CANCEL) { r.done = true; return 0; }
        if (id == BTN_ID_OK) { Accept(r); return 0; }
        if (id == LIST_ID && (code == 1 || code == 2)) {
            Window* list = Win(r.list);
            int selected = list ? C(list)->cur : -1;
            if (selected >= 0 && static_cast<size_t>(selected) < r.listNames.size()) {
                std::string path = Join(r.directory, r.listNames[static_cast<size_t>(selected)]);
                if (Exists(path, true)) {
                    if (code == 2) { r.directory = path; UpdateList(r); }
                    else if (Window* edit = Win(r.edit)) { edit->text = Text(r.listNames[static_cast<size_t>(selected)]); Redraw(r.edit); }
                } else if (Window* edit = Win(r.edit)) {
                    edit->text = Text(r.listNames[static_cast<size_t>(selected)]); Redraw(r.edit);
                    if (code == 2) Accept(r);
                }
            }
            return 0;
        }
    }
    return DefWindowProc(hwnd, msg, wp, 0, true);
}
HostCallOutcome HostDialogProc(cpu::CpuState& c, cpu::IGuestMemory& m) {
    return Ret(DialogProc(Arg(c,m,0), Arg(c,m,1), Arg(c,m,2), Arg(c,m,3), true));
}
uint32_t AddChild(Request& r, const char16_t* cls, const char16_t* title, uint32_t id,
                  int x, int y, int w, int h, uint32_t extraStyle = 0) {
    return CreateWindowInternal(0, cls, 0, title, WS_CHILD_VISIBLE | extraStyle,
                                x, y, w, h, r.hwnd, id, 0, 0, true, 0, 0);
}
bool ShowDialog(Request& r) {
    int procIndex = FindHostFunction("COMDLG32.dll", "__FileDialogWndProc");
    uint32_t addr = procIndex >= 0 ? IndexToHostAddress(procIndex) : 0;
    if (!addr) return false;
    RegisterHostWndProc(addr, &DialogProc, true);
    WndClass wc;
    wc.name = u"SwitchCommonFileDialog";
    wc.wndProc = addr;
    wc.unicode = true;
    wc.hbrBackground = SysColorBrush(15);
    RegisterClassInternal(wc);
    int width = 530, height = 390;
    int x = std::max(0, (S().screenW - width) / 2), y = std::max(0, (S().screenH - height) / 2);
    r.hwnd = CreateWindowInternal(WS_EX_DLGMODALFRAME, wc.name, 0,
             r.save ? u"Save As" : u"Open",
             WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, width, height,
             r.owner, 0, 0, 0, true, 0, 0);
    if (!r.hwnd) return false;
    Dialogs()[r.hwnd] = &r;
    AddChild(r, u"Static", u"Files and folders:", 1010, 12, 12, 380, 22);
    r.list = AddChild(r, u"ListBox", u"", LIST_ID, 12, 36, 490, 240, WS_VSCROLL | WS_BORDER | 0x0001);
    AddChild(r, u"Static", u"File name:", 1011, 12, 292, 90, 22);
    r.edit = AddChild(r, u"Edit", u"", EDIT_ID, 102, 289, 276, 26, WS_BORDER | 0x80);
    AddChild(r, u"Button", r.save ? u"Save" : u"Open", BTN_ID_OK, 385, 289, 116, 27, 0x00010000u | 1);
    AddChild(r, u"Button", u"Cancel", BTN_ID_CANCEL, 385, 322, 116, 27, 0x00010000u);
    UpdateList(r);
    ShowWindowInternal(r.hwnd, 5);
    uint32_t owner = r.owner;
    bool wasEnabled = Win(owner) && !(Win(owner)->style & WS_DISABLED);
    if (wasEnabled) Win(owner)->style |= WS_DISABLED;
    SetFocusInternal(r.list);
    while (!r.done && !g_guestExitRequested) {
        Msg msg;
        if (!FetchMessage(msg, 0, 0, 0, true, true)) break;
        if (msg.message == user::WM_QUIT) { S().quit = true; S().quitCode = msg.wParam; break; }
        if (msg.message == WM_KEYDOWN && msg.wParam == 27) { r.done = true; break; }
        if (msg.message == user::WM_TIMER && msg.lParam) {
            CallStdcall(*G().c, *G().m, msg.lParam, {msg.hwnd, user::WM_TIMER, msg.wParam, NowMs()});
            continue;
        }
        TranslateKey(msg);
        if (msg.hwnd) SendMsg(msg.hwnd, msg.message, msg.wParam, msg.lParam);
    }
    if (wasEnabled && Win(owner)) Win(owner)->style &= ~WS_DISABLED;
    Dialogs().erase(r.hwnd);
    DestroyWindowInternal(r.hwnd);
    if (Win(owner)) SetActive(owner);
    return true;
}
HostCallOutcome FileDialog(cpu::CpuState& c, cpu::IGuestMemory& m, bool wide, bool save) {
    g_extendedError = 0;
    uint32_t p = Arg(c, m, 0);
    if (!p) { g_extendedError = CDERR_INITIALIZATION; return Ret(0); }
    uint32_t sz = m.Read32(p);
    if (sz != 76 && sz != 88) { g_extendedError = CDERR_STRUCTSIZE; return Ret(0); }
    Request r;
    r.memory = &m; r.ofn = p; r.wide = wide; r.save = save;
    r.owner = m.Read32(p + 4);
    r.filePtr = m.Read32(p + 28);
    r.maxFile = m.Read32(p + 32);
    r.fileTitlePtr = m.Read32(p + 36);
    r.maxFileTitle = m.Read32(p + 40);
    r.defExtPtr = m.Read32(p + 60);
    r.flags = m.Read32(p + 52);
    if (!r.filePtr || !r.maxFile) { g_extendedError = CDERR_INITIALIZATION; return Ret(0); }
    std::string initial = wide ? Narrow(ReadWide(m, m.Read32(p + 44))) : ReadAnsi(m, m.Read32(p + 44));
    r.directory = initial.empty() ? rt::FileSystem().CurrentDirectory() : rt::FileSystem().FullGuestPath(initial);
    if (!Exists(r.directory, true)) r.directory = rt::FileSystem().CurrentDirectory();
    std::string file = wide ? Narrow(ReadWide(m, r.filePtr, r.maxFile)) : ReadAnsi(m, r.filePtr, r.maxFile);
    if (!file.empty()) {
        size_t slash = file.find_last_of("\\/");
        if (slash != std::string::npos) {
            std::string parent = Join(r.directory, file.substr(0, slash));
            if (Exists(parent, true)) r.directory = parent;
            file = file.substr(slash + 1);
        }
    }
    G().c = &c; G().m = &m;
    if (!ShowDialog(r)) { g_extendedError = CDERR_INITIALIZATION; return Ret(0); }
    return Ret(r.accepted ? 1 : 0);
}
HostCallOutcome GetOpenFileNameA(cpu::CpuState& c, cpu::IGuestMemory& m) { return FileDialog(c,m,false,false); }
HostCallOutcome GetOpenFileNameW(cpu::CpuState& c, cpu::IGuestMemory& m) { return FileDialog(c,m,true,false); }
HostCallOutcome GetSaveFileNameA(cpu::CpuState& c, cpu::IGuestMemory& m) { return FileDialog(c,m,false,true); }
HostCallOutcome GetSaveFileNameW(cpu::CpuState& c, cpu::IGuestMemory& m) { return FileDialog(c,m,true,true); }
HostCallOutcome CommDlgExtendedError(cpu::CpuState&, cpu::IGuestMemory&) { return Ret(g_extendedError); }
const HostFunctionEntry kEntries[] = {
    {"COMDLG32.dll","GetOpenFileNameA",4,CallConv::Stdcall,&GetOpenFileNameA},
    {"COMDLG32.dll","GetOpenFileNameW",4,CallConv::Stdcall,&GetOpenFileNameW},
    {"COMDLG32.dll","GetSaveFileNameA",4,CallConv::Stdcall,&GetSaveFileNameA},
    {"COMDLG32.dll","GetSaveFileNameW",4,CallConv::Stdcall,&GetSaveFileNameW},
    {"COMDLG32.dll","CommDlgExtendedError",0,CallConv::Stdcall,&CommDlgExtendedError},
    {"COMDLG32.dll","__FileDialogWndProc",16,CallConv::Stdcall,&HostDialogProc},
};
const bool kRegistered = RegisterHostFunctionsLayer(kEntries, sizeof(kEntries)/sizeof(kEntries[0]), kUiLayer);
} // namespace
} // namespace win32
