#include "mixed_output.h"
namespace win32::user {
bool MixedOutput::Present(WindowGpuBindings& bindings,ui::MixedPresentation& frame,int w,int h,ui::PresentationCursor cursor) {
    if(fault_)return false;
    if(w<=0 || h<=0 || w>ui::GpuGdiTargets::MaxOutputWidth || h>ui::GpuGdiTargets::MaxOutputHeight)return Fail();
    const bool hadGpu=device_.GpuActive();
    if(hadGpu) {
        ++stats_.gpuAttempts;
        if(bindings.RenderMixed(frame,gpu_,{nullptr,w,h,0,0},cursor)) {
            ++stats_.swapAttempts;
            if(device_.SwapGpu()){++stats_.gpuFrames;return true;}
        }
    }
    // Recover and render COMPLETE genuine pixels before closing the context or
    // acquiring a scanout buffer. No partial failed frame is queued as fallback.
    if(!pixels_.Resize(size_t(w)*h))return Fail();
    ui::OutputBuffer cpu{pixels_.data(),w,h,size_t(w),pixels_.size()};
    if(!bindings.RenderMixedCpu(frame,software_,cpu,cursor))return Fail();
    if(hadGpu) {
        device_.LeaveGpu();bindings.ContextLost();++stats_.fallbacks;
    }
    if(!device_.EnsureSoftware() || !device_.QueueSoftware(cpu))return Fail();
    ++stats_.cpuFrames;return true;
}
} // namespace win32::user
