#pragma once
// Recoverable guest exception boundary for loader initialization callbacks.
// Native failures/unsupported HLE operations are deliberately not intercepted.
#include "guest_thread.h"
#include "../cpu/guest_memory.h"

namespace rt {
class GuestFaultBoundary {
public:
    GuestFaultBoundary(GuestThread& owner,cpu::PagedGuestMemory& memory);
    ~GuestFaultBoundary();
    GuestFaultBoundary(const GuestFaultBoundary&)=delete;
    GuestFaultBoundary& operator=(const GuestFaultBoundary&)=delete;
    bool Failed() const { return record_.fault.pending; }
    const GuestCallbackFault& Fault() const { return record_.fault; }
private:
    GuestThread& owner_;
    cpu::PagedGuestMemory& memory_;
    GuestFaultRecord record_{};
    uint32_t savedSeh_=0xffffffffu;
};
bool GuestFaultPending();
bool GuestFaultBoundaryActive();
// Capture only after guest SEH declines the exception. First fault wins while
// the saved native stack unwinds; an inner handler fault cannot be overwritten.
bool CaptureGuestFault(uint32_t code,uint32_t eip,uint32_t address,uint32_t flags=0);
} // namespace rt
