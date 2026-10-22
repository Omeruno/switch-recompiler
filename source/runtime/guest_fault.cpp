#include "guest_fault.h"
#include "process.h"

namespace rt {
namespace {
GuestThread* CurrentFaultThread() {
    if(!g_process)return nullptr;
    if(auto* current=g_process->Scheduler().Current())return current;
    return &g_process->MainThread(); // standalone initialization/host inspectors
}
}
GuestFaultBoundary::GuestFaultBoundary(GuestThread& owner,cpu::PagedGuestMemory& memory)
    : owner_(owner),memory_(memory) {
    record_.previous=owner_.guestFaultBoundary;
    if(owner_.teb && memory_.IsRangeMapped(owner_.teb,4))savedSeh_=memory_.Read32(owner_.teb);
    owner_.guestFaultBoundary=&record_;
}
GuestFaultBoundary::~GuestFaultBoundary() {
    // A faulted guest callback did not execute its normal SEH epilogue. Drop
    // its stack registrations before returning to a live LoadLibrary caller.
    if(record_.fault.pending && owner_.teb && !owner_.memoryReleased &&
       memory_.IsRangeMapped(owner_.teb,4))memory_.Write32(owner_.teb,savedSeh_);
    owner_.guestFaultBoundary=record_.previous;
}
bool GuestFaultBoundaryActive() {
    auto* thread=CurrentFaultThread();return thread && thread->guestFaultBoundary;
}
bool GuestFaultPending() {
    auto* thread=CurrentFaultThread();return thread && thread->HasGuestFault();
}
bool CaptureGuestFault(uint32_t code,uint32_t eip,uint32_t address,uint32_t flags) {
    auto* thread=CurrentFaultThread();
    if(!thread || !thread->guestFaultBoundary)return false;
    if(!thread->HasGuestFault()) {
        auto& fault=thread->guestFaultBoundary->fault;
        fault.pending=true;fault.code=code;fault.eip=eip;fault.address=address;fault.flags=flags;
    }
    return true;
}
} // namespace rt
