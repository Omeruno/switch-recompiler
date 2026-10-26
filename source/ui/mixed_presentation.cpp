#include "mixed_presentation.h"
#include <cstring>
#include <algorithm>
#include <new>

namespace ui {
namespace {
bool Versions(PresentationBuffer<uint64_t>& out,int w,int h) {
    if(!out.Resize(size_t((w+31)/32)*((h+31)/32)))return false;
    std::fill(out.data(),out.data()+out.size(),1);return true;
}
bool Valid(const Surface& s) {
    return s.bits && s.bpp==32 && s.width>0 && s.height>0 && s.width<=8192 && s.height<=8192 &&
        s.pitch>=int64_t(s.width)*4 && s.pitch%4==0 && uint64_t(s.width)*s.height*4<=Presentation::MaxFrameBytes;
}
struct Probe:ResidentFrameConsumer {
    std::array<std::array<int,2>,GpuGdiTargets::MaxLayers> sizes{};
    size_t count=0;
    bool Consume(const ResidentFrameView& view) override {
        count=view.count;
        for(size_t i=0;i<count;++i)sizes[i]={view.images[i].width,view.images[i].height};
        return true;
    }
};
}
struct MixedPresentation::State {
    explicit State(PresentationAllocator* a):basePixels(a),owners(a),flatPixels(a),versions(a) {}
    struct Layer {
        Surface surface;
        PresentationBuffer<uint32_t> pixels;
        PresentationBuffer<uint64_t> versions;
        int resident=-1,width=0,height=0;
    };
    Surface base,flat;
    PresentationBuffer<uint32_t> basePixels,owners,flatPixels;
    PresentationBuffer<uint64_t> versions;
    uint64_t id=NewPresentationResourceId();
    std::array<Layer,WindowLayers::MaxLayers> storage;
    std::array<PresentationLayerFrame,WindowLayers::MaxLayers> layers{};
    std::array<uint32_t,GpuGdiTargets::MaxLayers> tokens{};
    GpuGdiTargets::Frame resident;
    PresentationSceneFrame scene;
    size_t count=0,bytes=0;
    // Owned immutable CPU snapshots carry stable revision maps for cursor redraw.
};
MixedPresentation::MixedPresentation(PresentationAllocator* a):allocator_(a) {}
MixedPresentation::~MixedPresentation()=default;
void MixedPresentation::Clear() {state_.reset();}
size_t MixedPresentation::CpuBytes() const {return state_?state_->bytes+state_->flatPixels.size()*4:0;}
bool MixedPresentation::Submit(const Surface& base,const PresentationSceneFrame& scene,
    GpuGdiTargets& manager,const GpuGdiTargets::Frame& resident,const uint32_t* tokens,size_t count) {
    if(!Valid(base) || base.width>GpuGdiTargets::MaxOutputWidth || base.height>GpuGdiTargets::MaxOutputHeight ||
       scene.width!=base.width || scene.height!=base.height || !scene.owners ||
       scene.count>WindowLayers::MaxLayers || (scene.count && !scene.layers) ||
       count>GpuGdiTargets::MaxLayers || (count && !tokens))return false;
    for(size_t i=0;i<count;++i) {
        if(!tokens[i])return false;
        for(size_t j=0;j<i;++j)if(tokens[i]==tokens[j])return false;
    }
    Probe probe;
    if(!manager.ConsumeFrame(resident,probe) || probe.count!=count)return false;
    std::unique_ptr<State> next(new(std::nothrow) State(allocator_));if(!next)return false;
    const size_t pixels=size_t(base.width)*base.height;
    if(!next->basePixels.Resize(pixels) || !next->owners.Resize(pixels) || !Versions(next->versions,base.width,base.height))return false;
    next->base={};next->base.width=base.width;next->base.height=base.height;next->base.pitch=base.width*4;
    next->base.bits=reinterpret_cast<uint8_t*>(next->basePixels.data());
    for(int y=0;y<base.height;++y)std::memcpy(next->base.Row(y),base.Row(y),size_t(base.width)*4);
    std::memcpy(next->owners.data(),scene.owners,pixels*4);
    next->bytes=pixels*8+next->versions.size()*8;size_t layerBytes=0,matched=0;
    for(size_t n=0;n<scene.count;++n) {
        const auto& src=scene.layers[n];auto& dst=next->layers[n];auto& store=next->storage[n];
        if(!src.token || src.left < -16384 || src.left>16384 || src.top < -16384 || src.top>16384)return false;
        for(size_t j=0;j<n;++j)if(src.token==scene.layers[j].token)return false;
        dst={nullptr,src.token,src.left,src.top,next->id,1,nullptr};
        for(size_t i=0;i<count;++i)if(src.token==tokens[i]) {
            store.resident=int(i);store.width=probe.sizes[i][0];store.height=probe.sizes[i][1];++matched;
        }
        if(store.resident<0) {
            if(!src.surface || !Valid(*src.surface))return false;
            store.width=src.surface->width;store.height=src.surface->height;
            const size_t bytes=size_t(store.width)*store.height*4;
            if(bytes>WindowLayers::MaxPixelBytes-layerBytes)return false;
            layerBytes+=bytes;
            store.pixels=PresentationBuffer<uint32_t>(allocator_);
            store.versions=PresentationBuffer<uint64_t>(allocator_);
            if(!store.pixels.Resize(bytes/4) || !Versions(store.versions,store.width,store.height))return false;
            next->bytes+=store.versions.size()*8;
            store.surface={};store.surface.width=store.width;store.surface.height=store.height;
            store.surface.pitch=store.width*4;store.surface.bits=reinterpret_cast<uint8_t*>(store.pixels.data());
            for(int y=0;y<store.height;++y)std::memcpy(store.surface.Row(y),src.surface->Row(y),size_t(store.width)*4);
            dst.surface=&store.surface;dst.versions=&store.versions;
        }
    }
    if(matched!=count)return false;
    // No canonical comparison: ownership is authoritative. Reject unknown or
    // out-of-bounds owners rather than sampling stale base under a GPU layer.
    for(int y=0;y<base.height;++y)for(int x=0;x<base.width;++x) {
        const auto token=next->owners[size_t(y)*base.width+x];if(!token)continue;
        size_t n=0;while(n<scene.count && next->layers[n].token!=token)++n;
        if(n==scene.count)return false;
        const auto& layer=next->layers[n];const auto& store=next->storage[n];
        const int64_t lx=int64_t(x)-layer.left,ly=int64_t(y)-layer.top;
        if(lx<0 || ly<0 || lx>=store.width || ly>=store.height)return false;
    }
    for(size_t i=0;i<count;++i)next->tokens[i]=tokens[i];
    next->count=count;next->resident=resident;next->bytes+=layerBytes;
    next->scene={next->owners.data(),base.width,base.height,next->id,1,&next->versions,next->layers.data(),scene.count};
    copiedBytes_+=next->bytes;state_.swap(next);return true;
}
bool MixedPresentation::Render(GpuGdiTargets& manager,OpenGlPresentation& renderer,const OutputBuffer& out,PresentationCursor cursor) {
    if(!state_)return false;
    struct Sink:ResidentFrameConsumer {
        State& s;OpenGlPresentation& renderer;const OutputBuffer& out;PresentationCursor cursor;
        Sink(State& s,OpenGlPresentation& r,const OutputBuffer& o,PresentationCursor c):s(s),renderer(r),out(o),cursor(c) {}
        bool Consume(const ResidentFrameView& view) override {
            // Recovered CPU views require the explicit software boundary below.
            for(size_t i=0;i<view.count;++i)if(!view.images[i].texture)return false;
            PresentationFrame frame{s.base,FitViewport(s.base.width,s.base.height,out.width,out.height),cursor,1,&s,&s.versions,s.id,1};
            frame.scene=&s.scene;
            return renderer.RenderResident(frame,out,view,s.tokens.data(),s.count);
        }
    } sink(*state_,renderer,out,cursor);
    return manager.ConsumeFrame(state_->resident,sink);
}
bool MixedPresentation::RenderCpu(GpuGdiTargets& manager,SoftwarePresentation& renderer,const OutputBuffer& out,PresentationCursor cursor) {
    if(!state_ || !out.Valid())return false;
    auto& s=*state_;
    if(!s.flatPixels.Resize(s.basePixels.size()))return false;
    // Do not recover an unrelated manager for a stale/foreign frame.
    Probe probe;if(!manager.ConsumeFrame(s.resident,probe))return false;
    manager.RecoverCpu();
    struct Sink:ResidentFrameConsumer {
        State& s;explicit Sink(State& s):s(s) {}
        bool Consume(const ResidentFrameView& view) override {
            for(int y=0;y<s.base.height;++y)for(int x=0;x<s.base.width;++x) {
                const size_t p=size_t(y)*s.base.width+x;uint32_t rgb=s.base.Get(x,y);
                const uint32_t token=s.owners[p];
                if(token)for(size_t n=0;n<s.scene.count;++n)if(s.layers[n].token==token) {
                    const auto& layer=s.layers[n];const auto& store=s.storage[n];
                    const Surface* image=store.resident<0?&store.surface:view.images[size_t(store.resident)].cpu;
                    if(!image || !image->Valid())return false;
                    rgb=image->Get(x-layer.left,y-layer.top);break;
                }
                s.flatPixels[p]=rgb;
            }
            return true;
        }
    } sink(s);
    if(!manager.ConsumeFrame(s.resident,sink))return false;
    s.flat=s.base;s.flat.bits=reinterpret_cast<uint8_t*>(s.flatPixels.data());
    PresentationFrame frame{s.flat,FitViewport(s.flat.width,s.flat.height,out.width,out.height),cursor};
    return renderer.Render(frame,out);
}
} // namespace ui
