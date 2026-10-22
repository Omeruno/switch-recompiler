#pragma once
#include "surface.h"
#include "presentation_memory.h"
#include "presentation_layers.h"
#include <cstddef>
#include <array>
#include "region.h"

namespace ui {

// Host output dimensions; also the native panel size of Switch Lite.
constexpr int kHandheldWidth=1280, kHandheldHeight=720;
constexpr int kPresentationTileSize=32;
struct Viewport {
    int x=0,y=0,width=0,height=0;
    bool Valid() const {return width>0 && height>0;}
    bool Contains(int px,int py) const;
    // Clamps drags outside the image to its edges. Call Contains for a new press.
    bool ToSource(int px,int py,int sourceW,int sourceH,float& sx,float& sy) const;
};
Viewport FitViewport(int sourceW,int sourceH,int outputW,int outputH);
struct PresentationCursor {int x=0,y=0;bool visible=false;};
struct OutputBuffer {
    uint32_t* pixels=nullptr;
    int width=0,height=0;
    size_t stridePixels=0,capacityPixels=0;
    // Opt in only when contents survive between calls. Increment generation
    // after external writes/reallocation, even if the address stays the same.
    bool preserved=false;
    uint64_t generation=0;
    bool Valid() const;
};
struct PresentationFrame {
    const Surface& surface;
    Viewport viewport;
    PresentationCursor cursor;
    uint64_t serial=0;
    const void* owner=nullptr;
    // Row-major source tile revisions, at kPresentationTileSize. A null map
    // requests conservative full output; views are valid only during Render.
    const PresentationBuffer<uint64_t>* tileVersions=nullptr;
    // Stable across address reuse; revision scope changes on resize/reset/rebind.
    uint64_t resourceId=0,resourceGeneration=0;
    const PresentationSceneFrame* scene=nullptr;
};
// Synchronous: implementations may borrow these views only until Render returns.
// No guest callbacks, framebuffer acquisition, or guest/Win32 state here.
// Output is RGBA8888 as little-endian 0xAABBGGRR; padding is never written.
class PresentationBackend {
public:
    virtual ~PresentationBackend()=default;
    virtual bool Render(const PresentationFrame&,const OutputBuffer&)=0;
    virtual void Reset() {}
    virtual bool RequiresCpuOutput() const {return true;}
    // Unknown work is conservatively counted as a full output frame.
    virtual uint64_t LastWrittenBytes() const {return uint64_t(-1);}
};
class SoftwarePresentation final:public PresentationBackend {
public:
    explicit SoftwarePresentation(PresentationAllocator* allocator=nullptr);
    bool Render(const PresentationFrame&,const OutputBuffer&) override;
    void Reset() override;
    uint64_t LastWrittenBytes() const override {return writtenBytes_;}
    size_t CacheBytes() const;
    static constexpr int TileSize=kPresentationTileSize;
    static constexpr size_t MaxBuffers=3,MaxDamageRects=128;
private:
    struct BufferState {
        const uint32_t* pixels=nullptr;
        const void* owner=nullptr;
        int width=0,height=0,sourceW=0,sourceH=0;
        size_t stride=0;
        uint64_t generation=0,resourceId=0,resourceGeneration=0;
        Viewport viewport;
        Rect cursor;
        PresentationBuffer<uint64_t> versions;
    };
    std::array<BufferState,MaxBuffers> buffers_;
    size_t nextBuffer_=0;
    uint64_t writtenBytes_=0;
    PresentationBuffer<int> columns_;
    PresentationAllocator* allocator_;
    int sourceWidth_=0,outputWidth_=0;
};
struct PresentationStats {
    uint64_t submitted=0,rendered=0,rejected=0,redraws=0;
    uint64_t snapshotBytes=0,outputBytes=0,snapshotNs=0,renderNs=0;
    uint64_t comparedBytes=0,noOpSubmissions=0,noOpRenders=0;
    uint64_t layeredSubmissions=0,layerFallbacks=0,layerSnapshotNs=0;
};
// Owns one latest CPU frame: cursor-only redraw never keeps a guest/GUI pointer.
// Backend is borrowed and must outlive calls. Detach releases frame memory.
class Presentation {
public:
    explicit Presentation(PresentationBackend* backend=nullptr,PresentationAllocator* allocator=nullptr);
    Presentation(const Presentation&)=delete;
    Presentation& operator=(const Presentation&)=delete;
    void Attach(PresentationBackend* backend);
    void Detach();
    void ClearFrame();
    void ResetStats() {stats_={};layers_.ResetStats();}
    bool Submit(const Surface&);
    bool Submit(const Surface&,const WindowSceneView&);
    void ClearLayers() {layers_.Clear();}
    size_t LayerBytes() const {return layers_.Bytes();}
    size_t LayerCount() const {return layers_.Frame()?layers_.Frame()->count:0;}
    uint64_t ReconciledPixels() const {return layers_.ReconciledPixels();}
    bool Render(const OutputBuffer&,PresentationCursor cursor={});
    int SourceWidth() const {return snapshot_.width;}
    int SourceHeight() const {return snapshot_.height;}
    size_t FrameBytes() const {return pixels_.size();}
    size_t TrackingBytes() const {return tileVersions_.capacity()*sizeof(uint64_t);}
    uint64_t ContentSerial() const {return serial_;}
    uint64_t ContentGeneration() const {return generation_;}
    const PresentationStats& Stats() const {return stats_;}
    static constexpr size_t MaxFrameBytes=16*1024*1024;
private:
    bool SubmitPixels(const Surface&);
    PresentationBackend* backend_=nullptr;
    PresentationAllocator* allocator_;
    Surface snapshot_;
    PresentationBuffer<uint8_t> pixels_;
    PresentationStats stats_;
    uint64_t serial_=0,lastRenderedSerial_=0;
    const uint64_t resourceId_;
    uint64_t generation_=0;
    PresentationBuffer<uint64_t> tileVersions_;
    PresentationLayers layers_;
};
} // namespace ui
