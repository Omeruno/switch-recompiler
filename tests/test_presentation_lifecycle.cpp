#include "source/ui/presentation.h"
#include <cassert>
#include <cstdio>
#include <new>
#include <vector>
using namespace ui;

struct Backend:PresentationBackend {
    SoftwarePresentation software;
    bool failAfterWrite=false;
    unsigned resets=0;
    uint64_t id=0,generation=0;
    bool Render(const PresentationFrame& frame,const OutputBuffer& out) override {
        id=frame.resourceId;generation=frame.resourceGeneration;
        if(!software.Render(frame,out))return false;
        if(failAfterWrite) {
            failAfterWrite=false;
            // A failed upload/convert may leave both pixels and tracking changed.
            out.pixels[0]=0;return false;
        }
        return true;
    }
    void Reset() override {++resets;software.Reset();}
    uint64_t LastWrittenBytes() const override {return software.LastWrittenBytes();}
};
int main() {
    Backend backend,other;
    OwnedSurface source;source.Allocate(64,64);
    for(int y=0;y<source.s.height;++y)for(int x=0;x<source.s.width;++x)source.s.Set(x,y,0x123456);
    std::vector<uint32_t> pixels(64*64);
    OutputBuffer out{pixels.data(),64,64,64,pixels.size(),true,1};
    alignas(Presentation) unsigned char storage[sizeof(Presentation)];
    auto* p=new(storage) Presentation(&backend);
    assert(p->Submit(source.s) && p->Render(out));
    const auto first=pixels;
    const uint64_t firstId=backend.id;
    assert(firstId);
    // A backend failure must invalidate successful per-buffer dirty histories.
    backend.failAfterWrite=true;
    auto epoch=p->ContentGeneration();
    assert(!p->Render(out) && pixels!=first);
    assert(p->Render(out) && pixels==first);
    assert(p->ContentGeneration()!=epoch);
    assert(backend.LastWrittenBytes()==pixels.size()*4);
    // Rebind retains the owned snapshot but clears both backend caches.
    p->Attach(&other);
    assert(!backend.software.CacheBytes());
    assert(p->Render(out) && other.id==firstId && pixels==first);
    p->Detach();
    assert(!p->FrameBytes() && !p->TrackingBytes() && !other.software.CacheBytes());
    assert(!p->Render(out));
    p->~Presentation();
    // The exact same Presentation address represents a distinct resource now.
    p=new(storage) Presentation(&backend);
    assert(p->Submit(source.s) && p->Render(out));
    assert(backend.id && backend.id!=firstId);
    // Repeated resize/reconnect/destroy releases all bounded tracking memory.
    for(int i=0;i<150;++i) {
        source.Allocate(17+(i%47),23+(i%41));for(int y=0;y<source.s.height;++y)for(int x=0;x<source.s.width;++x)source.s.Set(x,y,0x123456+i);
        assert(p->Submit(source.s) && p->Render(out,{i%17,i%23,true}));
        assert(p->FrameBytes()<=64*64*4);
        assert(p->TrackingBytes()<=4*sizeof(uint64_t));
        p->ClearFrame();
        assert(!p->FrameBytes() && !p->TrackingBytes() && !backend.software.CacheBytes());
    }
    p->Detach();p->~Presentation();
    puts("PASSED: partial-output failure/retry, resource identities/address reuse, backend rebind, repeated resize and cleanup");
}
