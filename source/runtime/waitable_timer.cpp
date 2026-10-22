#include "waitable_timer.h"
#include "process.h"
#include "clock.h"
#include <algorithm>
namespace rt {
void CancelTimer(TimerObject& timer,GuestScheduler* scheduler) {
    if(scheduler) if(auto* target=scheduler->Find(timer.callbackThread)) {
        auto& queue=target->pendingApcs;
        queue.erase(std::remove_if(queue.begin(),queue.end(),[&](const GuestThread::UserApc& apc) {
            return apc.timerApc && apc.timer.lock().get()==&timer;
        }),queue.end());
    }
    timer.active=false;timer.apcQueued=false;++timer.generation;
    timer.routine=timer.callbackThread=0;
    // Cancellation preserves signaled state. Rearm resets it separately.
}
bool HasActiveWaitableTimers(HandleTable& handles) {
    for(const auto& timer:handles.Timers()) if(timer->active) return true;
    return false;
}
void PollWaitableTimers(HandleTable& handles,GuestScheduler* scheduler) {
    const uint64_t now=Clock::NowUs(),wall=Clock::FileTime();
    for(const auto& timer:handles.Timers()) {
        if(!timer->active) continue;
        if(timer->routine && scheduler) {
            auto* target=scheduler->Find(timer->callbackThread);
            if(!target || target->status==GuestThreadStatus::Exited) { CancelTimer(*timer,scheduler);continue; }
        }
        if(timer->absoluteTime ? wall<timer->absoluteTime : now<timer->dueUs) continue;
        timer->signaled=true;
        if(timer->periodMs) {
            const uint64_t period=uint64_t(timer->periodMs)*1000;
            // Coalesce missed periods: no token count or unbounded APC backlog.
            timer->dueUs=timer->absoluteTime ? now+period-((wall-timer->absoluteTime)/10)%period :
                timer->dueUs+((now-timer->dueUs)/period+1)*period;
            timer->absoluteTime=0;
        } else timer->active=false;
        if(scheduler) {
            for(auto h:handles.Aliases(timer.get())) scheduler->Signal(h,true);
            if(timer->routine && !timer->apcQueued) {
                auto* target=scheduler->Find(timer->callbackThread);
                if(target && !target->exitRequested && target->status!=GuestThreadStatus::Exited) {
                    GuestThread::UserApc apc;
                    apc.routine=timer->routine;apc.data=timer->data;
                    apc.timeLow=static_cast<uint32_t>(wall);apc.timeHigh=static_cast<uint32_t>(wall>>32);
                    apc.timerApc=true;apc.generation=timer->generation;apc.timer=timer;
                    target->pendingApcs.push_back(apc);timer->apcQueued=true;
                    if(target->alertableWait) scheduler->InterruptWait(*target);
                }
            }
        }
    }
}
void PollWaitableTimers() {
    PollWaitableTimers(Handles(),g_process ? &g_process->Scheduler() : nullptr);
}
}
