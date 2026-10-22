// kernel32_sync.cpp — KERNEL32: синхронизация.
//
// Cooperative waits retry their original HLE call after an object signal.
// Objects live in the common per-process HANDLE table.

#include <map>

#include "handle_util.h"
#include "kernel_wait.h"
#include "user_apc.h"
#include "../runtime/guest_execution.h"
#include "../runtime/clock.h"
#include "../runtime/waitable_timer.h"
#include "../runtime/process.h"

namespace win32 {

rt::GuestThread* HleThreadFromHandle(uint32_t handle); // kernel32_module.cpp

namespace {

using namespace hle;

constexpr uint32_t kCurrentThreadId = 0x1004;
constexpr uint32_t kErrorNotOwner = 288;
uint32_t GuestTid() {
    return rt::g_process ? rt::g_process->CurrentThreadId() : kCurrentThreadId;
}

// CRITICAL_SECTION uses the Windows 32-bit six-DWORD layout.
// The guest address doubles as a scheduler wait key; no host OS lock is held.
HostCallOutcome K32_InitializeCriticalSection(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t cs = Arg(c, m, 0);
    m.Write32(cs + 0, 0);
    m.Write32(cs + 4, 0xFFFFFFFFu);
    m.Write32(cs + 8, 0);
    m.Write32(cs + 12, 0);
    m.Write32(cs + 16, 0);
    m.Write32(cs + 20, 0);
    return Ret(0);
}
HostCallOutcome K32_InitializeCriticalSectionAndSpinCount(cpu::CpuState& c, cpu::IGuestMemory& m) {
    K32_InitializeCriticalSection(c, m);
    m.Write32(Arg(c, m, 0) + 20, Arg(c, m, 1));
    return Ret(1);
}
HostCallOutcome K32_InitializeCriticalSectionEx(cpu::CpuState& c, cpu::IGuestMemory& m) { return K32_InitializeCriticalSectionAndSpinCount(c, m); }
HostCallOutcome K32_DeleteCriticalSection(cpu::CpuState& c, cpu::IGuestMemory& m) {
    const uint32_t cs = Arg(c, m, 0);
    // A deleted section must not strand any previously parked guest worker.
    if (rt::g_process) rt::g_process->Scheduler().Signal(cs, true);
    m.Write32(cs + 4, 0xFFFFFFFFu);
    m.Write32(cs + 8, 0);
    m.Write32(cs + 12, 0);
    return Ret(0);
}
bool TryAcquireCriticalSection(cpu::IGuestMemory& m, uint32_t cs) {
    const uint32_t current = GuestTid();
    const uint32_t owner = m.Read32(cs + 12);
    if (owner != 0 && owner != current) return false;
    const uint32_t recursion = m.Read32(cs + 8);
    m.Write32(cs + 8, recursion + 1);
    m.Write32(cs + 4, recursion);
    m.Write32(cs + 12, current);
    return true;
}
HostCallOutcome K32_EnterCriticalSection(cpu::CpuState& c, cpu::IGuestMemory& m) {
    const uint32_t cs = Arg(c, m, 0);
    if (TryAcquireCriticalSection(m, cs)) return Ret(0);
    // Keep guest EIP and stdcall stack intact: retry the HLE instruction when
    // the owning thread leaves. This is cooperative, never a host-side spin.
    rt::GuestThread* current = rt::g_process ?
        rt::g_process->Scheduler().Current() : nullptr;
    if (current && rt::g_process->Scheduler().Wait(*current, cs)) {
        HostCallOutcome pending;
        pending.customReturn = true;
        return pending;
    }
    // Standalone, unscheduled HLE callers cannot safely wait for an owner.
    SetLastError(m, kErrorNotOwner);
    return Ret(0);
}
HostCallOutcome K32_TryEnterCriticalSection(cpu::CpuState& c, cpu::IGuestMemory& m) {
    return Ret(TryAcquireCriticalSection(m, Arg(c, m, 0)) ? 1 : 0);
}
HostCallOutcome K32_LeaveCriticalSection(cpu::CpuState& c, cpu::IGuestMemory& m) {
    const uint32_t cs = Arg(c, m, 0);
    const uint32_t rec = m.Read32(cs + 8);
    if (!rec || m.Read32(cs + 12) != GuestTid()) {
        SetLastError(m, kErrorNotOwner);
        return Ret(0);
    }
    m.Write32(cs + 8, rec - 1);
    m.Write32(cs + 4, rec - 2); // Windows LockCount: -1 when unlocked
    if (rec == 1) {
        m.Write32(cs + 12, 0);
        if (rt::g_process) rt::g_process->Scheduler().Signal(cs, false);
    }
    return Ret(0);
}
HostCallOutcome K32_SetCriticalSectionSpinCount(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t cs = Arg(c, m, 0);
    uint32_t old = m.Read32(cs + 20);
    m.Write32(cs + 20, Arg(c, m, 1));
    return Ret(old);
}

// --- Interlocked* (KERNEL32 экспортирует их функциями) ---
HostCallOutcome K32_InterlockedIncrement(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t p = Arg(c, m, 0), v = m.Read32(p) + 1;
    m.Write32(p, v);
    return Ret(v);
}
HostCallOutcome K32_InterlockedDecrement(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t p = Arg(c, m, 0), v = m.Read32(p) - 1;
    m.Write32(p, v);
    return Ret(v);
}
HostCallOutcome K32_InterlockedExchange(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t p = Arg(c, m, 0), old = m.Read32(p);
    m.Write32(p, Arg(c, m, 1));
    return Ret(old);
}
HostCallOutcome K32_InterlockedExchangeAdd(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t p = Arg(c, m, 0), old = m.Read32(p);
    m.Write32(p, old + Arg(c, m, 1));
    return Ret(old);
}
// LONG InterlockedCompareExchange(LONG* dst, LONG exchange, LONG comparand)
HostCallOutcome K32_InterlockedCompareExchange(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t p = Arg(c, m, 0), old = m.Read32(p);
    if (old == Arg(c, m, 2)) m.Write32(p, Arg(c, m, 1));
    return Ret(old);
}

// --- Объекты ожидания ---
using WaitObject = rt::SyncObject;
using Kind = rt::ObjectKind;
std::shared_ptr<WaitObject> GetSync(uint32_t handle) {
    auto object = rt::Handles().Get(handle);
    if (!object || (object->type != Kind::Event && object->type != Kind::Mutex &&
                    object->type != Kind::Semaphore && object->type != Kind::Timer)) return nullptr;
    return std::static_pointer_cast<WaitObject>(object);
}
bool SetEventObject(cpu::IGuestMemory& m,uint32_t handle) {
    auto object = GetSync(handle);
    if (!object || object->type != Kind::Event) {
        SetLastError(m,ERROR_INVALID_HANDLE); return false;
    }
    if (!HandleAccess(m,handle,2)) return false;
    object->signaled = true;
    rt::SignalObject(object.get());
    return true;
}
HostCallOutcome K32_SetEvent(cpu::CpuState& c, cpu::IGuestMemory& m) {
    return Ret(SetEventObject(m,Arg(c,m,0)));
}
HostCallOutcome K32_ResetEvent(cpu::CpuState& c, cpu::IGuestMemory& m) {
    auto object = GetSync(Arg(c,m,0));
    if (!object || object->type != Kind::Event) {
        SetLastError(m,ERROR_INVALID_HANDLE); return Ret(0);
    }
    if (!HandleAccess(m,Arg(c,m,0),2)) return Ret(0);
    object->signaled = false;
    return Ret(1);
}
bool ReleaseMutexObject(cpu::IGuestMemory& m,uint32_t handle) {
    auto object=GetSync(handle);
    if (!object || object->type!=Kind::Mutex) {
        SetLastError(m,ERROR_INVALID_HANDLE);
        return false;
    }
    WaitObject& o=*object;
    uint32_t current=rt::g_process ? rt::g_process->CurrentThreadId() : kCurrentThreadId;
    if (o.ownerThread!=current || !o.recursion) {
        SetLastError(m,288u); // ERROR_NOT_OWNER
        return false;
    }
    if (--o.recursion==0) {
        o.ownerThread=0;
        o.signaled=true;
        rt::SignalObject(object.get());
    }
    return true;
}
bool ReleaseSemaphoreObject(cpu::IGuestMemory& m,uint32_t handle,int32_t amount,uint32_t prev) {
    auto object=GetSync(handle);
    if(!object || object->type!=Kind::Semaphore) {
        SetLastError(m,ERROR_INVALID_HANDLE);
        return false;
    }
    if (!HandleAccess(m,handle,2)) return false;
    WaitObject& o=*object;
    if(amount<=0 || amount>o.maxCount-o.count) {
        SetLastError(m,amount<=0 ? ERROR_INVALID_PARAMETER : 298u); // ERROR_TOO_MANY_POSTS
        return false;
    }
    if(prev) {
        if(!m.IsRangeMapped(prev,4)) { SetLastError(m,ERROR_INVALID_PARAMETER);return false; }
        m.Write32(prev,static_cast<uint32_t>(o.count));
    }
    o.count+=amount;
    rt::SignalObject(object.get());
    return true;
}

HostCallOutcome K32_ReleaseMutex(cpu::CpuState& c, cpu::IGuestMemory& m) {
    return Ret(ReleaseMutexObject(m,Arg(c,m,0)));
}
HostCallOutcome K32_ReleaseSemaphore(cpu::CpuState& c, cpu::IGuestMemory& m) {
    return Ret(ReleaseSemaphoreObject(m,Arg(c,m,0),static_cast<int32_t>(Arg(c,m,1)),Arg(c,m,2)));
}

// The native continuation retains this call across all wakeups: signal once,
// then park/revalidate only the wait side. Never retry the signal HLE on resume.
HostCallOutcome K32_SignalObjectAndWait(cpu::CpuState& c,cpu::IGuestMemory& m) {
    const uint32_t signal=Arg(c,m,0),wait=Arg(c,m,1),ms=Arg(c,m,2);
    const bool alertable=Arg(c,m,3)!=0;
    // Defensive runtime policy: validate wait access/type before any mutation.
    if (!ValidateWaitObjects(m,{wait},false)) return Ret(0xffffffffu);
    auto object=GetSync(signal);
    if (!object) { SetLastError(m,ERROR_INVALID_HANDLE); return Ret(0xffffffffu); }
    bool signaled=false;
    switch (object->type) {
        case Kind::Event: signaled=SetEventObject(m,signal); break;
        case Kind::Mutex: signaled=ReleaseMutexObject(m,signal); break;
        case Kind::Semaphore: signaled=ReleaseSemaphoreObject(m,signal,1,0); break;
        default: SetLastError(m,ERROR_INVALID_HANDLE);break;
    }
    if (!signaled) return Ret(0xffffffffu);
    object.reset(); // a parked wait must not extend the signaled object/name lifetime
    // No peer runs between preflight and signal. As on Windows, this does not
    // promise atomicity with the eventual acquisition of the second object.
    return Ret(AlertableObjectWait(c,m,{wait},false,ms,false,alertable));
}

constexpr uint32_t kWaitAbandoned = 0x80u;
uint32_t TryAcquire(uint32_t handle) {
    auto object = GetSync(handle);
    if (!object) return 0xffffffffu;
    WaitObject& o=*object;
    switch (o.type) {
        case Kind::Event:
        case Kind::Timer:
            if (!o.signaled) return 0x102;
            if (!o.manualReset) o.signaled=false;
            return 0;
        case Kind::Mutex: {
            if (o.ownerThread && o.ownerThread!=GuestTid()) return 0x102;
            const bool abandoned=o.abandoned;
            o.abandoned=false;
            o.ownerThread=GuestTid();
            ++o.recursion;
            o.signaled=false;
            return abandoned ? kWaitAbandoned : 0;
        }
        case Kind::Semaphore:
            if (o.count<=0) return 0x102;
            --o.count;
            return 0;
        default: return 0xffffffffu;
    }
}
bool ValidWaitHandle(uint32_t h) {
    return HleThreadFromHandle(h) || GetSync(h)!=nullptr;
}
bool WaitHandleReady(uint32_t h) {
    if (auto* thread=HleThreadFromHandle(h))
        return thread->status==rt::GuestThreadStatus::Exited;
    auto object=GetSync(h);
    if (!object) return false;
    if (object->type==Kind::Event || object->type==Kind::Timer) return object->signaled;
    if (object->type==Kind::Semaphore) return object->count>0;
    return !object->ownerThread || object->ownerThread==GuestTid();
}
uint32_t AcquireWaitHandle(uint32_t h) {
    return HleThreadFromHandle(h) ? 0 : TryAcquire(h);
}
void ClearWaitRetry(rt::GuestThread* current) {
    if(!current) return;
    current->retryWaitToken=current->timedOutHandle=0;
    current->retryWaitDeadlineUs=0;
}
HostCallOutcome FinishObjectWait(rt::GuestThread* current,uint32_t result) {
    ClearWaitRetry(current);
    return Ret(result);
}
// An unfinished finite wait is parked in the scheduler, then the guest
// retries the same HLE import upon signal or timeout. This avoids blocking
// the host/render thread and keeps the x86 stack unchanged until completion.
HostCallOutcome WaitSingle(cpu::CpuState& c, cpu::IGuestMemory& m,
                           uint32_t handle, uint32_t milliseconds) {
    constexpr uint32_t kWaitFailed=0xffffffffu;
    constexpr uint32_t kWaitTimeout=0x102u;
    rt::GuestThread* current=rt::g_process ?
        rt::g_process->Scheduler().Current() : nullptr;
    auto finish=[&](uint32_t result) -> HostCallOutcome {
        return FinishObjectWait(current,result);
    };
    if (!ValidateWaitObjects(m,{handle},false)) return finish(kWaitFailed);
    if (current && (current->timedOutHandle==handle ||
        (current->retryWaitToken==handle && current->retryWaitDeadlineUs &&
         rt::Clock::NowUs()>=current->retryWaitDeadlineUs))) {
        current->timedOutHandle=0;
        return finish(kWaitTimeout);
    }
    if (WaitHandleReady(handle)) return finish(AcquireWaitHandle(handle));
    if (milliseconds==0 || !current) return finish(kWaitTimeout);
    if (!rt::g_process->Scheduler().WaitFor(*current,handle,milliseconds))
        return finish(kWaitFailed);
    HostCallOutcome pending;
    pending.customReturn=true;
    return pending;
}
HostCallOutcome K32_WaitForSingleObject(cpu::CpuState& c, cpu::IGuestMemory& m) {
    return WaitSingle(c,m,Arg(c,m,0),Arg(c,m,1));
}
HostCallOutcome K32_WaitForSingleObjectEx(cpu::CpuState& c, cpu::IGuestMemory& m) {
    if (Arg(c,m,2)) return Ret(AlertableObjectWait(c,m,{Arg(c,m,0)},false,Arg(c,m,1)));
    return WaitSingle(c,m,Arg(c,m,0),Arg(c,m,1));
}
// Preflight all objects before any auto-reset/token/ownership mutation.
HostCallOutcome WaitMultiple(cpu::CpuState& c, cpu::IGuestMemory& m) {
    constexpr uint32_t kWaitFailed = 0xffffffffu;
    constexpr uint32_t kWaitTimeout = 0x102u;
    const uint32_t n = Arg(c,m,0), arr = Arg(c,m,1);
    const bool all = Arg(c,m,2) != 0;
    const uint32_t ms = Arg(c,m,3);
    rt::GuestThread* current=rt::g_process ?
        rt::g_process->Scheduler().Current() : nullptr;
    const auto finish=[&](uint32_t result) { return FinishObjectWait(current,result); };
    std::vector<uint32_t> handles;
    // Every terminal result, including preflight failure AFTER a wake, ends
    // this wait transaction. Only a parked customReturn may retain retry state.
    if (!ReadWaitObjects(m,n,arr,all,64,false,handles)) return finish(kWaitFailed);
    const uint32_t token=handles.front();
    if (current && (current->timedOutHandle==token ||
        (current->retryWaitToken==token && current->retryWaitDeadlineUs &&
         rt::Clock::NowUs()>=current->retryWaitDeadlineUs))) return finish(kWaitTimeout);
    const uint32_t result=PollWaitObjects(handles,all);
    if (result!=kWaitTimeout) return finish(result);
    if (ms==0 || !current || !rt::g_process) return finish(kWaitTimeout);
    if (!rt::g_process->Scheduler().WaitForMultiple(*current,handles,all,ms))
        return finish(kWaitFailed);
    HostCallOutcome pending;
    pending.customReturn=true;
    return pending;
}
HostCallOutcome K32_WaitForMultipleObjects(cpu::CpuState& c, cpu::IGuestMemory& m) {
    return WaitMultiple(c,m);
}
HostCallOutcome K32_WaitForMultipleObjectsEx(cpu::CpuState& c,cpu::IGuestMemory& m) {
    if (!Arg(c,m,4)) return WaitMultiple(c,m);
    std::vector<uint32_t> handles;
    const bool all=Arg(c,m,2)!=0;
    if (!ReadWaitObjects(m,Arg(c,m,0),Arg(c,m,1),all,64,false,handles))
        return FinishObjectWait(rt::g_process ? rt::g_process->Scheduler().Current() : nullptr,0xffffffffu);
    return Ret(AlertableObjectWait(c,m,handles,all,Arg(c,m,3)));
}

HostCallOutcome CooperativeSleep(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t duration=Arg(c,m,0);
    rt::GuestThread* current=rt::g_process ? rt::g_process->Scheduler().Current():nullptr;
    if(current) rt::g_process->Scheduler().Sleep(*current,duration);
    else rt::Clock::Sleep(duration); // legacy standalone HLE calls
    return Ret(0); // stdcall completes before the sleeping thread yields
}
HostCallOutcome K32_Sleep(cpu::CpuState& c, cpu::IGuestMemory& m) { return CooperativeSleep(c,m); }
HostCallOutcome K32_SleepEx(cpu::CpuState& c, cpu::IGuestMemory& m) {
    if (!Arg(c,m,1)) return CooperativeSleep(c,m);
    return Ret(AlertableObjectWait(c,m,{},false,Arg(c,m,0),true));
}
HostCallOutcome K32_SwitchToThread(cpu::CpuState&, cpu::IGuestMemory&) {
    if(!rt::g_process) return Ret(0);
    rt::GuestThread* current=rt::g_process->Scheduler().Current();
    if(!current) return Ret(0);
    current->yieldRequested=true;
    return Ret(rt::g_process->Scheduler().Size()>1 ? 1u:0u);
}

const HostFunctionEntry kEntries[] = {
    {"KERNEL32.dll", "InitializeCriticalSection", 4, CallConv::Stdcall, &K32_InitializeCriticalSection},
    {"KERNEL32.dll", "InitializeCriticalSectionAndSpinCount", 8, CallConv::Stdcall, &K32_InitializeCriticalSectionAndSpinCount},
    {"KERNEL32.dll", "InitializeCriticalSectionEx", 12, CallConv::Stdcall, &K32_InitializeCriticalSectionEx},
    {"KERNEL32.dll", "DeleteCriticalSection", 4, CallConv::Stdcall, &K32_DeleteCriticalSection},
    {"KERNEL32.dll", "EnterCriticalSection", 4, CallConv::Stdcall, &K32_EnterCriticalSection},
    {"KERNEL32.dll", "TryEnterCriticalSection", 4, CallConv::Stdcall, &K32_TryEnterCriticalSection},
    {"KERNEL32.dll", "LeaveCriticalSection", 4, CallConv::Stdcall, &K32_LeaveCriticalSection},
    {"KERNEL32.dll", "SetCriticalSectionSpinCount", 8, CallConv::Stdcall, &K32_SetCriticalSectionSpinCount},
    {"KERNEL32.dll", "InterlockedIncrement", 4, CallConv::Stdcall, &K32_InterlockedIncrement},
    {"KERNEL32.dll", "InterlockedDecrement", 4, CallConv::Stdcall, &K32_InterlockedDecrement},
    {"KERNEL32.dll", "InterlockedExchange", 8, CallConv::Stdcall, &K32_InterlockedExchange},
    {"KERNEL32.dll", "InterlockedExchangeAdd", 8, CallConv::Stdcall, &K32_InterlockedExchangeAdd},
    {"KERNEL32.dll", "InterlockedCompareExchange", 12, CallConv::Stdcall, &K32_InterlockedCompareExchange},
    {"KERNEL32.dll", "SetEvent", 4, CallConv::Stdcall, &K32_SetEvent},
    {"KERNEL32.dll", "ResetEvent", 4, CallConv::Stdcall, &K32_ResetEvent},
    {"KERNEL32.dll", "PulseEvent", 4, CallConv::Stdcall, &K32_ResetEvent},
    {"KERNEL32.dll", "ReleaseMutex", 4, CallConv::Stdcall, &K32_ReleaseMutex},
    {"KERNEL32.dll", "ReleaseSemaphore", 12, CallConv::Stdcall, &K32_ReleaseSemaphore},
    {"KERNEL32.dll", "SignalObjectAndWait", 16, CallConv::Stdcall, &K32_SignalObjectAndWait},
    {"KERNEL32.dll", "WaitForSingleObject", 8, CallConv::Stdcall, &K32_WaitForSingleObject},
    {"KERNEL32.dll", "WaitForSingleObjectEx", 12, CallConv::Stdcall, &K32_WaitForSingleObjectEx},
    {"KERNEL32.dll", "WaitForMultipleObjects", 16, CallConv::Stdcall, &K32_WaitForMultipleObjects},
    {"KERNEL32.dll", "WaitForMultipleObjectsEx", 20, CallConv::Stdcall, &K32_WaitForMultipleObjectsEx},
    {"KERNEL32.dll", "Sleep", 4, CallConv::Stdcall, &K32_Sleep},
    {"KERNEL32.dll", "SleepEx", 8, CallConv::Stdcall, &K32_SleepEx},
    {"KERNEL32.dll", "SwitchToThread", 0, CallConv::Stdcall, &K32_SwitchToThread},
};
const bool kRegistered = RegisterHostFunctions(kEntries, sizeof(kEntries) / sizeof(kEntries[0]));

} // namespace

bool ValidateWaitObjects(cpu::IGuestMemory& m,const std::vector<uint32_t>& handles,bool all) {
    rt::PollWaitableTimers();
    for (size_t i=0;i<handles.size();++i) {
        const uint32_t h=handles[i];
        if (!ValidWaitHandle(h)) { hle::SetLastError(m,6); return false; }
        if (!hle::HandleAccess(m,h,rt::kSynchronize)) return false;
        for (size_t j=0;j<i;++j) {
            const uint32_t previous=handles[j];
            auto object=rt::Handles().Get(h),old=rt::Handles().Get(previous);
            auto* thread=HleThreadFromHandle(h);
            if (h==previous || (all && ((object && object==old) ||
                (thread && thread==HleThreadFromHandle(previous))))) {
                hle::SetLastError(m,87);return false;
            }
        }
    }
    return true;
}
bool ReadWaitObjects(cpu::IGuestMemory& m,uint32_t count,uint32_t array,
                     bool all,uint32_t maximum,bool allowEmpty,std::vector<uint32_t>& handles) {
    handles.clear();
    if (count>maximum || (!count && !allowEmpty) ||
        (count && (!array || !m.IsRangeMapped(array,count*4)))) {
        hle::SetLastError(m,87);return false;
    }
    for (uint32_t i=0;i<count;++i) handles.push_back(m.Read32(array+i*4));
    return ValidateWaitObjects(m,handles,all);
}
uint32_t PollWaitObjects(const std::vector<uint32_t>& handles,bool all) {
    if (all) {
        for (uint32_t h:handles) if (!WaitHandleReady(h)) return 0x102;
        uint32_t result=0;
        for (size_t i=0;i<handles.size();++i)
            if (AcquireWaitHandle(handles[i])==0x80 && !result) result=0x80+static_cast<uint32_t>(i);
        return result;
    }
    for (size_t i=0;i<handles.size();++i)
        if (WaitHandleReady(handles[i])) return AcquireWaitHandle(handles[i])+static_cast<uint32_t>(i);
    return 0x102;
}
uint32_t AlertableObjectWait(cpu::CpuState& state,cpu::IGuestMemory& memory,
    const std::vector<uint32_t>& handles,bool all,uint32_t milliseconds,bool sleepOnly,bool enableApcs) {
    AlertableScope alertable(enableApcs);
    auto* current=rt::g_process ? rt::g_process->Scheduler().Current() : nullptr;
    struct ClearRetry {
        rt::GuestThread* thread;
        ~ClearRetry() { ClearWaitRetry(thread); }
    } cleanup{current};
    const bool finite=milliseconds!=0xffffffffu;
    const uint64_t deadline=finite ? rt::Clock::NowUs()+uint64_t(milliseconds)*1000 : 0;
    for (;;) {
        if (!rt::GuestExecution::Checkpoint()) return 0xffffffffu;
        if (!ValidateWaitObjects(memory,handles,all)) return 0xffffffffu;
        if (enableApcs) {
            if (DispatchUserApcs(state,memory)) return 0xc0;
            // Dispatch can yield, then a peer can cancel the queued timer APC
            // without any callback being delivered. Recheck both lifecycle and
            // HANDLE set after returning, before consuming tokens or parking.
            if (!rt::GuestExecution::Checkpoint()) return 0xffffffffu;
            if (!sleepOnly && !ValidateWaitObjects(memory,handles,all)) return 0xffffffffu;
        }
        if (!sleepOnly) {
            uint32_t result=PollWaitObjects(handles,all);
            if (result!=0x102) return result;
        }
        const uint64_t now=rt::Clock::NowUs();
        if (finite && now>=deadline) {
            if (sleepOnly && !milliseconds && current) current->yieldRequested=true;
            return sleepOnly ? 0 : 0x102;
        }
        if (!rt::GuestExecution::Active() || !current) return sleepOnly ? 0 : 0x102;
        const uint32_t remaining=finite ? static_cast<uint32_t>((deadline-now+999)/1000) : 0xffffffffu;
        current->retryWaitToken=current->timedOutHandle=0;
        current->retryWaitDeadlineUs=0;
        auto& scheduler=rt::g_process->Scheduler();
        if (sleepOnly) scheduler.WaitFor(*current,0xfffffff3u,remaining);
        else scheduler.WaitForMultiple(*current,handles,all,remaining);
        rt::GuestExecution::Yield();
    }
}

} // namespace win32
