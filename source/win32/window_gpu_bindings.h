#pragma once
#include "../ui/gdi.h"
#include "../ui/gpu_gdi_targets.h"
#include "../ui/mixed_presentation.h"

namespace win32::user {
// Optional native diagnostic bridge; no automatic Win32 GDI dispatch/activation.
// The current USER session and GL context must belong to this native caller.
class WindowGpuBindings {
public:
    struct Lease {ui::WindowRasterTarget raster;ui::GpuGdiTargets::Handle target=0;};
    static constexpr size_t MaxClipRects=128;
    explicit WindowGpuBindings(ui::PresentationAllocator* allocator=nullptr):targets_(allocator),frameOwners_(allocator),allocator_(allocator) {}
    ~WindowGpuBindings(){Shutdown();}
    bool Initialize();
    struct CoherenceStats {uint64_t imports=0,importPixels=0,commits=0,commitPixels=0,cpuBarriers=0;};
    bool EnableCoherence(); // explicit native test mode, current fresh USER session
    bool DisableCoherence();
    bool CheckpointCpu();
    enum class Fault {None,DirtyImport,WindowCommit,WindowDescription,Detach,ReentrantCpu};
    Fault FaultReason() const {return faultReason_;}
    const char* FaultName() const;
    bool CoherenceHealthy() const {return !fault_;}
    const CoherenceStats& CoherenceCounters() const {return coherenceStats_;}

    // Explicit coherent native presentation. Uses actual whole-window visible
    // regions; canonical CPU base may be stale only beneath resident owners.
    bool SubmitMixed(ui::MixedPresentation&,const ui::WindowSceneView* overlay=nullptr);
    bool PresentationReady() const;
    uint64_t PresentationRevision() const {return gpuRevision_;}
    bool RenderMixed(ui::MixedPresentation&,ui::OpenGlPresentation&,const ui::OutputBuffer&,ui::PresentationCursor={});
    bool RenderMixedCpu(ui::MixedPresentation&,ui::SoftwarePresentation&,const ui::OutputBuffer&,ui::PresentationCursor={});
    void Shutdown();
    void ContextLost(){targets_.ContextLost();}
    // No borrowed window/DC pointers retained. Old leases are checked again
    // against real current DC/window identity, layout, clip and target handle.
    bool Acquire(uint32_t hdc,Lease&);
    bool Valid(const Lease&);
    void Prune(); // owner-thread retirement, including after guest/session exit
    // Explicit native commands, not automatic GDI dispatch. Default diagnostic
    // mode keeps pixels private; opt-in coherence commits at CPU/lifecycle gates.
    bool Fill(const Lease&,ui::Rect logical,uint32_t rgb);
    const ui::Surface* CpuRead(const Lease&); // explicit barrier, borrowed result
    const ui::GpuGdiPrototype::Stats* Counters(const Lease&);
    bool GpuActive() const {return targets_.GpuActive();}
    size_t Pixels() const {return targets_.Pixels();}
private:
    struct Gate;
    bool BeforeCpu();
    bool Fail(Fault);
    void RevokeGate();
    bool SameSession() const;
    struct Slot {
        uint32_t hwnd=0;uint64_t identity=0;int width=0,height=0;
        ui::GpuGdiTargets::Handle target=0;
        bool import=true;
        ui::WindowRasterTarget stamp;
        ui::Region dirty;
        ui::PresentationBuffer<uint32_t> seed;
    };
    Slot* FindSlot(const Lease&);
    bool Seed(Slot&,const Lease&);
    std::shared_ptr<Gate> gate_;
    uint64_t desktopIdentity_=0,gpuRevision_=0;
    bool coherent_=false,fault_=false,barrier_=false;
    Fault faultReason_=Fault::None;
    CoherenceStats coherenceStats_;

    std::array<Slot,ui::GpuGdiTargets::MaxTargets> slots_{};
    ui::GpuGdiTargets targets_;
    ui::PresentationBuffer<uint32_t> frameOwners_;
    ui::PresentationAllocator* allocator_;
};
} // namespace win32::user
