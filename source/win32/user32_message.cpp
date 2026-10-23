// user32_message.cpp — очередь сообщений, ввод, таймеры, WM_PAINT и вывод
// кадра (см. user32.h).
//
// Сначала обрабатываются межпоточные sent messages, затем очередь PostMessage,
// затем ввод, затем WM_QUIT, затем WM_PAINT, затем WM_TIMER. Ожидание —
// через платформу фронтенда (g_platform.wait); без неё — «головной» режим:
// время до ближайшего таймера проматывается, а если ждать нечего,
// программа получает WM_QUIT (иначе зависла бы навсегда).

#include <algorithm>
#include <cstdio>

#include "../runtime/clock.h"
#include "../runtime/guest_execution.h"
#include "user32.h"

namespace win32 {
namespace user {

namespace {

constexpr uint32_t VK_LBUTTON = 1, VK_RBUTTON = 2, VK_MBUTTON = 4, VK_SHIFT = 0x10, VK_CONTROL = 0x11,
                   VK_MENU = 0x12, VK_CAPITAL = 0x14, VK_F10 = 0x79;

bool MatchesFilter(uint32_t msgHwnd, uint32_t message, uint32_t filterHwnd, uint32_t min, uint32_t max) {
    if (min || max) {
        if (message < min || message > max) return false;
    }
    if (filterHwnd == 0) return true;
    if (filterHwnd == 0xFFFFFFFFu) return msgHwnd == 0;
    for (Window* w = Win(msgHwnd); w; w = (w->style & WS_CHILD) ? Win(w->parent) : nullptr)
        if (w->hwnd == filterHwnd) return true;
    return false;
}

Window* FindPaintWindow(Window* w, uint32_t filter) {
    if (!w || !(w->style & WS_VISIBLE)) return nullptr;
    if (w->hwnd != S().desktop && w->threadId==CurrentGuiThread() && NeedsPaint(w) && MatchesFilter(w->hwnd, WM_PAINT, filter, 0, 0)) return w;
    for (uint32_t c : w->children)
        if (Window* f = FindPaintWindow(Win(c), filter)) return f;
    return nullptr;
}

uint32_t MouseKeyFlags() {
    const uint8_t* k = S().asyncKeys;
    uint32_t f = 0;
    if (k[VK_LBUTTON] & 0x80) f |= 1;
    if (k[VK_RBUTTON] & 0x80) f |= 2;
    if (k[VK_SHIFT] & 0x80) f |= 4;
    if (k[VK_CONTROL] & 0x80) f |= 8;
    if (k[VK_MBUTTON] & 0x80) f |= 0x10;
    return f;
}

uint32_t MakeLong(int lo, int hi) { return (static_cast<uint32_t>(lo) & 0xFFFF) | (static_cast<uint32_t>(hi) << 16); }

void QueueInput(uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp, uint64_t observedNs) {
    Msg m;
    m.hwnd = hwnd;
    m.message = msg;
    m.wParam = wp;
    m.lParam = lp;
    m.time = NowMs();
    if(S().inputFrames.Enabled())m.observedNs=observedNs?observedNs:ui::PerformanceNowNs();
    m.x = S().cursorX;
    m.y = S().cursorY;
    const auto* window=Win(hwnd);
    if (!window || !window->threadId) return;
    auto& queue=QueueFor(window->threadId);
    if (queue.closed) return;
    queue.changed |= (msg>=WM_KEYDOWN && msg<=WM_SYSCHAR) ? 1u :
        ((msg==WM_MOUSEMOVE || msg==WM_NCMOUSEMOVE) ? 2u : 4u);
    // движения мыши сливаем: важна последняя позиция
    if ((msg == WM_MOUSEMOVE || msg == WM_NCMOUSEMOVE) && !queue.input.empty()) {
        Msg& last = queue.input.back();
        if (last.message == msg && last.hwnd == hwnd) { S().inputFrames.Coalesced();last = m; return; }
    }
    queue.input.push_back(m);
}

void MouseEvent(const InputEvent& e) {
    State& s = S();
    s.cursorX = std::max(0, std::min(e.x, s.screenW - 1));
    s.cursorY = std::max(0, std::min(e.y, s.screenH - 1));
    int x = s.cursorX, y = s.cursorY;
    uint32_t vk = e.button == 1 ? VK_RBUTTON : e.button == 2 ? VK_MBUTTON : VK_LBUTTON;
    if (e.type == InputEvent::MouseDown) s.asyncKeys[vk] = 0x80;
    if (e.type == InputEvent::MouseUp) s.asyncKeys[vk] = 0;

    Window* target=WindowFromPoint(x,y);
    const auto* capture=Win(s.capture);
    const auto* foreground=Win(s.foreground);
    const bool captureForeground=capture && foreground && capture->threadId==foreground->threadId;
    const bool ownDrag=capture && std::any_of(std::begin(s.mouseDownOwners),std::end(s.mouseDownOwners),
        [&](uint32_t owner) {return owner==capture->threadId;});
    const bool captured=capture && ((captureForeground &&
        ((target && target->threadId==capture->threadId) || ownDrag)) || target==capture);
    if(captured)target=Win(s.capture);
    const unsigned button=e.button==1?1:e.button==2?2:0;
    if(e.type==InputEvent::MouseDown)s.mouseDownOwners[button]=target?target->threadId:0;
    if(e.type==InputEvent::MouseUp)s.mouseDownOwners[button]=0;
    if (!target) return;
    uint32_t hwnd = target->hwnd;
    uint32_t hit = captured ? HTCLIENT : SendMsg(hwnd, WM_NCHITTEST, 0, MakeLong(x, y));
    if (!rt::GuestExecution::Checkpoint() || !(target = Win(hwnd))) return;
    if (hit == HTTRANSPARENT && (target->style & WS_CHILD)) { hwnd = target->parent; hit = HTCLIENT; target = Win(hwnd); if (!target) return; }

    if (e.type == InputEvent::Wheel) {
        const auto* foreground=Win(s.foreground);
        const uint32_t focus=foreground?FocusForWindow(foreground->hwnd):0;
        uint32_t f = focus ? focus : hwnd;
        QueueInput(f, WM_MOUSEWHEEL, MakeLong(static_cast<int>(MouseKeyFlags()), e.wheel), MakeLong(x, y),e.observedNs);
        return;
    }
    bool client = hit == HTCLIENT;
    uint32_t base = e.button == 1 ? WM_RBUTTONDOWN : e.button == 2 ? WM_MBUTTONDOWN : WM_LBUTTONDOWN;
    uint32_t msg;
    if (e.type == InputEvent::MouseMove) msg = WM_MOUSEMOVE;
    else if (e.type == InputEvent::MouseDown) msg = base;
    else msg = base + 1;

    if (e.type == InputEvent::MouseDown) {
        // активация окна верхнего уровня щелчком
        Window* root = target;
        while (root && (root->style & WS_CHILD) && Win(root->parent)) root = Win(root->parent);
        if (root && root->hwnd != s.foreground && !(root->style & WS_DISABLED)) {
            const uint32_t rootHwnd=root->hwnd;
            uint32_t ma = SendMsg(hwnd, WM_MOUSEACTIVATE, rootHwnd, MakeLong(static_cast<int>(hit), static_cast<int>(msg)));
            if(!rt::GuestExecution::Checkpoint())return;
            if (ma != 3 && ma != 4 && Win(rootHwnd)) SetActive(rootHwnd);
            if (ma == 2 || ma == 4) return; // щелчок «съеден»
        }
        // двойной щелчок
        if (client && (target = Win(hwnd)) && target->cls && (target->cls->style & 0x8 /* CS_DBLCLKS */)) {
            uint32_t now = NowMs();
            if (s.lastClickHwnd == hwnd && s.lastClickMsg == msg && now - s.lastClickTime <= 500 &&
                std::abs(x - s.lastClickX) <= 4 && std::abs(y - s.lastClickY) <= 4) {
                msg = base + 2;
                s.lastClickHwnd = 0;
            } else {
                s.lastClickHwnd = hwnd;
                s.lastClickMsg = msg;
                s.lastClickTime = now;
                s.lastClickX = x;
                s.lastClickY = y;
            }
        }
    }
    if (!(target = Win(hwnd))) return;
    if (client) {
        int cx = x - ClientOriginX(target), cy = y - ClientOriginY(target);
        QueueInput(hwnd, msg, MouseKeyFlags(), MakeLong(cx, cy),e.observedNs);
    } else {
        QueueInput(hwnd, msg - WM_MOUSEMOVE + WM_NCMOUSEMOVE, hit, MakeLong(x, y),e.observedNs);
    }
}

uint32_t KeyTarget() {
    const uint32_t focus=FocusForWindow(S().foreground);
    if(focus && Win(focus))return focus;
    return S().foreground;
}

void KeyEvent(const InputEvent& e) {
    State& s = S();
    uint32_t vk = e.vk & 0xFF;
    bool down = e.type == InputEvent::KeyDown;
    bool wasDown = (s.asyncKeys[vk] & 0x80) != 0;
    s.asyncKeys[vk] = down ? 0x80 : 0;
    if (vk == 0xA0 || vk == 0xA1) s.asyncKeys[VK_SHIFT] = s.asyncKeys[vk];
    if (vk == 0xA2 || vk == 0xA3) s.asyncKeys[VK_CONTROL] = s.asyncKeys[vk];
    if (vk == 0xA4 || vk == 0xA5) s.asyncKeys[VK_MENU] = s.asyncKeys[vk];
    uint32_t target = KeyTarget();
    if (!target) return;
    bool alt = (s.asyncKeys[VK_MENU] & 0x80) != 0;
    bool sys = (alt && !(s.asyncKeys[VK_CONTROL] & 0x80)) || vk == VK_F10 || vk == VK_MENU;
    uint32_t lp = 1 | ((e.vk >> 8 & 0xFF) << 16);
    if (alt) lp |= 1u << 29;
    if (down && wasDown) lp |= 1u << 30;
    if (!down) lp |= (1u << 30) | (1u << 31);
    uint32_t msg = down ? (sys ? WM_SYSKEYDOWN : WM_KEYDOWN) : (sys ? WM_SYSKEYUP : WM_KEYUP);
    QueueInput(target, msg, vk, lp,e.observedNs);
}

// Применяет события платформы; false — ввода больше не будет
bool PollPlatform(int timeoutMs) {
    std::vector<InputEvent> events;
    bool alive = true;
    if (g_platform.wait) {
        alive = g_platform.wait(timeoutMs, events);
    } else if (timeoutMs < 0) {
        alive = false;
    } else if (timeoutMs > 0) {
        rt::Clock::Sleep(static_cast<uint32_t>(timeoutMs));
    }
    const uint64_t ingestedNs=S().inputFrames.Enabled()?ui::PerformanceNowNs():0;
    for (InputEvent e : events) {
        if(g_platform.filterInput && g_platform.filterInput(g_platform.shellContext,e))continue;
        if(!e.observedNs)e.observedNs=ingestedNs;
        switch (e.type) {
        case InputEvent::MouseMove: case InputEvent::MouseDown: case InputEvent::MouseUp: case InputEvent::Wheel:
            MouseEvent(e);
            break;
        case InputEvent::KeyDown: case InputEvent::KeyUp:
            KeyEvent(e);
            break;
        case InputEvent::Char:
            if (uint32_t t = KeyTarget()) QueueInput(t, WM_CHAR, e.ch, 1,e.observedNs);
            break;
        case InputEvent::Quit:
            if (S().foreground) PostMsg(S().foreground, WM_CLOSE, 0, 0);
            else PostQuit(0);
            break;
        }
    }
    return alive;
}

void UpdateKeyState(const Msg& m) {
    uint8_t* k = Q().keys;
    switch (m.message) {
    case WM_KEYDOWN: case WM_SYSKEYDOWN:
        if (!(k[m.wParam & 0xFF] & 0x80)) k[m.wParam & 0xFF] ^= 1; // бит переключения
        k[m.wParam & 0xFF] |= 0x80;
        break;
    case WM_KEYUP: case WM_SYSKEYUP:
        k[m.wParam & 0xFF] &= 0x7F;
        break;
    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: k[VK_LBUTTON] |= 0x80; break;
    case WM_LBUTTONUP: k[VK_LBUTTON] &= 0x7F; break;
    case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK: k[VK_RBUTTON] |= 0x80; break;
    case WM_RBUTTONUP: k[VK_RBUTTON] &= 0x7F; break;
    }
}



} // namespace

void PostQuit(uint32_t code) {
    auto& queue=Q();queue.quit=true;queue.quitCode=code;queue.changed|=0x108;
}
void PollQueueEvents() {
    PollPlatform(0);
    PresentIfNeeded(false);
}
uint32_t QueueStatusBits() {
    auto& queue=Q();
    uint32_t status=queue.posted.empty() && !queue.quit ? 0 : 0x108;
    for (const auto& sent:queue.sent) if (!sent->completed) { status|=0x40;break; }
    for (const Msg& input:queue.input)
        status|=(input.message>=WM_KEYDOWN && input.message<=WM_SYSCHAR) ? 1u :
            ((input.message==WM_MOUSEMOVE || input.message==WM_NCMOUSEMOVE) ? 2u : 4u);
    const uint32_t now=NowMs();
    for (auto& timer:queue.timers) if (timer.due<=now) {
        status|=0x10;
        if (!timer.announced) { queue.changed|=0x10;timer.announced=true; }
    }
    for (auto& item:S().windows) {
        auto& window=*item.second;
        if (window.threadId!=CurrentGuiThread()) continue;
        const bool paint=IsVisibleChain(&window) && NeedsPaint(&window);
        if (!paint) { window.paintAnnounced=false;continue; }
        status|=0x20;
        if (!window.paintAnnounced) { queue.changed|=0x20;window.paintAnnounced=true; }
    }
    return status;
}

void PresentIfNeeded(bool force) {
    State& s = S();
    if (!g_platform.present) return;
    uint64_t changes = ui::g_gdi.ScreenChangeCounter();
    const uint64_t shellRevision=g_platform.shellRevision?g_platform.shellRevision(g_platform.shellContext):0;
    if (changes == s.presentedChanges && shellRevision==s.presentedShellRevision && !force) return;
    uint32_t now = NowMs();
    if (!force && now - s.lastPresentMs < 16) return;
    s.presentedChanges = changes;s.presentedShellRevision=shellRevision;
    s.lastPresentMs = now;
    g_platform.present(s.screen.s);
}

// Выборка сообщения. wait — ждать (GetMessage), иначе только проверить.
bool FetchMessage(Msg& out, uint32_t hwnd, uint32_t min, uint32_t max, bool remove, bool wait) {
    ThreadQueue& queue=Q();
    for (;;) {
        if (!PumpSentMessages()) { out=Msg(); out.message=WM_QUIT; return true; }
        if (!rt::GuestExecution::Checkpoint()) {
            out=Msg(); out.message=WM_QUIT;
            return true;
        }
        // Present drawing from the previous dispatch even while messages keep
        // arriving. Early returns for input/posted/paint/timer messages must
        // not starve output until the pointer stops and the queue goes idle.
        // Keep the normal change-counter and 16 ms throttle; no guest callback.
        PresentIfNeeded(false);
        // ввод с платформы — не чаще раза в несколько мс (PeekMessage в цикле игры)
        uint32_t now = NowMs();
        if (now - S().lastPollMs >= 4) {
            S().lastPollMs = now;
            PollPlatform(0);
        }
        QueueStatusBits(); // observe timer/paint transitions before marking input old
        queue.changed=0;
        for (auto it = queue.posted.begin(); it != queue.posted.end(); ++it) {
            if (it->message!=WM_QUIT && !MatchesFilter(it->hwnd, it->message, hwnd, min, max)) continue;
            out = *it;
            if (remove) queue.posted.erase(it);
            return true;
        }
        for (auto it = queue.input.begin(); it != queue.input.end(); ++it) {
            if (it->message!=WM_QUIT && !MatchesFilter(it->hwnd, it->message, hwnd, min, max)) continue;
            out = *it;
            if (remove) {
                if(S().inputFrames.Enabled())S().inputFrames.Removed(out.observedNs,ui::PerformanceNowNs(),ui::g_gdi.ScreenChangeCounter());
                queue.input.erase(it); UpdateKeyState(out);
            }
            return true;
        }
        if (queue.quit) {
            out = Msg();
            out.message = WM_QUIT;
            out.wParam = queue.quitCode;
            out.time = NowMs();
            if (remove) queue.quit = false;
            return true;
        }
        if (MatchesFilter(0, WM_PAINT, 0, min, max)) {
            Window* p = FindPaintWindow(Desktop(), hwnd);
            if (p && p->update.Empty() && !p->internalPaint) {
                // только рамка
                SendMsg(p->hwnd, WM_NCPAINT, 1, 0);
                if (Window* q = Win(p->hwnd)) q->ncDirty = false;
                continue;
            }
            if (p) {
                out = Msg();
                out.hwnd = p->hwnd;
                out.message = WM_PAINT;
                out.time = NowMs();
                return true;
            }
        }
        now = NowMs();
        if (MatchesFilter(0, WM_TIMER, 0, min, max)) {
            for (Timer& t : queue.timers) {
                if (t.due > now || !MatchesFilter(t.hwnd, WM_TIMER, hwnd, 0, 0)) continue;
                out = Msg();
                out.hwnd = t.hwnd;
                out.message = WM_TIMER;
                out.wParam = t.id;
                out.lParam = t.proc;
                out.time = now;
                if (remove) { t.due = now + std::max<uint32_t>(t.interval, 10); t.announced=false; }
                return true;
            }
        }
        if (!wait) {
            PresentIfNeeded(false);
            return false;
        }
        PresentIfNeeded(true);
        // A peer can post the message we need. Do not enter a platform's
        // unbounded native wait while that peer's guest CPU is parked.
        if (rt::GuestExecution::Active() && rt::g_process->Scheduler().HasLivePeer(
                rt::g_process->CurrentThreadId())) {
            rt::g_process->Scheduler().Sleep(*rt::g_process->Scheduler().Current(),1);
            rt::GuestExecution::Yield();
            continue;
        }
        int timeout = -1;
        for (const Timer& t : queue.timers) {
            int d = t.due > now ? static_cast<int>(t.due - now) : 0;
            if (timeout < 0 || d < timeout) timeout = d;
        }
        if (!PollPlatform(timeout) && queue.posted.empty() && queue.input.empty() && queue.timers.empty() && !queue.quit) {
            // ждать больше нечего: завершаем цикл сообщений, а не виснем
            if (cpu::g_consoleAvailable) std::printf("[USER32] ожидание без источников событий: WM_QUIT\n");
            queue.quit = true;
            queue.quitCode = 0;
            queue.changed|=0x108;
        }
    }
}

// Символ для WM_CHAR по клавише (раскладка США)
uint32_t VkToChar(uint32_t vk, bool shift, bool caps) {
    if (vk >= 'A' && vk <= 'Z') return (shift != caps) ? vk : vk + 32;
    if (vk >= '0' && vk <= '9') return shift ? static_cast<uint32_t>(")!@#$%^&*("[vk - '0']) : vk;
    if (vk >= 0x60 && vk <= 0x69) return '0' + (vk - 0x60); // цифровой блок
    switch (vk) {
    case 0x20: return ' ';
    case 0x0D: return '\r';
    case 0x08: return 8;
    case 0x09: return 9;
    case 0x1B: return 27;
    case 0xBA: return shift ? ':' : ';';
    case 0xBB: return shift ? '+' : '=';
    case 0xBC: return shift ? '<' : ',';
    case 0xBD: return shift ? '_' : '-';
    case 0xBE: return shift ? '>' : '.';
    case 0xBF: return shift ? '?' : '/';
    case 0xC0: return shift ? '~' : '`';
    case 0xDB: return shift ? '{' : '[';
    case 0xDC: return shift ? '|' : '\\';
    case 0xDD: return shift ? '}' : ']';
    case 0xDE: return shift ? '"' : '\'';
    case 0x6A: return '*';
    case 0x6B: return '+';
    case 0x6D: return '-';
    case 0x6E: return '.';
    case 0x6F: return '/';
    }
    return 0;
}

bool TranslateKey(const Msg& m) {
    if (m.message != WM_KEYDOWN && m.message != WM_SYSKEYDOWN) return false;
    const uint8_t* k = Q().keys;
    bool ctrl = (k[VK_CONTROL] & 0x80) != 0;
    uint32_t ch = VkToChar(m.wParam, (k[VK_SHIFT] & 0x80) != 0, (k[VK_CAPITAL] & 1) != 0);
    if (ctrl && m.wParam >= 'A' && m.wParam <= 'Z') ch = m.wParam - 'A' + 1;
    if (!ch) return false;
    Msg c = m;
    c.message = m.message == WM_SYSKEYDOWN ? WM_SYSCHAR : WM_CHAR;
    c.wParam = ch;
    Q().posted.push_front(c);
    Q().changed|=0x108;
    return true;
}

} // namespace user
} // namespace win32
