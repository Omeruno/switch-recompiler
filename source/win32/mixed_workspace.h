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
    class CpuOutput {
    public:
        virtual ~CpuOutput()=default;
        // Synchronous submission must own all borrowed pixels/scene metadata.
        virtual bool Present(const ui::Surface&,const ui::WindowSceneView&)=0;
        virtual bool Redraw()=0;
        virtual void Reset()=0; // owned storage only; no GL/USER calls
    };
    bool EnableCpuRouting(CpuOutput*); // current healthy owner; nullptr selects reference
    uint64_t CpuFrames() const {return cpuFrames_;}
    uint64_t CpuSubmissions() const {return cpuSubmissions_;}
    bool CpuRoute() const {return cpuRoute_ && hasFrame_;}
    bool Attach();
    void Detach();
    NativePresentResult Refresh();
    NativePresentResult Redraw();
    bool FrameCurrent() const;
    void EnableSnapshotReuse(bool enabled){reuseEnabled_=enabled;} // same-build native reference
    uint64_t ReusedFrames() const {return reused_;}
    uint64_t SnapshotBytes() const {return frame_.CopiedBytes()-copyStart_;}
    bool Faulted() const {return fault_;}
    uint64_t Frames() const {return frames_;}
    uint64_t Submissions() const {return submissions_;}
    uint64_t Redraws() const {return redraws_;}
    MixedWorkspace(const MixedWorkspace&)=delete;
    MixedWorkspace& operator=(const MixedWorkspace&)=delete;
private:
    NativePresentResult Present();
    bool SnapshotCurrent() const;
    WindowGpuBindings& bindings_;
    ShellSession& shell_;
    Output output_;void* context_;
    ui::MixedPresentation frame_;
    Platform previous_;
    CpuOutput* cpuOutput_=nullptr;
    bool cpuRoute_=false;
    uint64_t cpuFrames_=0,cpuSubmissions_=0;
    bool attached_=false,fault_=false,hasFrame_=false,busy_=false,reuseEnabled_=true;
    uint64_t ownershipRevision_=0,reused_=0,copyStart_=0;
    uint64_t frames_=0,submissions_=0,redraws_=0,cpuRevision_=0,gpuRevision_=0,shellRevision_=0,layoutGeneration_=0;
};
} // namespace win32::user
