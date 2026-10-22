#pragma once
// Guest thread metadata for 32-bit Windows XP user mode.
// Each guest has its own CPU/TEB/stack; GuestScheduler interleaves quanta.
#include <cstdint>
#include <vector>
#include <utility>
#include <deque>
#include <memory>
#include "../cpu/cpu_state.h"

namespace rt {
struct TimerObject;
enum class GuestThreadStatus : uint8_t { Created, Runnable, Waiting, Suspended, Exited };

struct GuestThread {
    uint32_t id = 0;
    uint32_t teb = 0;
    uint32_t stackLimit = 0;
    uint32_t stackBase = 0;
    uint32_t tlsArray = 0;
    // Host-owned mappings, independent of guest-writable TEB/TLS pointers.
    // Metadata survives exit for HANDLE queries; mappings do not.
    std::vector<std::pair<uint32_t,uint32_t>> memoryRegions;
    bool memoryReleased = false;
    bool needsThreadAttach = false;
    bool exitRequested = false;
    uint32_t requestedExitCode = 0;
    bool detaching = false;
    bool detachAborted = false;
    struct UserApc {
        uint32_t routine=0, data=0, timeLow=0, timeHigh=0;
        bool timerApc=false;
        uint64_t generation=0;
        std::weak_ptr<TimerObject> timer;
    };
    std::deque<UserApc> pendingApcs;
    bool alertableWait = false;
    uint32_t exitCode = 259; // STILL_ACTIVE
    uint32_t suspendCount = 0;
    uint32_t waitObject = 0;
    // Cooperative Sleep: monotonic guest-clock deadline; no host thread sleep.
    uint64_t sleepDeadlineUs = 0;
    bool sleeping = false;
    bool yieldRequested = false;
    uint64_t waitDeadlineUs = 0;
    uint32_t timedOutHandle = 0;
    std::vector<uint32_t> waitHandles; // multiple-object wait membership
    bool waitAll = false;
    // Preserve the original absolute deadline when a wait is signaled but
    // the resumed HLE call must retry (e.g. incomplete wait-all).
    uint64_t retryWaitDeadlineUs = 0;
    uint32_t retryWaitToken = 0;
    GuestThreadStatus status = GuestThreadStatus::Created;
    cpu::CpuState cpu{};

    // Nested exception handlers belong to a thread, never the process.
    // Used by x86 SEH while dispatching guest callbacks recursively.
    struct ActiveSehFrame { uint32_t frame = 0; };
    ActiveSehFrame activeSeh[16] = {};
    uint32_t activeSehDepth = 0;
    uint32_t callbackDepth = 0; // native guest-call frames parked on this guest's fiber

    bool IsRunnable() const {
        return status == GuestThreadStatus::Runnable && suspendCount == 0;
    }
    bool ShouldUnwind() const {
        return status==GuestThreadStatus::Exited ||
            (exitRequested && (!detaching || detachAborted));
    }
};
} // namespace rt
