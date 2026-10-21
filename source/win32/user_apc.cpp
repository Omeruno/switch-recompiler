#include "user_apc.h"
#include "handle_util.h"
#include "../runtime/guest_execution.h"
namespace win32 {
rt::GuestThread* HleThreadFromHandle(uint32_t handle);
AlertableScope::AlertableScope(bool enabled)
    :thread_(rt::g_process ? rt::g_process->Scheduler().Current() : nullptr),
     previous_(thread_ && thread_->alertableWait) {
    if (thread_) thread_->alertableWait=enabled;
}
AlertableScope::~AlertableScope() { if (thread_) thread_->alertableWait=previous_; }
bool DispatchUserApcs(cpu::CpuState& state,cpu::IGuestMemory& memory) {
    auto* thread=rt::g_process ? rt::g_process->Scheduler().Current() : nullptr;
    if (!thread) return false;
    // An APC may block normally. A nested alertable wait explicitly re-enables
    // dispatch; merely queuing another APC must not interrupt a plain wait.
    AlertableScope dispatching(false);
    bool delivered=false;
    while (!thread->pendingApcs.empty() && rt::GuestExecution::Checkpoint()) {
        const auto apc=thread->pendingApcs.front();thread->pendingApcs.pop_front();
        cpu::CpuState local=state;
        delivered=true;
        CallGuestFunctionStdcall(local,memory,apc.routine,{apc.data},thread->teb,thread->teb,0x7fffffff);
    }
    return delivered;
}
namespace {
HostCallOutcome QueueUserAPC(cpu::CpuState& state,cpu::IGuestMemory& memory) {
    auto* target=HleThreadFromHandle(hle::Arg(state,memory,1));
    if (!target) { hle::SetLastError(memory,6); return hle::Ret(0); }
    if (!hle::HandleAccess(memory,hle::Arg(state,memory,1),0x10)) return hle::Ret(0);
    if (target->status==rt::GuestThreadStatus::Exited || target->exitRequested) {
        hle::SetLastError(memory,31); return hle::Ret(0);
    }
    const uint32_t routine=hle::Arg(state,memory,0);
    if (!routine) { hle::SetLastError(memory,87); return hle::Ret(0); }
    target->pendingApcs.push_back({routine,hle::Arg(state,memory,2)});
    if (target->alertableWait) rt::g_process->Scheduler().InterruptWait(*target);
    return hle::Ret(1);
}
const HostFunctionEntry entries[]={
    {"KERNEL32.dll","QueueUserAPC",12,CallConv::Stdcall,&QueueUserAPC},
};
const bool registered=RegisterHostFunctions(entries,1);
}
}
