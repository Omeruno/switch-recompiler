#include "user32.h"
#include "kernel_wait.h"
#include "user_apc.h"
#include "../runtime/guest_execution.h"

namespace win32 { namespace user {
uint32_t WaitForMessages(cpu::CpuState& state,cpu::IGuestMemory& memory,
    uint32_t count,uint32_t array,uint32_t milliseconds,uint32_t mask,uint32_t flags) {
    if (flags & ~7u) { hle::SetLastError(memory,87); return 0xffffffffu; }
    const bool all=(flags & 1)!=0,alertable=(flags & 2)!=0,inputAvailable=(flags & 4)!=0;
    std::vector<uint32_t> handles;
    if (!ReadWaitObjects(memory,count,array,all,63,true,handles)) return 0xffffffffu;
    AlertableScope alertableScope(alertable);
    auto& queue=Q();
    const bool finite=milliseconds!=0xffffffffu;
    const uint64_t deadline=finite ? rt::Clock::NowUs()+uint64_t(milliseconds)*1000 : 0;
    const auto finish=[&](uint32_t result) { queue.changed&=~mask; return result; };
    for (;;) {
        if (!rt::GuestExecution::Checkpoint()) return 0xffffffffu;
        if (!ValidateWaitObjects(memory,handles,all)) return 0xffffffffu;
        if (alertable && DispatchUserApcs(state,memory)) return 0xc0;
        {
            // Platform processing may enter native/guest window callbacks.
            AlertableScope processing(false);
            PollQueueEvents();
        }
        if (!rt::GuestExecution::Checkpoint()) return 0xffffffffu;
        if (!ValidateWaitObjects(memory,handles,all)) return 0xffffffffu;
        const uint32_t available=QueueStatusBits() & mask;
        const bool input=(available & (inputAvailable ? 0xffffffffu : queue.changed))!=0;
        if (!all || input) {
            const uint32_t result=PollWaitObjects(handles,all);
            if (result!=0x102) return finish(result);
        }
        if (!all && input) return finish(count);
        if (finite && rt::Clock::NowUs()>=deadline) return finish(0x102);
        if (rt::GuestExecution::Active()) {
            auto& scheduler=rt::g_process->Scheduler();
            // Periodic nonblocking input polling also keeps frontend events
            // alive when all guest threads are waiting. Never sleep the host
            // here while another guest CPU can run.
            scheduler.Sleep(*scheduler.Current(),1);
            rt::GuestExecution::Yield();
        } else rt::Clock::Sleep(1); // standalone synchronous HLE fallback
    }
}
} }
