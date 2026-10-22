// comdlg32_module.cpp — basic Windows XP-style common file dialogs for ui2.
// Implements OPENFILENAMEA/W, guest VFS browsing, cancellation and extended errors.
// No Paint-specific offsets or UI assumptions.
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdarg>
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

// Host-only opt-in trace for scripted regression tests; never touch
// Nintendo Switch's closed text console during graphics rendering.
void Trace(const char* fmt, ...) {
#ifndef __SWITCH__
    if (!std::getenv("SR_COMDLG_TRACE")) return;
    va_list ap;
    va_start(ap, fmt);
    std::fprintf(stderr, "[COMDLG] ");
    std::vfprintf(stderr, fmt, ap);
    std::fprintf(stderr, "\n");
    va_end(ap);
#else
    (void)fmt;
#endif
}


struct Request {
    cpu::IGuestMemory* memory = nullptr;
    uint32_t ofn = 0, owner = 0, filePtr = 0, maxFile = 0, flags = 0;
    uint32_t fileTitlePtr = 0, maxFileTitle = 0, defExtPtr = 0;
    bool wide = false, save = false, done = false, accepted = false;
    uint32_t hwnd = 0, list = 0, edit = 0;
    std::string directory;
    std::string initialFile;
    std::vector<std::string> patterns;
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
// OPENFILENAME filter uses pairs "label\0pattern\0... \0\0".
std::vector<std::string> ReadFilters(cpu::IGuestMemory& m, uint32_t ptr, bool wide) {
    std::vector<std::string> result;
    if (!ptr) return result;
    uint32_t offset = 0;
    auto readPart = [&]() {
        std::u16string part;
        for (uint32_t n = 0; n < 4096 && offset < 16384; ++n, ++offset) {
            uint16_t ch = wide ? m.Read16(ptr + offset * 2) : m.Read8(ptr + offset);
            if (!ch) { ++offset; break; }
            part.push_back(static_cast<char16_t>(ch));
        }
        return WideToUtf8(part);
    };
    for (unsigned int i = 0; i < 64 && offset < 16384; ++i) {
        std::string label = readPart();
        if (label.empty()) break;
        std::string pattern = readPart();
        if (pattern.empty()) break;
        result.push_back(pattern);
    }
    return result;
}
bool MatchExtension(const std::string& name, const std::string& pattern) {
    size_t start = 0;
    while (start < pattern.size()) {
        size_t end = pattern.find(';', start);
        if (end == std::string::npos) end = pattern.size();
        std::string glob = Lower(pattern.substr(start, end - start));
        std::string file = Lower(name);
        if (glob == "*" || glob == "*.*") return true;
        if (glob.size() > 2 && glob[0] == '*' && glob[1] == '.' &&
            file.size() >= glob.size() - 1 &&
            file.compare(file.size() - glob.size() + 1, glob.size() - 1, glob.substr(1)) == 0) return true;
        if (glob == file) return true;
        start = end + 1;
    }
    return false;
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
            if (S_ISREG(st.st_mode) && !r.patterns.empty() &&
                !MatchExtension(name, r.patterns.front())) continue;
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
    Trace("list: directory=%s entries=%zu selected_filter=%s", r.directory.c_str(), r.listNames.size(), r.patterns.empty() ? "(all)" : r.patterns.front().c_str());
    if (Window* top = Win(r.hwnd)) top->text = (r.save ? u"Save As — " : u"Open — ") + Text(r.directory);
    // Recalculate ListBox scroll range after populating its host-owned items.
    SendMsg(r.list, user::WM_SIZE, 0, 0);
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
    Trace("accept: name=%s directory=%s", chosen.c_str(), r.directory.c_str());
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
        Trace("command id=%u code=%u listSel=%d", id, code, Win(r.list) ? C(Win(r.list))->cur : -1);
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
    if (!r.initialFile.empty()) {
        if (Window* initialEdit = Win(r.edit)) {
            initialEdit->text = Text(r.initialFile);
            Redraw(r.edit);
        }
    }
    ShowWindowInternal(r.hwnd, 5);
    uint32_t owner = r.owner;
    bool wasEnabled = Win(owner) && !(Win(owner)->style & WS_DISABLED);
    if (wasEnabled) Win(owner)->style |= WS_DISABLED;
    SetFocusInternal(r.list);
    while (!r.done && !g_guestExitRequested) {
        Msg msg;
        if (!FetchMessage(msg, 0, 0, 0, true, true)) break;
        if (msg.message == user::WM_QUIT) { PostQuit(msg.wParam); break; }
        if (msg.message == user::WM_KEYDOWN) {
            if (msg.wParam == 27) { r.done = true; break; } // Esc
            if (msg.wParam == 13) { Accept(r); continue; } // Enter
            if (msg.wParam == 9) { // Tab: cycle through interactive controls
                uint32_t id = r.list;
                if (Q().focus == r.list) id = r.edit;
                else if (Q().focus == r.edit) id = r.list;
                if (id) SetFocusInternal(id);
                continue;
            }
        }
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
    r.patterns = ReadFilters(m, m.Read32(p + 12), wide);
    uint32_t filterIndex = m.Read32(p + 24);
    if (filterIndex >= 1 && filterIndex <= r.patterns.size())
        std::rotate(r.patterns.begin(), r.patterns.begin() + filterIndex - 1, r.patterns.end());
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
    r.initialFile = file;
    G().c = &c; G().m = &m;
    if (!ShowDialog(r)) { g_extendedError = CDERR_INITIALIZATION; return Ret(0); }
    return Ret(r.accepted ? 1 : 0);
}
HostCallOutcome GetOpenFileNameA(cpu::CpuState& c, cpu::IGuestMemory& m) { return FileDialog(c,m,false,false); }
HostCallOutcome GetOpenFileNameW(cpu::CpuState& c, cpu::IGuestMemory& m) { return FileDialog(c,m,true,false); }
HostCallOutcome GetSaveFileNameA(cpu::CpuState& c, cpu::IGuestMemory& m) { return FileDialog(c,m,false,true); }
HostCallOutcome GetSaveFileNameW(cpu::CpuState& c, cpu::IGuestMemory& m) { return FileDialog(c,m,true,true); }
HostCallOutcome CommDlgExtendedError(cpu::CpuState&, cpu::IGuestMemory&) { return Ret(g_extendedError); }

// short WINAPI GetFileTitleA/W(LPCTSTR path, LPTSTR title, WORD cchSize).
// Unlike GetOpenFileName, GetFileTitle does not open a modal window or access
// the filesystem. It returns 0 on success, required chars on short buffer.
HostCallOutcome FileTitle(cpu::CpuState& c, cpu::IGuestMemory& m, bool wide) {
    const uint32_t input = Arg(c,m,0);
    const uint32_t output = Arg(c,m,1);
    const uint32_t capacity = Arg(c,m,2) & 0xffffu; // WORD in Win32 ABI
    if (!input) return Ret(static_cast<uint32_t>(static_cast<int32_t>(-1)));
    if (wide) {
        std::u16string path = ReadWide(m,input,32768);
        size_t sep = path.find_last_of(u"\\/");
        std::u16string name = path.substr(sep == std::u16string::npos ? 0 : sep + 1);
        if (name.empty() || name.size() >= 0x7fff) return Ret(0xffffffffu);
        uint32_t required = static_cast<uint32_t>(name.size() + 1);
        if (!output || capacity < required) return Ret(required);
        WriteWide(m,output,name,capacity);
    } else {
        std::string path = ReadAnsi(m,input,32768);
        size_t sep = path.find_last_of("\\/");
        std::string name = path.substr(sep == std::string::npos ? 0 : sep + 1);
        if (name.empty() || name.size() >= 0x7fff) return Ret(0xffffffffu);
        uint32_t required = static_cast<uint32_t>(name.size() + 1);
        if (!output || capacity < required) return Ret(required);
        WriteAnsi(m,output,name,capacity);
    }
    return Ret(0);
}
HostCallOutcome GetFileTitleA(cpu::CpuState& c, cpu::IGuestMemory& m) { return FileTitle(c,m,false); }
HostCallOutcome GetFileTitleW(cpu::CpuState& c, cpu::IGuestMemory& m) { return FileTitle(c,m,true); }

const HostFunctionEntry kEntries[] = {
    {"COMDLG32.dll","GetOpenFileNameA",4,CallConv::Stdcall,&GetOpenFileNameA},
    {"COMDLG32.dll","GetOpenFileNameW",4,CallConv::Stdcall,&GetOpenFileNameW},
    {"COMDLG32.dll","GetSaveFileNameA",4,CallConv::Stdcall,&GetSaveFileNameA},
    {"COMDLG32.dll","GetSaveFileNameW",4,CallConv::Stdcall,&GetSaveFileNameW},
    {"COMDLG32.dll","__FileDialogWndProc",16,CallConv::Stdcall,&HostDialogProc},
};
// These exports are part of COMDLG32 regardless of which file-dialog UI is active.
// MSVC/MFC resolves them dynamically via GetProcAddress (delay-load).
const HostFunctionEntry kCoreEntries[] = {
    {"COMDLG32.dll","CommDlgExtendedError",0,CallConv::Stdcall,&CommDlgExtendedError},
    {"COMDLG32.dll","GetFileTitleA",12,CallConv::Stdcall,&GetFileTitleA},
    {"COMDLG32.dll","GetFileTitleW",12,CallConv::Stdcall,&GetFileTitleW},
};
const bool kRegisteredCore = RegisterHostFunctions(kCoreEntries, sizeof(kCoreEntries)/sizeof(kCoreEntries[0]));
const bool kRegistered = RegisterHostFunctionsLayer(kEntries, sizeof(kEntries)/sizeof(kEntries[0]), kUiLayer);
} // namespace
void SetCommonDialogError(uint32_t error) {g_extendedError=error;}
} // namespace win32
