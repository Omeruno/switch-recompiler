#pragma once
#include "gpu_gdi_prototype.h"
#include <memory>

namespace ui {
// G4a isolated multi-target/presentation bridge, not a production GDI backend.
// One dedicated current GL context and native owner thread, as in G3.
class GpuGdiTargets {
public:
    using Handle=uint64_t;
    static constexpr size_t MaxTargets=4,MaxPixels=512*512,MaxLayers=4;
    static constexpr int MaxOutputWidth=1280,MaxOutputHeight=720;
    struct Layer {Handle target=0;Rect bounds;}; // opaque, nearest-scaled target
    struct Stats {uint64_t frames=0,drawAttempts=0,failures=0;};
    explicit GpuGdiTargets(PresentationAllocator* allocator=nullptr);
    ~GpuGdiTargets();
    GpuGdiTargets(const GpuGdiTargets&)=delete;
    GpuGdiTargets& operator=(const GpuGdiTargets&)=delete;
    bool Initialize();
    void Shutdown();
    Handle Create(int width,int height);
    bool Destroy(Handle);
    bool Fill(Handle,Rect,uint32_t rgb);
    bool Copy(Handle,Rect,const uint32_t* rgb,size_t count);
    const Surface* CpuRead(Handle);
    // Explicit CPU barrier, not a claimed GPU-to-GPU accelerated transfer.
    bool CopyTargetViaCpu(Handle dst,Rect at,Handle src,Rect from);
    // Draw to default framebuffer; no swap or CPU readback. False frames must
    // never be swapped. All handles/rectangles are checked before output writes.
    bool Present(const Layer*,size_t count,int width,int height);
    // Owned fallback frame, borrowed read-only until next compose/reset.
    const Surface* ComposeCpu(const Layer*,size_t count,int width,int height);
    void ContextLost(); // AFTER actual context destruction, no driver calls.
    bool GpuActive() const {return gpuActive_;}
    size_t Pixels() const {return pixels_;}
    const GpuGdiPrototype::Stats* TargetStats(Handle) const;
    const Stats& Counters() const {return stats_;}
private:
    struct Slot {Handle handle=0;size_t pixels=0;std::unique_ptr<GpuGdiPrototype> image;};
    GpuGdiPrototype* Find(Handle);
    const GpuGdiPrototype* Find(Handle) const;
    bool Validate(const Layer*,size_t,int,int) const;
    bool OpenPresenter();
    void DeletePresenter();
    bool DrawFrame(const Layer*,size_t,int,int);
    void Fallback();
    PresentationAllocator* allocator_;
    std::array<Slot,MaxTargets> slots_;
    size_t pixels_=0;
    bool initialized_=false,gpuActive_=false;
    uint32_t program_=0,vao_=0;
    PresentationBuffer<uint32_t> output_;
    Surface outputSurface_;
    Stats stats_;
};
} // namespace ui
