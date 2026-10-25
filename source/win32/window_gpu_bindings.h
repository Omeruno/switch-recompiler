#pragma once
#include "../ui/gdi.h"
#include "../ui/gpu_gdi_targets.h"

namespace win32::user {
// Optional native diagnostic bridge; no automatic Win32 GDI dispatch/activation.
// The current USER session and GL context must belong to this native caller.
class WindowGpuBindings {
public:
    struct Lease {ui::WindowRasterTarget raster;ui::GpuGdiTargets::Handle target=0;};
    static constexpr size_t MaxClipRects=128;
    explicit WindowGpuBindings(ui::PresentationAllocator* allocator=nullptr):targets_(allocator) {}
    bool Initialize();
    void Shutdown();
    void ContextLost(){targets_.ContextLost();}
    // No borrowed window/DC pointers retained. Old leases are checked again
    // against real current DC/window identity, layout, clip and target handle.
    bool Acquire(uint32_t hdc,Lease&);
    bool Valid(const Lease&);
    void Prune(); // owner-thread retirement, including after guest/session exit
    // Explicit native experiment only. Does NOT update canonical CPU GDI pixels;
    // ordinary GDI must remain CPU-authoritative until the coherence gate.
    bool Fill(const Lease&,ui::Rect logical,uint32_t rgb);
    const ui::Surface* CpuRead(const Lease&); // explicit barrier, borrowed result
    const ui::GpuGdiPrototype::Stats* Counters(const Lease&);
    bool GpuActive() const {return targets_.GpuActive();}
    size_t Pixels() const {return targets_.Pixels();}
private:
    struct Slot {uint32_t hwnd=0;uint64_t identity=0;int width=0,height=0;ui::GpuGdiTargets::Handle target=0;};
    std::array<Slot,ui::GpuGdiTargets::MaxTargets> slots_{};
    ui::GpuGdiTargets targets_;
};
} // namespace win32::user
