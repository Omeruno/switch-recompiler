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
    // Cooperative Sleep: monotonic guest-clock deadline; no host thread sleep.
    uint64_t sleepDeadlineUs = 0;
    bool sleeping = false;
    bool yieldRequested = false;
    GuestThreadStatus status = GuestThreadStatus::Created;
    cpu::CpuState cpu{};

    // Nested exception handlers belong to a thread, never the process.
    // Used by x86 SEH while dispatching guest callbacks recursively.
    struct ActiveSehFrame { uint32_t frame = 0; };
    ActiveSehFrame activeSeh[16] = {};
    uint32_t activeSehDepth = 0;

    bool IsRunnable() const {
        return status == GuestThreadStatus::Runnable && suspendCount == 0;
    }
};
} // namespace rt
