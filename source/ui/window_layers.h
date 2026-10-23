#pragma once
#include "surface.h"
#include "presentation_memory.h"
#include <array>

namespace ui {
struct WindowLayerView {
    const Surface* surface=nullptr;
    uint32_t token=0;
    int left=0,top=0;
    uint64_t generation=0;
    const PresentationBuffer<uint64_t>* versions=nullptr;
};
struct WindowSceneView {
    const uint32_t* owners=nullptr;
    int width=0,height=0;
    const WindowLayerView* layers=nullptr;
    size_t count=0;
    bool writeThrough=false; // producer records every window write and screen ownership change
    const PresentationBuffer<uint64_t>* ownershipVersions=nullptr;
    uint64_t ownershipGeneration=0;
};
// Bounded window-owned raster storage. The CPU screen stays authoritative for
// Win32 readback/ROP; ownership selects actual window pixels for composition.
class WindowLayers {
public:
    static constexpr size_t MaxLayers=48,MaxPixelBytes=16*1024*1024;
    WindowLayers():WindowLayers(nullptr) {}
    explicit WindowLayers(PresentationAllocator* allocator):owners_(allocator),ownerVersions_(allocator),allocator_(allocator) {}
    bool Enable(int width,int height);
    void Clear();
    bool Enabled() const {return owners_.size()!=0;}
    const SurfaceWriteCapture* ScreenCapture() const {return Enabled()?&screenCapture_:nullptr;}
    const SurfaceWriteCapture* Capture(uint32_t key,int left,int top,int width,int height);
    void Forget(uint32_t key);
    WindowSceneView Scene();
    size_t PixelBytes() const {return pixelBytes_;}
    size_t OwnershipBytes() const {return owners_.size()*sizeof(uint32_t);}
private:
    struct Slot {
        uint32_t key=0,token=0;
        uint64_t generation=0,touched=0,writeSerial=0;
        int left=0,top=0;
        Surface surface;
        PresentationBuffer<uint32_t> pixels;
        PresentationBuffer<uint64_t> versions;
        SurfaceWriteCapture capture;
    };
    void Drop(Slot&);
    std::array<Slot,MaxLayers> slots_;
    std::array<WindowLayerView,MaxLayers> views_;
    PresentationBuffer<uint32_t> owners_;
    PresentationBuffer<uint64_t> ownerVersions_;
    PresentationAllocator* allocator_;
    SurfaceWriteCapture screenCapture_;
    size_t pixelBytes_=0;
    uint64_t clock_=0,generation_=0;
    uint64_t ownerClock_=0,ownerEpoch_=0;
    uint32_t nextToken_=1;
};
} // namespace ui
