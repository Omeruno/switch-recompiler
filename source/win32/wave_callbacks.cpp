#include "wave_callbacks.h"
#include "hle_util.h"
#include "../runtime/guest_execution.h"
#include <deque>
namespace win32 {
namespace {
constexpr size_t MaxNotifications=128;
constexpr uint32_t WaitToken=0x7ffd0001;
struct Notification { uint32_t routine,handle,message,instance,header; };
struct Dispatcher {
    std::deque<Notification> queue;
    uint32_t thread=0;
    bool parking=false,stopped=false,failed=false;
};
Dispatcher& State(){return rt::LocalState<Dispatcher>();}
HostCallOutcome Worker(cpu::CpuState& c,cpu::IGuestMemory& m) {
    auto& state=State();
    auto& process=*rt::g_process;
    auto& thread=*process.Scheduler().Current();
    // This frame belongs to the worker's continuation. A blocking callback
    // parks that continuation; peers keep their own CPU/TEB/native stack.
    while(rt::GuestExecution::Checkpoint() && !state.stopped) {
        if(state.queue.empty()) {
            state.parking=true;
            process.Scheduler().Wait(thread,WaitToken);
            rt::GuestExecution::Yield();
            state.parking=false;
            continue;
        }
        const auto item=state.queue.front();state.queue.pop_front();
        cpu::CpuState local=c;
        CallGuestFunctionStdcall(local,m,item.routine,
            {item.handle,item.message,item.instance,item.header,0},
            thread.teb,thread.teb,0x7fffffff);
    }
    state.failed=true;
    state.queue.clear();
    return hle::Ret(0);
}
const HostFunctionEntry entries[]={
    {"INTERNAL","WaveCallbackWorker",4,CallConv::Stdcall,&Worker},
};
const bool registered=RegisterHostFunctions(entries,1);
}
bool WaveCallbacksHealthy() {
    if(!rt::g_process)return false;
    auto& state=State();auto* thread=rt::g_process->FindThread(state.thread);
    return !state.stopped && !state.failed && thread &&
        thread->status!=rt::GuestThreadStatus::Exited && !thread->exitRequested;
}
bool PrepareWaveCallbacks() {
    if(!rt::g_process || !rt::GuestExecution::Active())return false;
    auto& state=State();
    if(state.thread)return WaveCallbacksHealthy();
    const int index=FindHostFunction("INTERNAL","WaveCallbackWorker");
    if(index<0)return false;
    std::string error;
    auto* thread=rt::g_process->CreateGuestThread(IndexToHostAddress(index),0,0,false,error);
    if(!thread)return false;
    thread->background=true;state.thread=thread->id;
    return true;
}
bool QueueWaveCallback(uint32_t routine,uint32_t handle,uint32_t message,
                       uint32_t instance,uint32_t header) {
    if(!WaveCallbacksHealthy())return false;
    auto& state=State();
    if(state.queue.size()>=MaxNotifications)return false;
    state.queue.push_back({routine,handle,message,instance,header});
    // Never interrupt a callback's own guest wait. Only the idle dispatcher
    // wait belongs to us; its pending queue is otherwise serviced on return.
    if(state.parking)
        rt::g_process->Scheduler().InterruptWait(*rt::g_process->FindThread(state.thread));
    return true;
}
void EndWaveCallbacks() {
    if(!rt::g_process)return;
    auto& state=State();state.stopped=true;state.queue.clear();
}
}
