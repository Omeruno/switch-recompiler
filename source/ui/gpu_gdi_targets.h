#pragma once
#include "gpu_gdi_prototype.h"
#include "resident_frame.h"
#include <memory>

namespace ui {
// Bounded multi-target/presentation bridge; ordinary solid GDI admission lives
// in WindowGpuBindings and remains an explicit experiment.
// One current GL 3.3 core context/native owner thread; bounded state handoff
// preserves another renderer. See docs/G4B2_SHARED_CONTEXT.md.
class GpuGdiTargets {
public:
    using Handle=uint64_t;
    static constexpr size_t MaxTargets=4,MaxPixels=512*512,MaxLayers=4;
    static constexpr int MaxOutputWidth=1280,MaxOutputHeight=720;
    struct Layer {Handle target=0;Rect bounds;}; // opaque, nearest-scaled target
    // Owning resource references, not an immutable pixel snapshot. Destruction
    // only drops native pins; GL retirement remains on the manager owner thread.
    class Frame {
    public:
        Frame()=default;
    private:
        friend class GpuGdiTargets;
        std::array<Layer,MaxLayers> layers_{};
        std::array<std::shared_ptr<uint8_t>,MaxLayers> pins_{};
        uint64_t owner_=0;size_t count_=0;int width_=0,height_=0;
    };
    bool PrepareFrame(const Layer*,size_t count,int width,int height,Frame&);
    bool Present(const Frame&);
    // Flush/recover owned resources, then lend views only for this callback.
    // Consumer rejection is not a device error and does not disable GPU.
    bool ConsumeFrame(const Frame&,ResidentFrameConsumer&);
    const Surface* ComposeCpu(const Frame&);
    struct Stats {uint64_t frames=0,drawAttempts=0,failures=0;};
    explicit GpuGdiTargets(PresentationAllocator* allocator=nullptr);
    ~GpuGdiTargets();
    GpuGdiTargets(const GpuGdiTargets&)=delete;
    GpuGdiTargets& operator=(const GpuGdiTargets&)=delete;
    bool Initialize();
    void EnableCapacityReplay(bool enabled);
    void EnableCoveredRetirement(bool enabled); // native same-build reference control
    void Shutdown();
    Handle Create(int width,int height);
    bool Destroy(Handle);
    bool Fill(Handle,Rect,uint32_t rgb);
    bool Copy(Handle,Rect,const uint32_t* rgb,size_t count);
    bool ImportCpu(Handle,const uint32_t* rgb,size_t count);
    const Surface* CpuRead(Handle);
    // Explicit CPU barrier, not a claimed GPU-to-GPU accelerated transfer.
    bool CopyTargetViaCpu(Handle dst,Rect at,Handle src,Rect from);
    // Draw to default framebuffer; no swap or CPU readback. False frames must
    // never be swapped. All handles/rectangles are checked before output writes.
    bool Present(const Layer*,size_t count,int width,int height);
    // Owned fallback frame, borrowed read-only until next compose/reset.
    const Surface* ComposeCpu(const Layer*,size_t count,int width,int height);
    void RecoverCpu(); // explicit owner-thread renderer-failure fallback
    void ContextLost(); // AFTER actual context destruction, no driver calls.
    bool GpuActive() const {return gpuActive_;}
    size_t Pixels() const {return pixels_;}
    const GpuGdiPrototype::Stats* TargetStats(Handle) const;
    const Stats& Counters() const {return stats_;}
private:
    bool coveredRetirement_=false,capacityReplay_=false;
    struct Slot {Handle handle=0;size_t pixels=0;std::unique_ptr<GpuGdiPrototype> image;
        std::shared_ptr<uint8_t> pin;bool retired=false;};
    void CollectRetired();
    bool ValidateFrame(const Frame&) const;
    GpuGdiPrototype* Retained(Handle);
    const GpuGdiPrototype* Retained(Handle) const;
    bool PresentImpl(const Layer*,size_t,int,int);
    const Surface* ComposeCpuImpl(const Layer*,size_t,int,int);
    GpuGdiPrototype* Find(Handle);
    const GpuGdiPrototype* Find(Handle) const;
    bool Validate(const Layer*,size_t,int,int) const;
    bool OpenPresenter();
    void DeletePresenter();
    bool DrawFrame(const Layer*,size_t,int,int);
    void Fallback();
    PresentationAllocator* allocator_;
    std::array<Slot,MaxTargets> slots_;
    uint64_t frameOwner_=0;
    size_t pixels_=0;
    bool initialized_=false,gpuActive_=false;
    uint32_t program_=0,vao_=0;
    PresentationBuffer<uint32_t> output_;
    Surface outputSurface_;
    Stats stats_;
};
} // namespace ui
