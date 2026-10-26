#include "presentation.h"
#include "presentation_cursor.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <limits>

namespace ui {
namespace {
// Process-wide identities prevent placement-new / allocator address reuse from
// making a new source match an old output history. No pointer is dereferenced.
std::atomic<uint64_t> nextResourceId{1};
using Steady=std::chrono::steady_clock;
uint64_t Elapsed(Steady::time_point start) {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(Steady::now()-start).count());
}
bool Dimensions(int w,int h) {return w>0 && h>0 && w<=8192 && h<=8192;}
bool ValidSource(const Surface& s) {
    if(!s.bits || !Dimensions(s.width,s.height))return false;
    if(s.bpp!=1 && s.bpp!=4 && s.bpp!=8 && s.bpp!=16 && s.bpp!=24 && s.bpp!=32)return false;
    const int64_t pitch=s.pitch;
    return (pitch<0?-pitch:pitch)>=Surface::DibPitch(s.width,s.bpp);
}
uint32_t Rgba(uint32_t rgb) {return 0xFF000000u|((rgb>>16)&255)|(rgb&0xFF00)|((rgb&255)<<16);}
}
uint64_t NewPresentationResourceId() {return nextResourceId.fetch_add(1,std::memory_order_relaxed);}
bool Viewport::Contains(int px,int py) const {
    return Valid() && px>=x && py>=y && int64_t(px)<int64_t(x)+width && int64_t(py)<int64_t(y)+height;
}
bool Viewport::ToSource(int px,int py,int sw,int sh,float& sx,float& sy) const {
    if(!Valid() || !Dimensions(sw,sh))return false;
    sx=std::max(0.0f,std::min(static_cast<float>((int64_t(px)-x)*sw)/width,static_cast<float>(sw-1)));
    sy=std::max(0.0f,std::min(static_cast<float>((int64_t(py)-y)*sh)/height,static_cast<float>(sh-1)));
    return true;
}
Viewport FitViewport(int sw,int sh,int ow,int oh) {
    if(!Dimensions(sw,sh) || !Dimensions(ow,oh))return {};
    int w=ow,h=oh;
    if(int64_t(sw)*oh<=int64_t(ow)*sh)w=std::max(1,int(int64_t(sw)*oh/sh));
    else h=std::max(1,int(int64_t(sh)*ow/sw));
    return {(ow-w)/2,(oh-h)/2,w,h};
}
bool OutputBuffer::Valid() const {
    if(!pixels || !Dimensions(width,height) || stridePixels<static_cast<size_t>(width))return false;
    const size_t rows=static_cast<size_t>(height-1);
    if(rows && stridePixels>(std::numeric_limits<size_t>::max()-size_t(width))/rows)return false;
    return capacityPixels>=rows*stridePixels+static_cast<size_t>(width);
}
SoftwarePresentation::SoftwarePresentation(PresentationAllocator* allocator):columns_(allocator),allocator_(allocator) {}
void SoftwarePresentation::Reset() {
    for(auto& buffer:buffers_) {buffer=BufferState{};buffer.versions=PresentationBuffer<uint64_t>(allocator_);}
    columns_.Clear();sourceWidth_=outputWidth_=0;
    nextBuffer_=0;writtenBytes_=0;
}
size_t SoftwarePresentation::CacheBytes() const {
    size_t bytes=columns_.capacity()*sizeof(int);
    for(const auto& buffer:buffers_)bytes+=buffer.versions.capacity()*sizeof(uint64_t);
    return bytes;
}
bool SoftwarePresentation::Render(const PresentationFrame& frame,const OutputBuffer& out) {
    writtenBytes_=0;
    const Surface& s=frame.surface;const Viewport& v=frame.viewport;
    if(!ValidSource(s) || !out.Valid() || !v.Valid() || v.x<0 || v.y<0 ||
       int64_t(v.x)+v.width>out.width || int64_t(v.y)+v.height>out.height)return false;
    if(sourceWidth_!=s.width || outputWidth_!=v.width) {
        if(!columns_.Resize(static_cast<size_t>(v.width)))return false;
        for(int x=0;x<v.width;++x)columns_[x]=int(int64_t(x)*s.width/v.width);
        sourceWidth_=s.width;outputWidth_=v.width;
    }
    const int cols=(s.width+TileSize-1)/TileSize,rows=(s.height+TileSize-1)/TileSize;
    const bool tracked=out.preserved && frame.owner && frame.serial && frame.tileVersions &&
        frame.tileVersions->size()==size_t(cols)*rows;
    BufferState* state=nullptr;
    if(tracked)for(auto& buffer:buffers_) {
        if(buffer.pixels==out.pixels && buffer.owner==frame.owner &&
           buffer.resourceId==frame.resourceId && buffer.resourceGeneration==frame.resourceGeneration &&
           buffer.width==out.width &&
           buffer.height==out.height && buffer.stride==out.stridePixels && buffer.generation==out.generation &&
           buffer.sourceW==s.width && buffer.sourceH==s.height &&
           buffer.viewport.x==v.x && buffer.viewport.y==v.y && buffer.viewport.width==v.width &&
           buffer.viewport.height==v.height && buffer.versions.size()==size_t(cols)*rows) {state=&buffer;break;}
    }
    bool full=!state;
    Rect cursor;
    if(frame.cursor.visible) {
        const int cx=std::max(0,std::min(frame.cursor.x,s.width-1));
        const int cy=std::max(0,std::min(frame.cursor.y,s.height-1));
        const int px=v.x+int(int64_t(cx)*v.width/s.width),py=v.y+int(int64_t(cy)*v.height/s.height);
        cursor=Rect(px,py,std::min(px+11,out.width),std::min(py+19,out.height));
    }
    std::array<Rect,MaxDamageRects+3> damage{};size_t damageCount=0;
    uint64_t dirtyArea=0;
    if(!full) {
        auto edge=[](int pixel,int output,int source) {return int((int64_t(pixel)*output+source-1)/source);};
        for(int ty=0;ty<rows && !full;++ty)for(int tx=0;tx<cols;) {
            size_t index=size_t(ty)*cols+tx;
            if(state->versions[index]==(*frame.tileVersions)[index]) {++tx;continue;}
            const int first=tx++;
            while(tx<cols && state->versions[size_t(ty)*cols+tx]!=(*frame.tileVersions)[size_t(ty)*cols+tx])++tx;
            Rect r(v.x+edge(first*TileSize,v.width,s.width),v.y+edge(ty*TileSize,v.height,s.height),
                   v.x+edge(std::min(tx*TileSize,s.width),v.width,s.width),
                   v.y+edge(std::min((ty+1)*TileSize,s.height),v.height,s.height));
            if(r.Empty())continue;
            dirtyArea+=uint64_t(r.Width())*r.Height();
            if(damageCount!=0 && damage[damageCount-1].left==r.left && damage[damageCount-1].right==r.right && damage[damageCount-1].bottom==r.top)
                damage[damageCount-1].bottom=r.bottom;
            else damage[damageCount++]=r;
            if(damageCount>MaxDamageRects || dirtyArea>uint64_t(out.width)*out.height/2) {full=true;break;}
        }
    }
    if(full) {damageCount=1;damage[0]=Rect(0,0,out.width,out.height);}
    else if(cursor!=state->cursor) {
        if(!state->cursor.Empty())damage[damageCount++]=state->cursor;
        if(!cursor.Empty())damage[damageCount++]=cursor;
    }
    for(size_t n=0;n<damageCount;++n) {
        const Rect& r=damage[n];
        writtenBytes_+=uint64_t(r.Width())*r.Height()*4;
        for(int y=r.top;y<r.bottom;++y) {
            uint32_t* row=out.pixels+size_t(y)*out.stridePixels;
            if(y<v.y || y>=v.y+v.height) {std::fill(row+r.left,row+r.right,0xFF000000u);continue;}
            const int left=std::max(r.left,std::min(v.x,r.right));
            const int right=std::max(left,std::min(v.x+v.width,r.right));
            std::fill(row+r.left,row+left,0xFF000000u);std::fill(row+right,row+r.right,0xFF000000u);
            const int sy=int(int64_t(y-v.y)*s.height/v.height);
            if(s.bpp==32) {
                const uint8_t* src=s.Row(sy);
                if(v.width==s.width) {
                    for(int x=left;x<right;++x) {uint32_t rgb;std::memcpy(&rgb,src+size_t(x-v.x)*4,4);row[x]=Rgba(rgb);}
                } else for(int x=left;x<right;++x) {uint32_t rgb;std::memcpy(&rgb,src+size_t(columns_[x-v.x])*4,4);row[x]=Rgba(rgb);}
            } else for(int x=left;x<right;++x)row[x]=Rgba(s.Get(columns_[x-v.x],sy));
        }
    }
    // Restore old/new cursor backgrounds per output buffer, then overlay the
    // original arrow. No guest pointer or borrowed output is retained.
    if(damageCount!=0 && !cursor.Empty()) {
        for(int y=0;y<kCursorHeight && cursor.top+y<out.height;++y)
            for(int x=0;x<kCursorWidth && cursor.left+x<out.width;++x) {
                const uint32_t pixel=CursorRgba(x,y);if(!pixel)continue;
                out.pixels[size_t(cursor.top+y)*out.stridePixels+cursor.left+x]=pixel;writtenBytes_+=4;
            }
    }
    if(tracked) {
        if(!state) {state=&buffers_[nextBuffer_];nextBuffer_=(nextBuffer_+1)%MaxBuffers;*state=BufferState{};state->versions=PresentationBuffer<uint64_t>(allocator_);}
        state->pixels=out.pixels;state->owner=frame.owner;state->width=out.width;state->height=out.height;
        state->stride=out.stridePixels;state->generation=out.generation;state->sourceW=s.width;state->sourceH=s.height;
        state->resourceId=frame.resourceId;state->resourceGeneration=frame.resourceGeneration;
        state->viewport=v;state->cursor=cursor;
        if(state->versions.Resize(frame.tileVersions->size()))
            std::memcpy(state->versions.data(),frame.tileVersions->data(),frame.tileVersions->size()*sizeof(uint64_t));
        else *state=BufferState{}; // optional cache: correct full output on next call
    } else {
        // Unpreserved/raw rendering may overwrite an address tracked earlier.
        for(auto& buffer:buffers_)if(buffer.pixels==out.pixels)buffer=BufferState{};
    }
    return true;
}
Presentation::Presentation(PresentationBackend* backend,PresentationAllocator* allocator):allocator_(allocator),pixels_(allocator),resourceId_(NewPresentationResourceId()),tileVersions_(allocator),layers_(allocator) {
    Attach(backend);
}
void Presentation::Attach(PresentationBackend* backend) {
    if(backend_ && backend_!=backend)backend_->Reset();
    backend_=backend;++generation_;lastRenderedSerial_=0;
    if(backend_)backend_->Reset();
}
void Presentation::ClearFrame() {
    layers_.Clear();
    snapshot_=Surface{};pixels_.Clear();
    tileVersions_.Clear();serial_=lastRenderedSerial_=0;
    ++generation_;
    if(backend_)backend_->Reset();
}
void Presentation::Detach() {ClearFrame();backend_=nullptr;}
bool Presentation::Submit(const Surface& s) {layers_.Clear();return SubmitPixels(s);}
bool Presentation::Submit(const Surface& s,const WindowSceneView& scene) {
    if(!SubmitPixels(s))return false;
    const auto start=Steady::now();
    if(layers_.Submit(snapshot_,scene,&tileVersions_))++stats_.layeredSubmissions;else ++stats_.layerFallbacks;
    const auto elapsed=Elapsed(start);stats_.snapshotNs+=elapsed;stats_.layerSnapshotNs+=elapsed;
    return true; // an optional layer allocation failure retains correct flat output
}
bool Presentation::SubmitPixels(const Surface& s) {
    if(!ValidSource(s) || uint64_t(s.width)*s.height*4>MaxFrameBytes) {
        ++stats_.rejected;ClearFrame();return false;
    }
    const auto start=Steady::now();
    bool fresh=snapshot_.width!=s.width || snapshot_.height!=s.height;
    const int tile=kPresentationTileSize,cols=(s.width+tile-1)/tile,rows=(s.height+tile-1)/tile;
    if(fresh) {
        // Prepare both allocations before changing the current renderable frame.
        PresentationBuffer<uint8_t> pixels(allocator_);
        PresentationBuffer<uint64_t> versions(allocator_);
        if(!pixels.Resize(size_t(s.width)*s.height*4) || !versions.Resize(size_t(cols)*rows)) {
            ++stats_.rejected;return false;
        }
        pixels_.Swap(pixels);tileVersions_.Swap(versions);
        std::memset(tileVersions_.data(),0,tileVersions_.size()*sizeof(uint64_t));
        snapshot_=Surface{};snapshot_.width=s.width;snapshot_.height=s.height;
        snapshot_.pitch=s.width*4;snapshot_.bits=pixels_.data();
        ++generation_;
        if(backend_)backend_->Reset();
    }
    if(serial_==std::numeric_limits<uint64_t>::max()) {
        serial_=0;fresh=true;++generation_;if(backend_)backend_->Reset();
    }
    const uint64_t revision=serial_+1;
    bool any=false;
    for(int ty=0;ty<rows;++ty)for(int tx=0;tx<cols;++tx) {
        bool changed=fresh;
        const int x0=tx*tile,x1=std::min(x0+tile,s.width),y1=std::min((ty+1)*tile,s.height);
        for(int y=ty*tile;y<y1;++y) {
            auto* dst=snapshot_.Row(y)+size_t(x0)*4;
            if(s.bpp==32) {
                const auto* src=s.Row(y)+size_t(x0)*4;const size_t bytes=size_t(x1-x0)*4;
                if(fresh || std::memcmp(dst,src,bytes)!=0) {
                    std::memcpy(dst,src,bytes);stats_.snapshotBytes+=bytes;changed=true;
                }
            } else {
                auto* pixels=reinterpret_cast<uint32_t*>(dst);
                for(int x=x0;x<x1;++x) {
                    const auto rgb=s.Get(x,y);
                    if(fresh || pixels[x-x0]!=rgb) {pixels[x-x0]=rgb;stats_.snapshotBytes+=4;changed=true;}
                }
            }
        }
        if(changed) {tileVersions_[size_t(ty)*cols+tx]=revision;any=true;}
    }
    if(any)serial_=revision;else ++stats_.noOpSubmissions;
    ++stats_.submitted;stats_.comparedBytes+=uint64_t(s.width)*s.height*4;stats_.snapshotNs+=Elapsed(start);
    return true;
}
bool Presentation::Render(const OutputBuffer& out,PresentationCursor cursor) {
    if(!backend_ || !snapshot_.Valid() || (backend_->RequiresCpuOutput()?!out.Valid():!Dimensions(out.width,out.height))) {++stats_.rejected;return false;}
    const auto start=Steady::now();
    PresentationFrame frame{snapshot_,FitViewport(snapshot_.width,snapshot_.height,out.width,out.height),cursor,serial_,this,&tileVersions_,resourceId_,generation_};
    frame.scene=layers_.Frame();
    const bool ok=backend_->Render(frame,out);stats_.renderNs+=Elapsed(start);
    if(!ok) {
        // A failed backend may already have changed output bytes or its cache.
        // Retry the retained CPU snapshot with a fresh epoch and full damage.
        ++stats_.rejected;++generation_;backend_->Reset();return false;
    }
    ++stats_.rendered;if(lastRenderedSerial_==serial_)++stats_.redraws;
    lastRenderedSerial_=serial_;auto bytes=backend_->LastWrittenBytes();
    if(bytes==uint64_t(-1))bytes=uint64_t(out.width)*out.height*4;
    stats_.outputBytes+=bytes;if(!bytes)++stats_.noOpRenders;return true;
}
} // namespace ui
