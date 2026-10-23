#pragma once
#include "window_layers.h"

namespace ui {
struct PresentationLayerFrame {
    const Surface* surface=nullptr;
    uint32_t token=0;
    int left=0,top=0;
    uint64_t generation=0,serial=0;
    const PresentationBuffer<uint64_t>* versions=nullptr;
};
struct PresentationSceneFrame {
    const uint32_t* owners=nullptr;
    int width=0,height=0;
    uint64_t generation=0,serial=0;
    const PresentationBuffer<uint64_t>* versions=nullptr;
    const PresentationLayerFrame* layers=nullptr;
    size_t count=0;
};
// Owns all data needed for cursor redraw after the guest windows are destroyed.
// Canonical pixels remain a separate CPU fallback, never used to populate a
// window texture. Untracked/stale ownership reconciles to that fallback plane.
class PresentationLayers {
public:
    explicit PresentationLayers(PresentationAllocator* allocator=nullptr):owners_(allocator),versions_(allocator),sourceVersions_(allocator),sourceOwnerVersions_(allocator),allocator_(allocator) {}
    bool Submit(const Surface& canonical,const WindowSceneView&,const PresentationBuffer<uint64_t>* sourceVersions=nullptr);
    void Clear();
    const PresentationSceneFrame* Frame() const {return frame_.owners?&frame_:nullptr;}
    size_t Bytes() const;
    uint64_t ReconciledPixels() const {return reconciled_;}
    uint64_t ComparedBytes() const {return comparedBytes_;}
    uint64_t CheckedPixels() const {return checkedPixels_;}
    void ResetStats() {reconciled_=comparedBytes_=checkedPixels_=0;}
private:
    struct Slot {
        Surface surface;
        PresentationBuffer<uint32_t> pixels;
        PresentationBuffer<uint64_t> versions;
        PresentationBuffer<uint64_t> sourceVersions;
        uint32_t token=0;
        uint64_t generation=0,serial=0;
    };
    bool CopyLayer(Slot&,const WindowLayerView&,bool tracked);
    std::array<Slot,WindowLayers::MaxLayers> slots_;
    std::array<PresentationLayerFrame,WindowLayers::MaxLayers> views_;
    PresentationBuffer<uint32_t> owners_;
    PresentationBuffer<uint64_t> versions_;
    PresentationBuffer<uint64_t> sourceVersions_;
    PresentationBuffer<uint64_t> sourceOwnerVersions_;
    PresentationAllocator* allocator_;
    PresentationSceneFrame frame_;
    uint64_t generation_=0,serial_=0,reconciled_=0;
    uint64_t sourceOwnerGeneration_=0;
    uint64_t comparedBytes_=0,checkedPixels_=0;
};
} // namespace ui
