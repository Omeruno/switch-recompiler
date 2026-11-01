#pragma once
#include "arm64_emitter.h"
namespace cpu {
// One native owner and cooperative guest threads. Storage/memory outlive cache.
// Lazy metadata,immutable publications,always validated source. Does not batch
// scheduler checkpoints itself or retain CPU/TEB/guest mapping pointers.
// Multi-instruction execution requires an explicit budget from the caller.
class NativeCache {
public:
    static constexpr size_t Entries=16;
    static constexpr uint32_t HotVisits=8;
    NativeCache(IGuestMemory&,CodeStore&);
    ~NativeCache();
    NativeCache(const NativeCache&)=delete;
    NativeCache& operator=(const NativeCache&)=delete;
    void Close();
private:
    struct State;
    static bool Dispatch(void*,CpuState&,uint32_t,uint32_t&,uint8_t&,uint8_t&);
    bool Step(CpuState&,uint32_t,uint32_t&,uint8_t&,uint8_t&);
    void Retire(size_t);
    void Disable();
    IGuestMemory& memory_;CodeStore& storage_;
    std::unique_ptr<State> state_;
    bool bound_=false,closed_=false;
};
} // namespace cpu
