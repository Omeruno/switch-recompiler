#pragma once
#include "window_gpu_bindings.h"
#include "shell_session.h"

namespace win32::user {
// Native owner-thread output bridge. Declare after ShellSession, before executor;
// detach after draining and before devices/bindings/session destruction.
class MixedWorkspace {
public:
    // Output owns rendering/swap policy. Must return false for an unswapped
    // failure; recovery can explicitly render real CPU pixels before success.
    using Output=bool (*)(void*,WindowGpuBindings&,ui::MixedPresentation&);
    MixedWorkspace(WindowGpuBindings&,ShellSession&,Output,void*,ui::PresentationAllocator* =nullptr);
    ~MixedWorkspace();
    bool Attach();
    void Detach();
    NativePresentResult Refresh();
    NativePresentResult Redraw();
    bool FrameCurrent() const;
    bool Faulted() const {return fault_;}
    uint64_t Frames() const {return frames_;}
    uint64_t Submissions() const {return submissions_;}
    uint64_t Redraws() const {return redraws_;}
    MixedWorkspace(const MixedWorkspace&)=delete;
    MixedWorkspace& operator=(const MixedWorkspace&)=delete;
private:
    NativePresentResult Present();
    WindowGpuBindings& bindings_;
    ShellSession& shell_;
    Output output_;void* context_;
    ui::MixedPresentation frame_;
    Platform previous_;
    bool attached_=false,fault_=false,hasFrame_=false,busy_=false;
    uint64_t frames_=0,submissions_=0,redraws_=0,cpuRevision_=0,gpuRevision_=0,shellRevision_=0,layoutGeneration_=0;
};
} // namespace win32::user
