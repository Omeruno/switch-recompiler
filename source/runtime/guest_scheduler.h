#pragma once
// Cooperative guest-thread scheduler. It does not create host OS threads.
// A quantum is an instruction budget; callers execute the selected
// GuestThread::cpu and then explicitly yield to the scheduler.
#include <cstddef>
#include <cstdint>
#include <vector>
#include "guest_thread.h"
#include "clock.h"

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
        WakeExpiredSleepers();
        if (threads_.empty()) return nullptr;
        for (size_t searched = 0; searched < threads_.size(); ++searched) {
            size_t index = (cursor_ + searched) % threads_.size();
            if (threads_[index]->IsRunnable()) {
                cursor_ = (index + 1) % threads_.size();
                current_ = threads_[index];
                return threads_[index];
            }
        }
        current_ = nullptr;
        return nullptr;
    }

    GuestThread* Current() const { return current_; }
    // Restore the host-visible running thread after a nested callback or
    // host-only thread probe without disturbing round-robin cursor.
    void SetCurrent(GuestThread* thread) { current_ = thread; }

    struct QuantumResult {
        uint32_t threadId = 0;
        uint32_t executed = 0;
        bool stoppedByCallback = false;
    };

    // Execute at most 'budget' instructions in one selected guest thread.
    // step(thread) must execute precisely one guest instruction and return
    // true if execution may continue; false means yield/stop. The scheduler
    // never touches the host clock or calls a blocking OS wait.
    // The callback may change the guest lifecycle (Wait/Exit/Suspend);
    // observe that immediately before executing the next instruction.
    template <typename Step>
    QuantumResult RunQuantum(Step&& step, uint32_t budget = kDefaultQuantum) {
        QuantumResult result{};
        GuestThread* thread = Next();
        if (!thread) return result;
        result.threadId = thread->id;
        for (uint32_t n = 0; n < budget && thread->IsRunnable(); ++n) {
            bool keepRunning = step(*thread);
            ++result.executed;
            if (!keepRunning) {
                result.stoppedByCallback = true;
                break;
            }
        }
        return result;
    }

    // Sleep is time-based, not object-based. The guest may continue after
    // the deadline without blocking the host or the other virtual CPUs.
    void Sleep(GuestThread& thread, uint32_t milliseconds) {
        if (milliseconds == 0) {
            thread.yieldRequested = true;
            return;
        }
        thread.sleeping = true;
        thread.sleepDeadlineUs = Clock::NowUs() + uint64_t(milliseconds) * 1000u;
        thread.waitObject = 0;
        thread.status = GuestThreadStatus::Waiting;
    }

    size_t WakeExpiredSleepers() {
        const uint64_t now=Clock::NowUs();
        size_t woke=0;
        for (GuestThread* t : threads_) {
            if (!t->sleeping || t->status!=GuestThreadStatus::Waiting ||
                now<t->sleepDeadlineUs) continue;
            t->sleeping=false;
            t->sleepDeadlineUs=0;
            t->status=GuestThreadStatus::Runnable;
            ++woke;
        }
        return woke;
    }

    bool HasSleepingThreads() const {
        for(const GuestThread* t: threads_)
            if(t->sleeping && t->status==GuestThreadStatus::Waiting) return true;
        return false;
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
        thread.sleeping=false;
        thread.sleepDeadlineUs=0;
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
        thread.sleeping = false;
        thread.sleepDeadlineUs = 0;
        thread.yieldRequested = false;
        thread.suspendCount = 0;
        // Thread HANDLE becomes signaled when its guest thread exits.
        Signal(0xE1000000u | (thread.id & 0x00FFFFFFu), true);
    }

    size_t Size() const { return threads_.size(); }
    uint32_t Quantum() const { return kDefaultQuantum; }

private:
    std::vector<GuestThread*> threads_;
    size_t cursor_ = 0;
    GuestThread* current_ = nullptr;
};
} // namespace rt
