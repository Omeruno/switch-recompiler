// user32_hook.cpp — USER32: хуки (SetWindowsHookEx) нового слоя окон.
//
// Поток один, поэтому хуки потока и глобальные — одна цепочка на тип.
// Вызываются WH_CBT (создание/уничтожение/активация окна) и WH_MSGFILTER;
// остальные типы только хранятся. MFC без HCBT_CREATEWND не привязывает
// CWnd к окну и считает создание неудачным.

#include <algorithm>
#include <vector>

#include "user32.h"

namespace win32 {
namespace user {

using namespace hle;

namespace {

struct Hook {
    uint32_t handle;
    int id;
    uint32_t proc;
    bool removed = false;
};
std::vector<Hook>& Hooks() {
    static std::vector<Hook> h;
    return h;
}
uint32_t g_nextHook = 0x00A00010;
int g_hookDepth = 0;

// Следующий живой хук того же типа после позиции pos (цепочка: новые первыми)
int NextHook(int id, int pos) {
    std::vector<Hook>& h = Hooks();
    for (int i = pos; i < static_cast<int>(h.size()); ++i)
        if (h[i].id == id && !h[i].removed) return i;
    return -1;
}
uint32_t CallHookAt(int i, int code, uint32_t wp, uint32_t lp) {
    uint32_t proc = Hooks()[i].proc;
    ++g_hookDepth;
    uint32_t r = CallGuestFunctionStdcall(*G().c, *G().m, proc, {static_cast<uint32_t>(code), wp, lp}, g_tebAddr, g_tebAddr,
                                          0x7FFFFFFF);
    if (--g_hookDepth == 0) {
        std::vector<Hook>& h = Hooks();
        h.erase(std::remove_if(h.begin(), h.end(), [](const Hook& k) { return k.removed; }), h.end());
    }
    return r;
}

#define FN(name) HostCallOutcome name(cpu::CpuState& c, cpu::IGuestMemory& m)
#define ENTER (G().c = &c, G().m = &m)

// HHOOK SetWindowsHookEx(idHook, proc, hMod, threadId)
FN(SetWindowsHookExA) {
    ENTER;
    int id = static_cast<int32_t>(Arg(c, m, 0));
    uint32_t proc = Arg(c, m, 1);
    if (!proc || id < -1 || id > 14) { SetLastError(m, 1426); return Ret(0); } // ERROR_INVALID_HOOK_FILTER
    uint32_t h = g_nextHook += 4;
    Hooks().insert(Hooks().begin(), Hook{h, id, proc});
    return Ret(h);
}
FN(UnhookWindowsHookEx) {
    ENTER;
    uint32_t h = Arg(c, m, 0);
    for (Hook& k : Hooks())
        if (k.handle == h && !k.removed) {
            k.removed = true;
            if (!g_hookDepth) Hooks().erase(std::find_if(Hooks().begin(), Hooks().end(), [h](const Hook& x) { return x.handle == h; }));
            return Ret(1);
        }
    SetLastError(m, 1404); // ERROR_INVALID_HOOK_HANDLE
    return Ret(0);
}
// LRESULT CallNextHookEx(hhk, code, wp, lp)
FN(CallNextHookEx) {
    ENTER;
    uint32_t h = Arg(c, m, 0);
    std::vector<Hook>& hooks = Hooks();
    for (size_t i = 0; i < hooks.size(); ++i)
        if (hooks[i].handle == h) {
            int n = NextHook(hooks[i].id, static_cast<int>(i) + 1);
            return Ret(n < 0 ? 0 : CallHookAt(n, static_cast<int32_t>(Arg(c, m, 1)), Arg(c, m, 2), Arg(c, m, 3)));
        }
    return Ret(0);
}
FN(UnhookWindowsHook) {
    ENTER;
    int id = static_cast<int32_t>(Arg(c, m, 0));
    uint32_t proc = Arg(c, m, 1);
    for (Hook& k : Hooks())
        if (k.id == id && k.proc == proc && !k.removed) { k.removed = true; return Ret(1); }
    return Ret(0);
}
FN(CallMsgFilterW) {
    ENTER;
    return Ret(CallHooks(-1 /* WH_MSGFILTER */, static_cast<int32_t>(Arg(c, m, 1)), 0, Arg(c, m, 0)));
}

#define E(name, bytes) {"USER32.dll", #name, bytes, CallConv::Stdcall, &name}
const HostFunctionEntry kEntries[] = {
    E(SetWindowsHookExA, 16), {"USER32.dll", "SetWindowsHookExW", 16, CallConv::Stdcall, &SetWindowsHookExA},
    E(UnhookWindowsHookEx, 4), E(CallNextHookEx, 16), E(UnhookWindowsHook, 8),
    E(CallMsgFilterW, 8), {"USER32.dll", "CallMsgFilterA", 8, CallConv::Stdcall, &CallMsgFilterW},
    {"USER32.dll", "CallMsgFilter", 8, CallConv::Stdcall, &CallMsgFilterW},
};
const bool kRegistered = RegisterHostFunctionsLayer(kEntries, sizeof(kEntries) / sizeof(kEntries[0]), kUiLayer);

} // namespace

uint32_t CallHooks(int id, int code, uint32_t wp, uint32_t lp) {
    int i = NextHook(id, 0);
    return i < 0 ? 0 : CallHookAt(i, code, wp, lp);
}

} // namespace user
} // namespace win32
