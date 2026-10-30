// Per-guest USER32 queues and synchronous sent-message rendezvous.
#include "user32.h"
#include "../runtime/guest_execution.h"
#include <algorithm>
#include <cstring>

namespace win32 { namespace user {
namespace {
constexpr uint32_t sendWaitToken=0xfffffff2u;
bool Live(uint32_t id) {
    if (!rt::g_process) return id==0x1004;
    const auto* thread=rt::g_process->FindThread(id);
    return thread && thread->status!=rt::GuestThreadStatus::Exited;
}
void Finish(const std::shared_ptr<SentMessage>& request,uint32_t result,uint32_t error=0) {
    if (request->completed) return;
    request->result=result; request->error=error; request->completed=true;
    if (rt::g_process) rt::g_process->Scheduler().Signal(sendWaitToken,true);
}
void Error(uint32_t error) { if (G().m) hle::SetLastError(*G().m,error); }
}
uint32_t CurrentGuiThread() { return rt::g_process ? rt::g_process->CurrentThreadId() : 0x1004; }
ThreadQueue& QueueFor(uint32_t id) {
    auto inserted=S().queues.emplace(id,ThreadQueue{});
    if (inserted.second) std::memcpy(inserted.first->second.keys,S().asyncKeys,256);
    return inserted.first->second;
}
ThreadQueue& Q() { return QueueFor(CurrentGuiThread()); }
WindowCallScope::WindowCallScope() { ++S().nativeCalls; }
WindowCallScope::~WindowCallScope() {
    if (--S().nativeCalls==0) { S().retiredWindows.clear(); S().retiredClasses.clear(); }
}
bool PostToThread(uint32_t id,uint32_t hwnd,uint32_t msg,uint32_t wp,uint32_t lp) {
    auto it=S().queues.find(id);
    if (!Live(id) || it==S().queues.end() || it->second.closed) return false;
    Msg message;message.hwnd=hwnd;message.message=msg;message.wParam=wp;message.lParam=lp;
    message.time=NowMs();message.x=S().cursorX;message.y=S().cursorY;
    it->second.posted.push_back(message);
    it->second.changed|=0x108;
    return true;
}
uint32_t SendMessageState() {
    auto& stack=Q().receiving;
    if (stack.empty()) return 0;
    const auto& request=stack.back();
    return (request->notify ? 2u : 1u) | (request->completed ? 8u : 0u);
}
bool ReplyToSender(uint32_t result) {
    auto& stack=Q().receiving;
    if (stack.empty() || stack.back()->notify || stack.back()->completed) return false;
    Finish(stack.back(),result);
    return true;
}
bool PumpSentMessages() {
    auto& queue=Q();
    while (!queue.sent.empty()) {
        if (!rt::GuestExecution::Checkpoint()) return false;
        auto request=queue.sent.front();queue.sent.pop_front();
        if (request->completed) continue;
        auto* window=Win(request->msg.hwnd);
        if (!window || window->threadId!=CurrentGuiThread()) { Finish(request,0,1400); continue; }
        request->started=true;
        queue.receiving.push_back(request);
        // Keep context LIFO across nested sent-message pumps and fiber parks.
        struct ReceiveScope {
            ThreadQueue& queue;
            ~ReceiveScope() { queue.receiving.pop_back(); }
        } scope{queue};
        uint32_t result=CallProc(window->wndProc,window->unicode,window->hwnd,
            request->msg.message,request->msg.wParam,request->msg.lParam,request->unicode);
        bool alive=rt::GuestExecution::Checkpoint();
        Finish(request,alive ? result : 0,alive ? 0 : 1444);
        if (!alive) return false;
    }
    return true;
}
bool SendMsgTimeout(uint32_t hwnd,uint32_t msg,uint32_t wp,uint32_t lp,bool unicode,
                    uint32_t timeout,uint32_t flags,uint32_t& result,bool notify) {
    WindowCallScope windowScope;
    result=0;
    if (hwnd==0xffffu) {
        const auto list=Desktop()->children;
        bool success=true;
        for (uint32_t target:list)
            if (Win(target) && !SendMsgTimeout(target,msg,wp,lp,unicode,timeout,flags,result,notify)) success=false;
        return success;
    }
    auto* window=Win(hwnd);
    if (!window) { Error(1400); return false; }
    const uint32_t target=window->threadId;
    const uint32_t sender=CurrentGuiThread();
    if (!target || target==sender) {
        result=CallProc(window->wndProc,window->unicode,hwnd,msg,wp,lp,unicode);
        return true;
    }
    if (!Live(target) || !rt::GuestExecution::Active()) { Error(1444); return false; }
    Q();
    auto& destination=QueueFor(target);
    if (destination.closed) { Error(1444); return false; }
    auto request=std::make_shared<SentMessage>();
    request->sender=sender;request->receiver=target;request->unicode=unicode;request->notify=notify;
    request->msg.hwnd=hwnd;request->msg.message=msg;request->msg.wParam=wp;request->msg.lParam=lp;
    destination.sent.push_back(request);
    destination.changed|=0x40;
    rt::g_process->Scheduler().Signal(sendWaitToken,true);
    if (notify) return true;
    const bool finite=timeout!=0xffffffffu;
    const uint64_t deadline=finite ? rt::Clock::NowUs()+uint64_t(timeout)*1000 : 0;
    while (!request->completed) {
        if (!rt::GuestExecution::Checkpoint()) { Finish(request,0,1444); break; }
        if (!(flags & 1u) && !PumpSentMessages()) { Finish(request,0,1444); break; }
        if (request->completed) break;
        if (finite && rt::Clock::NowUs()>=deadline) { Finish(request,0,1460); break; }
        auto& scheduler=rt::g_process->Scheduler();
        auto* current=scheduler.Current();
        if (finite) scheduler.Sleep(*current,1);
        else scheduler.Wait(*current,sendWaitToken);
        rt::GuestExecution::Yield();
    }
    result=request->result;
    if (request->error) { Error(request->error); return false; }
    return true;
}
void RetireWindow(uint32_t hwnd) {
    S().windowLayers.Forget(hwnd);
    auto it=S().windows.find(hwnd);
    if (it==S().windows.end()) return;
    for (auto& item:S().queues) {
        auto& queue=item.second;
        for (auto& request:queue.sent) if (request->msg.hwnd==hwnd) Finish(request,0,1400);
        const auto remove=[&](std::deque<Msg>& messages) {
            messages.erase(std::remove_if(messages.begin(),messages.end(),
                [&](const Msg& message) { return message.hwnd==hwnd; }),messages.end());
        };
        remove(queue.posted);remove(queue.input);
    }
    RetireWindowMenu(hwnd);
    ForgetWindowInput(hwnd);
    ForgetDialogWindow(hwnd);
    S().retiredWindows.push_back(std::move(it->second));
    S().windows.erase(it);
    if (!S().nativeCalls) S().retiredWindows.clear();
}
void CleanupThreadWindows(uint32_t id) {
    if (!S().initialized) return;
    WindowCallScope scope;
    std::vector<uint32_t> windows;
    for (const auto& item:S().windows) if (item.second->threadId==id) windows.push_back(item.first);
    for (uint32_t hwnd:windows) {
        if (!rt::GuestExecution::Checkpoint()) break;
        if (Win(hwnd)) DestroyWindowInternal(hwnd);
    }
}
void OnGuiThreadExit(uint32_t id) {
    WindowCallScope scope;
    for(auto& owner:S().mouseDownOwners)if(owner==id)owner=0;
    auto queue=S().queues.find(id);
    if (queue!=S().queues.end()) {
        auto& q=queue->second;
        for (auto& request:q.sent) Finish(request,0,1444);
        for (auto& request:q.receiving) Finish(request,0,1444);
        q.sent.clear();q.posted.clear();q.input.clear();q.timers.clear();q.quit=false;q.closed=true;
    }
    std::vector<uint32_t> windows;
    for (const auto& item:S().windows) if (item.second->threadId==id) windows.push_back(item.first);
    // Thread-exit cleanup must still retire handles on failure, but never
    // continue presenting after losing a dirty window's CPU authority.
    if(!windows.empty() && !ui::g_gdi.PrepareCpuAccess())S().presentationFault=true;
    for (uint32_t hwnd:windows) {
        auto* window=Win(hwnd);
        if (auto* parent=ParentWin(window)) {
            auto& children=parent->children;
            children.erase(std::remove(children.begin(),children.end(),hwnd),children.end());
        }
        RetireWindow(hwnd);
    }
    if (!windows.empty()) ui::g_gdi.InvalidateLayout();
}
} } // namespace win32::user
