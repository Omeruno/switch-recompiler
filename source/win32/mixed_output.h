#pragma once
#include "window_gpu_bindings.h"

namespace win32::user {
// Same owner thread as renderer/bindings. QueueSoftware borrows complete RGBA
// pixels only during the call. LeaveGpu must actually destroy the old context.
class MixedOutputDevice {
public:
    virtual ~MixedOutputDevice()=default;
    virtual bool GpuActive() const=0;
    virtual bool SwapGpu()=0;
    virtual void LeaveGpu()=0;
    virtual bool EnsureSoftware()=0;
    virtual bool QueueSoftware(const ui::OutputBuffer&)=0;
};
class MixedOutput {
public:
    struct Stats {uint64_t gpuAttempts=0,swapAttempts=0,gpuFrames=0,fallbacks=0,cpuFrames=0,failures=0;};
    MixedOutput(MixedOutputDevice& d,ui::OpenGlPresentation& g,ui::SoftwarePresentation& s,
        ui::PresentationAllocator* a=nullptr):device_(d),gpu_(g),software_(s),pixels_(a) {}
    bool Present(WindowGpuBindings&,ui::MixedPresentation&,int width,int height,ui::PresentationCursor={});
    void Reset(){stats_={};fault_=false;}
    void Release(){pixels_.Clear();Reset();}
    bool Faulted() const {return fault_;}
    const Stats& Counters() const {return stats_;}
    size_t StagingBytes() const {return pixels_.size()*4;}
private:
    bool Fail(){++stats_.failures;fault_=true;return false;}
    MixedOutputDevice& device_;ui::OpenGlPresentation& gpu_;ui::SoftwarePresentation& software_;
    ui::PresentationBuffer<uint32_t> pixels_;
    Stats stats_;bool fault_=false;
};
} // namespace win32::user
