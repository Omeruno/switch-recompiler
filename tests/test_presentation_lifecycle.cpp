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
struct FaultAllocator:PresentationAllocator {
    int failAfter=-1;size_t live=0,peak=0;
    void* Allocate(size_t bytes) override {
        if(failAfter==0)return nullptr;
        if(failAfter>0)--failAfter;
        auto* p=static_cast<size_t*>(std::malloc(bytes+sizeof(size_t)));
        if(!p)return nullptr;
        *p=bytes;live+=bytes;peak=std::max(peak,live);return p+1;
    }
    void Free(void* pointer) override {
        if(!pointer)return;
        auto* p=static_cast<size_t*>(pointer)-1;live-=*p;std::free(p);
    }
};
static void AllocationFailures() {
    FaultAllocator heap;
    {
        SoftwarePresentation software(&heap);Presentation p(&software,&heap);
        OwnedSurface source;source.Allocate(64,64);
        source.s.Set(0,0,0x123456);
        std::vector<uint32_t> output(64*64);
        OutputBuffer out{output.data(),64,64,64,output.size(),true,1};
        heap.failAfter=0;assert(!p.Submit(source.s) && !p.FrameBytes() && !heap.live);
        heap.failAfter=1;assert(!p.Submit(source.s) && !p.FrameBytes() && !heap.live);
        heap.failAfter=-1;assert(p.Submit(source.s) && p.Render(out));
        const auto expected=output;const auto epoch=p.ContentGeneration();
        source.Allocate(37,45);source.s.Set(0,0,0xABCDEF);
        // Both snapshot allocation failure points preserve the old usable frame.
        for(int n:{0,1}) {
            heap.failAfter=n;assert(!p.Submit(source.s));
            assert(p.ContentGeneration()==epoch && p.SourceWidth()==64);
            assert(p.Render(out) && output==expected);
        }
        heap.failAfter=-1;assert(p.Submit(source.s));
        // Column map is required; retry it without terminating the process.
        heap.failAfter=0;assert(!p.Render(out));
        heap.failAfter=-1;assert(p.Render(out));
        const auto resized=output;
        software.Reset();
        // The optional history cache can fail after a successful conversion.
        heap.failAfter=1;assert(p.Render(out) && output==resized);
        heap.failAfter=-1;assert(p.Render(out) && output==resized);
        for(int i=0;i<100;++i) {
            source.Allocate(17+i%41,19+i%43);
            heap.failAfter=i%3;const bool submitted=p.Submit(source.s);
            heap.failAfter=-1;if(submitted)assert(p.Render(out));
        }
        p.Detach();assert(!heap.live);
    }
    assert(!heap.live && heap.peak<128*1024);
}
int main() {
    AllocationFailures();
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
