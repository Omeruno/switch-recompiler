#pragma once
// Cooperative guest-thread scheduler. It does not create host OS threads.
// A quantum is an instruction budget; callers execute the selected
// GuestThread::cpu and then explicitly yield to the scheduler.
#include <cstddef>
#include <cstdint>
#include <vector>
#include "guest_thread.h"

namespace rt {
class GuestScheduler {
public:
    static constexpr uint32_t kDefaultQuantum = 4096;

    bool Add(GuestThread& thread) {
        if (!thread.id) return false;
        for (GuestThread* old : threads_)
            if (old->id == thread.id || old == &thread) return false;
        threads_.push_back(&thread);
        return true;
    }

    GuestThread* Find(uint32_t id) const {
        for (GuestThread* t : threads_) if (t->id == id) return t;
        return nullptr;
    }

    // Round-robin over runnable threads, starting immediately after the
    // last selected thread. Waiting, suspended and exited threads are skipped.
    GuestThread* Next() {
        if (threads_.empty()) return nullptr;
        for (size_t searched = 0; searched < threads_.size(); ++searched) {
            size_t index = (cursor_ + searched) % threads_.size();
            if (threads_[index]->IsRunnable()) {
                cursor_ = (index + 1) % threads_.size();
                return threads_[index];
            }
        }
        return nullptr;
    }

    uint32_t Suspend(GuestThread& thread) {
        uint32_t previous = thread.suspendCount;
        if (previous != UINT32_MAX && thread.status != GuestThreadStatus::Exited)
            ++thread.suspendCount;
        return previous;
    }

    // Returns previous suspend count; caller can distinguish an error by
    // checking the returned thread lifecycle state.
    uint32_t Resume(GuestThread& thread) {
        uint32_t previous = thread.suspendCount;
        if (thread.status != GuestThreadStatus::Exited && thread.suspendCount)
            --thread.suspendCount;
        return previous;
    }

    bool Wait(GuestThread& thread, uint32_t objectHandle) {
        if (!objectHandle || thread.status == GuestThreadStatus::Exited) return false;
        thread.waitObject = objectHandle;
        thread.status = GuestThreadStatus::Waiting;
        return true;
    }

    size_t Signal(uint32_t objectHandle, bool wakeAll) {
        size_t count = 0;
        for (GuestThread* thread : threads_) {
            if (thread->status != GuestThreadStatus::Waiting ||
                thread->waitObject != objectHandle) continue;
            thread->waitObject = 0;
            thread->status = GuestThreadStatus::Runnable;
            ++count;
            if (!wakeAll) break;
        }
        return count;
    }

    void Exit(GuestThread& thread, uint32_t code) {
        thread.exitCode = code;
        thread.status = GuestThreadStatus::Exited;
        thread.waitObject = 0;
        thread.suspendCount = 0;
    }

    size_t Size() const { return threads_.size(); }
    uint32_t Quantum() const { return kDefaultQuantum; }

private:
    std::vector<GuestThread*> threads_;
    size_t cursor_ = 0;
};
} // namespace rt
