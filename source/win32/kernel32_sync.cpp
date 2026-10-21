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

namespace {

using namespace hle;

constexpr uint32_t kCurrentThreadId = 0x1004;

// CRITICAL_SECTION: DebugInfo, LockCount, RecursionCount, OwningThread, LockSemaphore, SpinCount
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
HostCallOutcome K32_DeleteCriticalSection(cpu::CpuState&, cpu::IGuestMemory&) { return Ret(0); }
HostCallOutcome K32_EnterCriticalSection(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t cs = Arg(c, m, 0);
    m.Write32(cs + 4, m.Read32(cs + 4) + 1);
    m.Write32(cs + 8, m.Read32(cs + 8) + 1);
    m.Write32(cs + 12, kCurrentThreadId);
    return Ret(0);
}
HostCallOutcome K32_TryEnterCriticalSection(cpu::CpuState& c, cpu::IGuestMemory& m) { K32_EnterCriticalSection(c, m); return Ret(1); }
HostCallOutcome K32_LeaveCriticalSection(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t cs = Arg(c, m, 0);
    uint32_t rec = m.Read32(cs + 8);
    if (rec) {
        m.Write32(cs + 8, rec - 1);
        m.Write32(cs + 4, m.Read32(cs + 4) - 1);
        if (rec == 1) m.Write32(cs + 12, 0);
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
    WaitObject o{WaitObject::Mutex, false, Arg(c, m, 1) == 0, 0, 0};
    SetLastError(m, ERROR_SUCCESS);
    return Ret(NewObject(o));
}
HostCallOutcome K32_ReleaseMutex(cpu::CpuState& c, cpu::IGuestMemory& m) {
    auto it = Objects().find(Arg(c, m, 0));
    if (it == Objects().end()) return Ret(0);
    it->second.signaled = true;
    return Ret(1);
}
HostCallOutcome K32_CreateSemaphoreA(cpu::CpuState& c, cpu::IGuestMemory& m) {
    WaitObject o{WaitObject::Semaphore, false, false, static_cast<int32_t>(Arg(c, m, 1)), static_cast<int32_t>(Arg(c, m, 2))};
    return Ret(NewObject(o));
}
HostCallOutcome K32_ReleaseSemaphore(cpu::CpuState& c, cpu::IGuestMemory& m) {
    auto it = Objects().find(Arg(c, m, 0));
    if (it == Objects().end()) return Ret(0);
    uint32_t prev = Arg(c, m, 2);
    if (prev) m.Write32(prev, static_cast<uint32_t>(it->second.count));
    it->second.count += static_cast<int32_t>(Arg(c, m, 1));
    if (it->second.count > it->second.maxCount) it->second.count = it->second.maxCount;
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
            o.signaled = false; // рекурсивный захват своим же потоком разрешён
            return 0;
        case WaitObject::Semaphore:
            if (o.count <= 0) return 0x102;
            o.count--;
            return 0;
    }
    return 0;
}
HostCallOutcome WaitSingle(cpu::CpuState& c, cpu::IGuestMemory& m, uint32_t handle, uint32_t milliseconds) {
    constexpr uint32_t kWaitFailed=0xffffffffu;
    constexpr uint32_t kWaitTimeout=0x102u;
    // Thread handles belong to the guest scheduler. Never report an
    // unfinished worker as signaled, and never block the host render loop.
    if ((handle & 0xff000000u)==0xE1000000u && rt::g_process) {
        rt::GuestThread* target=rt::g_process->FindThread(handle & 0x00ffffffu);
        if (!target || !HleIsOpenThreadHandle(handle)) {
            SetLastError(m,ERROR_INVALID_HANDLE); return Ret(kWaitFailed);
        }
        if (target->status==rt::GuestThreadStatus::Exited) return Ret(0);
        if (milliseconds==0) return Ret(kWaitTimeout);
        rt::GuestThread* current=rt::g_process->Scheduler().Current();
        if (!current) return Ret(kWaitTimeout); // no active dispatch context
        if (milliseconds!=0xffffffffu) return Ret(kWaitTimeout); // timed waits pending
        if (!rt::g_process->Scheduler().Wait(*current,handle))
            return Ret(kWaitFailed);
        // Do not advance EIP/ESP. After target exits, scheduler wakes this
        // thread, and HLE retries the same WAIT call and returns WAIT_OBJECT_0.
        HostCallOutcome pending;
        pending.customReturn=true;
        return pending;
    }
    // Other currently implemented kernel objects: event, mutex, semaphore.
    auto it=Objects().find(handle);
    if(it==Objects().end()) {
        SetLastError(m,ERROR_INVALID_HANDLE);
        return Ret(kWaitFailed);
    }
    uint32_t acquired=TryAcquire(handle);
    if(acquired!=kWaitTimeout || milliseconds==0) return Ret(acquired);
    // Positive finite durations require an independent deadline; do not
    // incorrectly pretend they waited. Support infinite waits first.
    if(milliseconds!=0xffffffffu || !rt::g_process) return Ret(kWaitTimeout);
    rt::GuestThread* current=rt::g_process->Scheduler().Current();
    if(!current) return Ret(kWaitTimeout);
    if(!rt::g_process->Scheduler().Wait(*current,handle)) return Ret(kWaitFailed);
    HostCallOutcome pending;
    pending.customReturn=true;
    return pending;
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

HostCallOutcome K32_Sleep(cpu::CpuState& c, cpu::IGuestMemory& m) { rt::Clock::Sleep(Arg(c, m, 0)); return Ret(0); }
HostCallOutcome K32_SleepEx(cpu::CpuState& c, cpu::IGuestMemory& m) { rt::Clock::Sleep(Arg(c, m, 0)); return Ret(0); }
HostCallOutcome K32_SwitchToThread(cpu::CpuState&, cpu::IGuestMemory&) { return Ret(0); }

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
