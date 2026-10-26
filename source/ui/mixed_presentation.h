#pragma once
#include "gpu_gdi_targets.h"
#include "opengl_presentation.h"

namespace ui {
// Native owner-thread retained scene. CPU inputs are copied; resident resources
// are pinned, live contents, never historical snapshots. No manager is retained.
class MixedPresentation {
public:
    explicit MixedPresentation(PresentationAllocator* allocator=nullptr);
    ~MixedPresentation();
    MixedPresentation(const MixedPresentation&)=delete;
    MixedPresentation& operator=(const MixedPresentation&)=delete;
    bool Submit(const Surface& base,const PresentationSceneFrame& scene,
        GpuGdiTargets&,const GpuGdiTargets::Frame&,const uint32_t* tokens,size_t count);
    bool Render(GpuGdiTargets&,OpenGlPresentation&,const OutputBuffer&,PresentationCursor={});
    // Explicit read/recovery boundary. With a dead context notify ContextLost
    // first. Never automatically swap a rejected GPU output.
    bool RenderCpu(GpuGdiTargets&,SoftwarePresentation&,const OutputBuffer&,PresentationCursor={});
    void Clear();
    size_t CpuBytes() const;
    uint64_t CopiedBytes() const {return copiedBytes_;}
private:
    struct State;
    std::unique_ptr<State> state_;
    PresentationAllocator* allocator_;
    uint64_t copiedBytes_=0;
};
} // namespace ui
