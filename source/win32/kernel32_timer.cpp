#include "handle_util.h"
#include "../runtime/waitable_timer.h"
#include "../runtime/clock.h"
namespace win32 { namespace {
using namespace hle;
std::shared_ptr<rt::TimerObject> Timer(cpu::IGuestMemory& memory,uint32_t handle) {
    auto object=rt::Handles().Get(handle);
    if(!object || object->type!=rt::ObjectKind::Timer) { SetLastError(memory,6);return nullptr; }
    if(!HandleAccess(memory,handle,2)) return nullptr;
    return std::static_pointer_cast<rt::TimerObject>(object);
}
HostCallOutcome SetWaitableTimer(cpu::CpuState& c,cpu::IGuestMemory& m) {
    auto timer=Timer(m,Arg(c,m,0));if(!timer) return Ret(0);
    const uint32_t due=Arg(c,m,1),routine=Arg(c,m,3);
    const int32_t period=static_cast<int32_t>(Arg(c,m,2));
    auto* scheduler=rt::g_process ? &rt::g_process->Scheduler() : nullptr;
    auto* current=scheduler ? scheduler->Current() : nullptr;
    if(!due || !m.IsRangeMapped(due,8) || period<0 || (routine && (!current || current->exitRequested))) {
        SetLastError(m,87);return Ret(0);
    }
    const uint64_t bits=uint64_t(m.Read32(due))|(uint64_t(m.Read32(due+4))<<32);
    rt::PollWaitableTimers();
    rt::CancelTimer(*timer,scheduler);
    timer->signaled=false;timer->periodMs=static_cast<uint32_t>(period);
    timer->routine=routine;timer->callbackThread=routine ? current->id : 0;timer->data=Arg(c,m,4);
    timer->absoluteTime=0;
    if(bits>>63) timer->dueUs=rt::Clock::NowUs()+((~bits+1)+9)/10;
    else if(bits) timer->absoluteTime=bits;
    else timer->dueUs=rt::Clock::NowUs();
    timer->active=true;
    // Horizon power-management wake requests are unsupported, as allowed by API.
    if(Arg(c,m,5)) SetLastError(m,50);
    return Ret(1);
}
HostCallOutcome CancelWaitableTimer(cpu::CpuState& c,cpu::IGuestMemory& m) {
    auto timer=Timer(m,Arg(c,m,0));if(!timer) return Ret(0);
    rt::PollWaitableTimers();
    rt::CancelTimer(*timer,rt::g_process ? &rt::g_process->Scheduler() : nullptr);
    return Ret(1);
}
const HostFunctionEntry entries[]={
    {"KERNEL32.dll","SetWaitableTimer",24,CallConv::Stdcall,&SetWaitableTimer},
    {"KERNEL32.dll","CancelWaitableTimer",4,CallConv::Stdcall,&CancelWaitableTimer},
};
const bool registered=RegisterHostFunctions(entries,2);
} }
