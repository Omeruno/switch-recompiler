#pragma once
// Drives the existing guest scheduler using one saved native stack per guest.
// A WndProc/SEH/CRT callback can park without recursively running its peers.
#include <functional>
#include <map>
#include <memory>
#include "host_fiber.h"
#include "process.h"

namespace rt {
class GuestExecution {
public:
    using Step = std::function<bool(GuestThread&)>;
    enum class Result { Ran, Idle, Stopped, AllocationFailed };
    GuestExecution(Process& process, Step step);
    ~GuestExecution();
    GuestExecution(const GuestExecution&) = delete;
    GuestExecution& operator=(const GuestExecution&) = delete;
    Result RunOne();
    bool Stopped() const { return stopped_; }
    // Called by nested guest loops as well as the outer instruction loop.
    // false means unwind native callbacks normally without more guest code.
    static bool Checkpoint();
    static bool Active();
    static void Yield();
private:
    struct Continuation {
        GuestExecution* owner=nullptr;
        GuestThread* thread=nullptr;
        HostFiber fiber;
    };
    static void Run(void* arg);
    bool CheckCurrent();
    void SuspendCurrent();
    void Resume(Continuation& continuation);
    void ReapExited();
    Process& process_;
    Step step_;
    HostFiber root_;
    std::map<uint32_t,std::unique_ptr<Continuation>> continuations_;
    Continuation* running_=nullptr;
    GuestExecution* previous_=nullptr;
    uint32_t operations_=0;
    bool stopped_=false;
};
} // namespace rt
