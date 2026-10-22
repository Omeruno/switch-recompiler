#pragma once
#include "surface.h"
#include <cstddef>

namespace ui {

// Host output dimensions; also the native panel size of Switch Lite.
constexpr int kHandheldWidth=1280, kHandheldHeight=720;
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
    bool Valid() const;
};
struct PresentationFrame {
    const Surface& surface;
    Viewport viewport;
    PresentationCursor cursor;
    uint64_t serial=0;
};
// Synchronous: implementations may borrow these views only until Render returns.
// No guest callbacks, framebuffer acquisition, or guest/Win32 state here.
// Output is RGBA8888 as little-endian 0xAABBGGRR; padding is never written.
class PresentationBackend {
public:
    virtual ~PresentationBackend()=default;
    virtual bool Render(const PresentationFrame&,const OutputBuffer&)=0;
};
class SoftwarePresentation final:public PresentationBackend {
public:
    bool Render(const PresentationFrame&,const OutputBuffer&) override;
private:
    std::vector<int> columns_;
    int sourceWidth_=0,outputWidth_=0;
};
struct PresentationStats {
    uint64_t submitted=0,rendered=0,rejected=0,redraws=0;
    uint64_t snapshotBytes=0,outputBytes=0,snapshotNs=0,renderNs=0;
};
// Owns one latest CPU frame: cursor-only redraw never keeps a guest/GUI pointer.
// Backend is borrowed and must outlive calls. Detach releases frame memory.
class Presentation {
public:
    explicit Presentation(PresentationBackend* backend=nullptr):backend_(backend){}
    Presentation(const Presentation&)=delete;
    Presentation& operator=(const Presentation&)=delete;
    void Attach(PresentationBackend* backend) {backend_=backend;}
    void Detach();
    void ClearFrame();
    void ResetStats() {stats_={};}
    bool Submit(const Surface&);
    bool Render(const OutputBuffer&,PresentationCursor cursor={});
    int SourceWidth() const {return snapshot_.s.width;}
    int SourceHeight() const {return snapshot_.s.height;}
    size_t FrameBytes() const {return snapshot_.storage.size();}
    const PresentationStats& Stats() const {return stats_;}
    static constexpr size_t MaxFrameBytes=16*1024*1024;
private:
    PresentationBackend* backend_=nullptr;
    OwnedSurface snapshot_;
    PresentationStats stats_;
    uint64_t serial_=0,lastRenderedSerial_=0;
};
} // namespace ui
