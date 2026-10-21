#pragma once
// Same-native-thread stackful context. No OS threads and no host exceptions.
#include <cstddef>
#include <cstdint>
#include <memory>
#if !defined(__aarch64__)
#include <ucontext.h>
#endif

namespace rt {
class HostFiber {
public:
    using Entry = void (*)(void*);
    HostFiber() = default; // scheduler's original native stack
    HostFiber(const HostFiber&) = delete;
    HostFiber& operator=(const HostFiber&) = delete;
    bool Initialize(Entry entry, void* arg, size_t stackSize = 512*1024);
    bool Resume(HostFiber& caller);
    void Suspend();
    bool Finished() const { return finished_; }
    bool Started() const { return started_; }
private:
    static void Bootstrap();
    void CheckStack() const;
    void SwitchTo(HostFiber& other);
#if defined(__aarch64__)
    // AAPCS64: x19..x30, SP, d8..d15, FPCR, FPSR.
    alignas(16) uint64_t context_[24] = {};
#else
    ucontext_t context_{};
#endif
    std::unique_ptr<uint8_t[]> stack_;
    size_t stackSize_ = 0;
    Entry entry_ = nullptr;
    void* arg_ = nullptr;
    HostFiber* caller_ = nullptr;
    bool started_ = false, finished_ = false;
};
} // namespace rt
