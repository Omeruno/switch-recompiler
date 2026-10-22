#include "guest_execution.h"
#include "../win32/host_functions.h"
#include "../win32/user32.h"
#include "../win32/user_apc.h"
#include <cerrno>
#include <new>

namespace rt {
namespace { thread_local GuestExecution* active=nullptr; }
GuestExecution::GuestExecution(Process& process, Step step)
    : process_(process), step_(std::move(step)), previous_(active) { active=this; }
GuestExecution::~GuestExecution() {
    // Return through pending native frames so their RAII resources are
    // destroyed; freeing suspended stacks would silently leak these objects.
    stopped_=true;
    const bool exitRequested=win32::g_guestExitRequested;
    const int32_t exitCode=win32::g_guestExitCode;
    win32::g_guestExitRequested=true;
    for (auto& item:continuations_)
        if (item.second && item.second->fiber.Started() && !item.second->fiber.Finished())
            Resume(*item.second);
    // A nested executor may temporarily borrow the same Process. Its owner
    // still has live native stacks; only the last executor ends the session.
    bool ownsSession=true;
    for (auto* owner=previous_;owner;owner=owner->previous_)
        if (&owner->process_==&process_) { ownsSession=false; break; }
    if (ownsSession)
        process_.StopThreads(exitRequested ? uint32_t(exitCode) : 0xc0000001u);
    ReapExited();
    win32::g_guestExitRequested=exitRequested;
    win32::g_guestExitCode=exitCode;
    active=previous_;
}
bool GuestExecution::Active() { return active && active->running_; }
bool GuestExecution::Checkpoint() { return !Active() || active->CheckCurrent(); }
void GuestExecution::Yield() {
    if (Active() && !active->stopped_ && !win32::g_guestExitRequested)
        active->SuspendCurrent();
}
void GuestExecution::SuspendCurrent() {
    auto context=win32::user::G();
    uint32_t teb=win32::g_tebAddr;
    int error=errno;
    running_->fiber.Suspend();
    win32::user::G()=context;
    win32::g_tebAddr=teb;
    errno=error;
}
bool GuestExecution::CheckCurrent() {
    GuestThread& thread=*running_->thread;
    if (stopped_ || win32::g_guestExitRequested || thread.ShouldUnwind())
        return false;
    if (!thread.IsRunnable() || thread.yieldRequested || ++operations_>=256) {
        thread.yieldRequested=false;
        SuspendCurrent();
    }
    return !stopped_ && !win32::g_guestExitRequested && !thread.ShouldUnwind();
}
void GuestExecution::Run(void* arg) {
    auto& continuation=*static_cast<Continuation*>(arg);
    auto& executor=*continuation.owner;
    auto& thread=*continuation.thread;
    bool start=true;
    if (thread.needsThreadAttach) {
        thread.needsThreadAttach=false;
        start=executor.process_.NotifyThread(thread,2);
    }
    // APCs queued before a suspended thread starts precede its user entry.
    if (start) win32::DispatchUserApcs(thread.cpu,executor.process_.Memory());
    if (start) while (executor.CheckCurrent())
        if (!executor.step_(thread)) { executor.stopped_=true; break; }
    // ExitThread first unwinds native WndProc/SEH frames. Run detach on a
    // clean native frame while guest stack/TEB/TLS and HANDLE are still live.
    if (thread.exitRequested && thread.status!=GuestThreadStatus::Exited &&
        !executor.stopped_ && !win32::g_guestExitRequested) {
        thread.detaching=true;
        win32::user::CleanupThreadWindows(thread.id);
        executor.process_.NotifyThread(thread,3);
        thread.detaching=false;
        if (!executor.stopped_ && !win32::g_guestExitRequested)
            executor.process_.Scheduler().Exit(thread,thread.requestedExitCode);
    }
}
void GuestExecution::Resume(Continuation& continuation) {
    auto* savedThread=process_.Scheduler().Current();
    auto context=win32::user::G();
    uint32_t teb=win32::g_tebAddr;
    int error=errno;
    running_=&continuation;
    operations_=0;
    process_.Scheduler().SetCurrent(continuation.thread);
    win32::user::G()={&continuation.thread->cpu,&process_.Memory()};
    win32::g_tebAddr=continuation.thread->teb;
    continuation.fiber.Resume(root_);
    running_=nullptr;
    process_.Scheduler().SetCurrent(savedThread);
    win32::user::G()=context;
    win32::g_tebAddr=teb;
    errno=error;
}
void GuestExecution::ReapExited() {
    for (auto it=continuations_.begin();it!=continuations_.end();) {
        auto& continuation=it->second;
        if (!continuation) { it=continuations_.erase(it); continue; }
        if (continuation->thread->status!=GuestThreadStatus::Exited) {
            ++it;
            continue;
        }
        // An exit may have been requested by a peer while this native stack
        // was parked in a callback. Checkpoint now returns false, unwinding
        // its wrappers normally before any guest mappings can be reclaimed.
        if (continuation->fiber.Started() && !continuation->fiber.Finished())
            Resume(*continuation);
        if (continuation->fiber.Started() && !continuation->fiber.Finished()) {
            ++it;
            continue;
        }
        process_.ReleaseThreadMemory(*continuation->thread);
        it=continuations_.erase(it);
    }
    // Created-suspended workers can exit before a native stack ever exists.
    // Keep tombstones in Process/Scheduler for open thread HANDLEs.
    const auto releaseWithoutContinuation=[&](GuestThread& thread) {
        if (thread.status==GuestThreadStatus::Exited &&
            continuations_.find(thread.id)==continuations_.end())
            process_.ReleaseThreadMemory(thread);
    };
    releaseWithoutContinuation(process_.MainThread());
    for (auto& thread:process_.workerThreads_) releaseWithoutContinuation(*thread);
}
GuestExecution::Result GuestExecution::RunOne() {
    ReapExited();
    if (stopped_ || win32::g_guestExitRequested) { stopped_=true; return Result::Stopped; }
    GuestThread* thread=process_.Scheduler().Next();
    if (!thread) return Result::Idle;
    auto& continuation=continuations_[thread->id];
    if (!continuation) {
        auto created=std::unique_ptr<Continuation>(new (std::nothrow) Continuation());
        if (!created) return Result::AllocationFailed;
        created->owner=this; created->thread=thread;
        if (!created->fiber.Initialize(&Run,created.get(),
                thread==&process_.MainThread() ? 2*1024*1024 : 512*1024)) return Result::AllocationFailed;
        continuation=std::move(created);
    }
    Resume(*continuation);
    ReapExited();
    return stopped_ || win32::g_guestExitRequested ? Result::Stopped : Result::Ran;
}
} // namespace rt
