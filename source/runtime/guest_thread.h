#pragma once
// Guest thread metadata for 32-bit Windows XP user mode.
// Process currently executes one guest CPU state externally. This model
// establishes per-thread ownership without introducing a scheduler yet.
#include <cstdint>
#include "../cpu/cpu_state.h"

namespace rt {
enum class GuestThreadStatus : uint8_t { Created, Runnable, Waiting, Suspended, Exited };

struct GuestThread {
    uint32_t id = 0;
    uint32_t teb = 0;
    uint32_t stackLimit = 0;
    uint32_t stackBase = 0;
    uint32_t tlsArray = 0;
    uint32_t exitCode = 259; // STILL_ACTIVE
    uint32_t suspendCount = 0;
    uint32_t waitObject = 0;
    GuestThreadStatus status = GuestThreadStatus::Created;
    cpu::CpuState cpu{};

    bool IsRunnable() const {
        return status == GuestThreadStatus::Runnable && suspendCount == 0;
    }
};
} // namespace rt
