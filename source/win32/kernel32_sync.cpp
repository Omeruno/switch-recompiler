// kernel32_sync.cpp — KERNEL32: синхронизация.
//
// Пока гость однопоточный: критическая секция никогда не ждёт, а только
// считает рекурсивные входы; объекты ожидания (события, мьютексы,
// семафоры) хранят состояние, и ожидание сразу возвращает результат.
// Когда появятся потоки (планировщик), ожидание будет переключать поток.

#include <map>

#include "hle_util.h"
#include "../runtime/clock.h"
#include "../runtime/process.h"

namespace win32 {

bool HleIsOpenThreadHandle(uint32_t handle); // kernel32_module.cpp

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
struct WaitObject {
    enum Kind { Event, Mutex, Semaphore } kind;
    bool manualReset = false;
    bool signaled = false;
    int32_t count = 0, maxCount = 0;
    uint32_t ownerThread = 0;
    uint32_t recursion = 0;
};
std::map<uint32_t, WaitObject>& Objects() {
    static std::map<uint32_t, WaitObject> objs;
    return objs;
}
uint32_t g_nextSyncHandle = 0x00100000u; // не пересекается с файловыми хендлами (0x100..)

uint32_t NewObject(const WaitObject& o) {
    uint32_t h = (g_nextSyncHandle += 4);
    Objects()[h] = o;
    return h;
}

HostCallOutcome K32_CreateEventA(cpu::CpuState& c, cpu::IGuestMemory& m) {
    WaitObject o{WaitObject::Event, Arg(c, m, 1) != 0, Arg(c, m, 2) != 0, 0, 0};
    return Ret(NewObject(o));
}
HostCallOutcome K32_SetEvent(cpu::CpuState& c, cpu::IGuestMemory& m) {
    auto it = Objects().find(Arg(c, m, 0));
    if (it == Objects().end()) { SetLastError(m, ERROR_INVALID_HANDLE); return Ret(0); }
    it->second.signaled = true;
    if (rt::g_process)
        rt::g_process->Scheduler().Signal(Arg(c,m,0),it->second.manualReset);
    return Ret(1);
}
HostCallOutcome K32_ResetEvent(cpu::CpuState& c, cpu::IGuestMemory& m) {
    auto it = Objects().find(Arg(c, m, 0));
    if (it == Objects().end()) { SetLastError(m, ERROR_INVALID_HANDLE); return Ret(0); }
    it->second.signaled = false;
    return Ret(1);
}
HostCallOutcome K32_CreateMutexA(cpu::CpuState& c, cpu::IGuestMemory& m) {
    const bool initiallyOwned=Arg(c,m,1)!=0;
    WaitObject o{WaitObject::Mutex, false, !initiallyOwned, 0, 0};
    if (initiallyOwned) {
        o.ownerThread=rt::g_process ? rt::g_process->CurrentThreadId() : kCurrentThreadId;
        o.recursion=1;
    }
    SetLastError(m, ERROR_SUCCESS);
    return Ret(NewObject(o));
}
HostCallOutcome K32_ReleaseMutex(cpu::CpuState& c, cpu::IGuestMemory& m) {
    const uint32_t handle=Arg(c,m,0);
    auto it=Objects().find(handle);
    if (it==Objects().end() || it->second.kind!=WaitObject::Mutex) {
        SetLastError(m,ERROR_INVALID_HANDLE);
        return Ret(0);
    }
    WaitObject& o=it->second;
    uint32_t current=rt::g_process ? rt::g_process->CurrentThreadId() : kCurrentThreadId;
    if (o.ownerThread!=current || !o.recursion) {
        SetLastError(m,288u); // ERROR_NOT_OWNER
        return Ret(0);
    }
    if (--o.recursion==0) {
        o.ownerThread=0;
        o.signaled=true;
        if(rt::g_process)rt::g_process->Scheduler().Signal(handle,false);
    }
    return Ret(1);
}
HostCallOutcome K32_CreateSemaphoreA(cpu::CpuState& c, cpu::IGuestMemory& m) {
    const int32_t initial=static_cast<int32_t>(Arg(c,m,1));
    const int32_t maximum=static_cast<int32_t>(Arg(c,m,2));
    if(initial<0 || maximum<=0 || initial>maximum) {
        SetLastError(m,ERROR_INVALID_PARAMETER);
        return Ret(0);
    }
    WaitObject o{WaitObject::Semaphore,false,false,initial,maximum};
    return Ret(NewObject(o));
}
HostCallOutcome K32_ReleaseSemaphore(cpu::CpuState& c, cpu::IGuestMemory& m) {
    const uint32_t handle=Arg(c,m,0);
    auto it=Objects().find(handle);
    if(it==Objects().end() || it->second.kind!=WaitObject::Semaphore) {
        SetLastError(m,ERROR_INVALID_HANDLE);
        return Ret(0);
    }
    WaitObject& o=it->second;
    const int32_t amount=static_cast<int32_t>(Arg(c,m,1));
    if(amount<=0 || amount>o.maxCount-o.count) {
        SetLastError(m,ERROR_INVALID_PARAMETER);
        return Ret(0);
    }
    uint32_t prev=Arg(c,m,2);
    if(prev) {
        if(!m.IsRangeMapped(prev,4)) { SetLastError(m,ERROR_INVALID_PARAMETER);return Ret(0); }
        m.Write32(prev,static_cast<uint32_t>(o.count));
    }
    o.count+=amount;
    if(rt::g_process) {
        for(int32_t i=0;i<amount;++i)
            if(rt::g_process->Scheduler().Signal(handle,false)==0) break;
    }
    return Ret(1);
}

// Однопоточный гость: ждать некого, поэтому сразу отвечаем по состоянию.
uint32_t TryAcquire(uint32_t h) {
    auto it = Objects().find(h);
    if (it == Objects().end()) return 0; // хендлы потоков/процессов/файлов: «уже сигнален»
    WaitObject& o = it->second;
    switch (o.kind) {
        case WaitObject::Event:
            if (!o.signaled) return 0x102; // WAIT_TIMEOUT
            if (!o.manualReset) o.signaled = false;
            return 0;
        case WaitObject::Mutex:
            {
                const uint32_t id=rt::g_process ? rt::g_process->CurrentThreadId() : kCurrentThreadId;
                if(o.ownerThread && o.ownerThread!=id) return 0x102;
                o.ownerThread=id;
                ++o.recursion;
                o.signaled=false;
                return 0;
            }
        case WaitObject::Semaphore:
            if (o.count <= 0) return 0x102;
            o.count--;
            return 0;
    }
    return 0;
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
    auto tryPark=[&]() -> HostCallOutcome {
        if (!current || !rt::g_process) return Ret(kWaitTimeout);
        if (!rt::g_process->Scheduler().WaitFor(*current,handle,milliseconds))
            return Ret(kWaitFailed);
        HostCallOutcome pending;
        pending.customReturn=true;
        return pending;
    };
    if ((handle & 0xff000000u)==0xE1000000u && rt::g_process) {
        rt::GuestThread* target=rt::g_process->FindThread(handle & 0x00ffffffu);
        if (!target || !HleIsOpenThreadHandle(handle)) {
            SetLastError(m,ERROR_INVALID_HANDLE);
            return Ret(kWaitFailed);
        }
        if(current && current->timedOutHandle==handle) {
            current->timedOutHandle=0;
            return Ret(kWaitTimeout);
        }
        if (target->status==rt::GuestThreadStatus::Exited) return Ret(0);
        if (milliseconds==0) return Ret(kWaitTimeout);
        return tryPark();
    }
    auto it=Objects().find(handle);
    if(it==Objects().end()) {
        SetLastError(m,ERROR_INVALID_HANDLE);
        return Ret(kWaitFailed);
    }
    if(current && current->timedOutHandle==handle) {
        current->timedOutHandle=0;
        return Ret(kWaitTimeout);
    }
    uint32_t acquired=TryAcquire(handle);
    if(acquired!=kWaitTimeout || milliseconds==0) return Ret(acquired);
    return tryPark();
}
HostCallOutcome K32_WaitForSingleObject(cpu::CpuState& c, cpu::IGuestMemory& m) {
    return WaitSingle(c,m,Arg(c,m,0),Arg(c,m,1));
}
HostCallOutcome K32_WaitForSingleObjectEx(cpu::CpuState& c, cpu::IGuestMemory& m) {
    return WaitSingle(c,m,Arg(c,m,0),Arg(c,m,1));
}
// DWORD WaitForMultipleObjects(DWORD n, const HANDLE*, BOOL waitAll, DWORD ms)
HostCallOutcome K32_WaitForMultipleObjects(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t n = Arg(c, m, 0), arr = Arg(c, m, 1), all = Arg(c, m, 2);
    if (all) {
        for (uint32_t i = 0; i < n; ++i) if (TryAcquire(m.Read32(arr + i * 4)) == 0x102) return Ret(0x102);
        return Ret(0);
    }
    for (uint32_t i = 0; i < n; ++i) if (TryAcquire(m.Read32(arr + i * 4)) == 0) return Ret(i);
    return Ret(0x102);
}

HostCallOutcome CooperativeSleep(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t duration=Arg(c,m,0);
    rt::GuestThread* current=rt::g_process ? rt::g_process->Scheduler().Current():nullptr;
    if(current) rt::g_process->Scheduler().Sleep(*current,duration);
    else rt::Clock::Sleep(duration); // legacy standalone HLE calls
    return Ret(0); // stdcall completes before the sleeping thread yields
}
HostCallOutcome K32_Sleep(cpu::CpuState& c, cpu::IGuestMemory& m) { return CooperativeSleep(c,m); }
HostCallOutcome K32_SleepEx(cpu::CpuState& c, cpu::IGuestMemory& m) { return CooperativeSleep(c,m); }
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
    {"KERNEL32.dll", "CreateEventA", 16, CallConv::Stdcall, &K32_CreateEventA},
    {"KERNEL32.dll", "CreateEventW", 16, CallConv::Stdcall, &K32_CreateEventA},
    {"KERNEL32.dll", "SetEvent", 4, CallConv::Stdcall, &K32_SetEvent},
    {"KERNEL32.dll", "ResetEvent", 4, CallConv::Stdcall, &K32_ResetEvent},
    {"KERNEL32.dll", "PulseEvent", 4, CallConv::Stdcall, &K32_ResetEvent},
    {"KERNEL32.dll", "CreateMutexA", 12, CallConv::Stdcall, &K32_CreateMutexA},
    {"KERNEL32.dll", "CreateMutexW", 12, CallConv::Stdcall, &K32_CreateMutexA},
    {"KERNEL32.dll", "ReleaseMutex", 4, CallConv::Stdcall, &K32_ReleaseMutex},
    {"KERNEL32.dll", "CreateSemaphoreA", 16, CallConv::Stdcall, &K32_CreateSemaphoreA},
    {"KERNEL32.dll", "CreateSemaphoreW", 16, CallConv::Stdcall, &K32_CreateSemaphoreA},
    {"KERNEL32.dll", "ReleaseSemaphore", 12, CallConv::Stdcall, &K32_ReleaseSemaphore},
    {"KERNEL32.dll", "WaitForSingleObject", 8, CallConv::Stdcall, &K32_WaitForSingleObject},
    {"KERNEL32.dll", "WaitForSingleObjectEx", 12, CallConv::Stdcall, &K32_WaitForSingleObjectEx},
    {"KERNEL32.dll", "WaitForMultipleObjects", 16, CallConv::Stdcall, &K32_WaitForMultipleObjects},
    {"KERNEL32.dll", "Sleep", 4, CallConv::Stdcall, &K32_Sleep},
    {"KERNEL32.dll", "SleepEx", 8, CallConv::Stdcall, &K32_SleepEx},
    {"KERNEL32.dll", "SwitchToThread", 0, CallConv::Stdcall, &K32_SwitchToThread},
};
const bool kRegistered = RegisterHostFunctions(kEntries, sizeof(kEntries) / sizeof(kEntries[0]));

} // namespace

bool HleCloseSyncHandle(uint32_t handle) { return Objects().erase(handle) != 0; }

} // namespace win32
