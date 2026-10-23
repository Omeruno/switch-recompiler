#include "source/ui/presentation.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>

using namespace ui;
struct FaultAllocator:PresentationAllocator {
    size_t calls=0,failAt=0,live=0;
    void* Allocate(size_t bytes) override {++calls;if(calls==failAt)return nullptr;void* p=std::malloc(bytes);if(p)++live;return p;}
    void Free(void* p) override {if(p){assert(live);--live;std::free(p);}}
};
struct Inspect:PresentationBackend {
    SoftwarePresentation software;
    bool expectScene=true,expectOwned=true;
    uint32_t expected=0x123456;
    bool Render(const PresentationFrame& frame,const OutputBuffer& output) override {
        assert(bool(frame.scene)==expectScene);
        if(frame.scene) {
            auto owner=frame.scene->owners[12*frame.scene->width+12];assert(bool(owner)==expectOwned);
            if(owner) {
                bool found=false;
                for(size_t n=0;n<frame.scene->count;++n)if(frame.scene->layers[n].token==owner) {
                    const auto& layer=frame.scene->layers[n];assert(layer.surface->Get(12-layer.left,12-layer.top)==expected);found=true;
                }
                assert(found);
            }
        }
        return software.Render(frame,output);
    }
};
static void Basic() {
    WindowLayers store;assert(store.Enable(64,48));
    OwnedSurface screen;screen.Allocate(64,48);screen.s.writeCapture=store.ScreenCapture();
    Surface draw=screen.s;draw.writeCapture=store.Capture(100,10,10,20,20);
    draw.Set(12,12,0x123456);auto first=store.Scene().owners[12*64+12];assert(first);
    assert(store.Scene().layers[0].surface->Get(2,2)==0x123456);
    Inspect backend;Presentation p(&backend);std::vector<uint32_t> out(64*48);
    assert(p.Submit(screen.s,store.Scene()) && p.Render({out.data(),64,48,64,out.size()}));
    p.ResetStats();assert(p.Submit(screen.s,store.Scene()));
    assert(!p.LayerComparedBytes() && !p.OwnershipCheckedPixels());
    draw.writeCapture=store.Capture(100,10,10,20,20);draw.Set(12,12,0x123456);
    assert(p.Submit(screen.s,store.Scene()));
    assert(!p.LayerComparedBytes() && !p.OwnershipCheckedPixels()); // redundant raster write is a no-op too
    draw.writeCapture=store.Capture(100,10,10,20,20);draw.Set(13,12,0x777777);
    assert(p.Submit(screen.s,store.Scene()));
    assert(p.LayerComparedBytes()<=4096 && p.OwnershipCheckedPixels()<=1024);
    // A direct CPU pointer write cannot leave a stale window texture visible.
    reinterpret_cast<uint32_t*>(screen.s.Row(12))[12]=0xABCDEF;
    assert(store.Scene().layers[0].surface->Get(2,2)==0x123456); // not copied from the finished screen
    backend.expectOwned=false;
    assert(p.Submit(screen.s,store.Scene()) && p.Render({out.data(),64,48,64,out.size()}));assert(p.ReconciledPixels());
    draw.Set(12,12,0x123456);backend.expectOwned=true;
    assert(p.Submit(screen.s,store.Scene()));
    const auto epoch=store.Scene().ownershipGeneration;
    draw.writeCapture=store.Capture(100,10,10,20,20);
    *draw.writeCapture->ownerClock=~uint64_t(0);draw.Set(14,12,0x888888);
    assert(store.Scene().ownershipGeneration!=epoch && p.Submit(screen.s,store.Scene()));
    store.Forget(100);assert(!store.PixelBytes() && !store.Scene().owners[12*64+12]);
    // Cursor-only replay owns its layer data after the original window is gone.
    assert(p.Render({out.data(),64,48,64,out.size()},{20,20,true}));
    backend.expectOwned=false;assert(p.Submit(screen.s,store.Scene()) && p.Render({out.data(),64,48,64,out.size()}));
    draw.writeCapture=store.Capture(100,10,10,20,20);draw.Set(12,12,0x123456);
    assert(store.Scene().owners[12*64+12]!=first); // HWND/key reuse is not resource reuse
    auto oldGeneration=store.Scene().layers[0].generation;
    draw.writeCapture=store.Capture(100,11,11,21,21);assert(!store.Scene().owners[12*64+12]);
    draw.Set(12,12,0x123456);assert(store.Scene().layers[0].generation!=oldGeneration);
    backend.expectOwned=true;assert(p.Submit(screen.s,store.Scene()));
    auto bad=store.Scene();WindowLayerView duplicates[2]={bad.layers[0],bad.layers[0]};bad.layers=duplicates;bad.count=2;
    backend.expectScene=false;assert(p.Submit(screen.s,bad) && p.Render({out.data(),64,48,64,out.size()}));
    assert(p.Stats().layerFallbacks==1);p.Detach();assert(!p.LayerBytes());
    screen.s.writeCapture=nullptr;store.Clear();assert(!store.PixelBytes() && !store.OwnershipBytes());
}
static void AllocationFailures() {
    WindowLayers store;assert(store.Enable(64,48));OwnedSurface screen;screen.Allocate(64,48);
    screen.s.writeCapture=store.Capture(1,10,10,20,20);screen.s.Set(12,12,0x123456);screen.s.writeCapture=nullptr;
    for(size_t fail=1;fail<=10;++fail) {
        FaultAllocator allocator;allocator.failAt=fail;
        {
            Inspect backend;Presentation p(&backend,&allocator);std::vector<uint32_t> out(64*48);
            const bool submitted=p.Submit(screen.s,store.Scene());
            if(fail<=2)assert(!submitted && !p.SourceWidth());
            else {
                assert(submitted);backend.expectScene=fail>6;
                assert(p.Render({out.data(),64,48,64,out.size()}));
                assert(out[12*64+12]==0xFF563412);
                if(fail>=7) {
                    assert(p.Submit(screen.s,store.Scene()) && p.Render({out.data(),64,48,64,out.size()}));
                    p.ResetStats();assert(p.Submit(screen.s,store.Scene()));
                    assert(!p.LayerComparedBytes() && !p.OwnershipCheckedPixels());
                }
            }
        }
        assert(!allocator.live);
    }
    FaultAllocator allocator;
    {
        WindowLayers failed(&allocator);allocator.failAt=1;assert(!failed.Enable(64,48));assert(!failed.Enabled());
        allocator.failAt=0;assert(failed.Enable(64,48));allocator.failAt=allocator.calls+1;
        const auto* capture=failed.Capture(1,10,10,20,20);assert(capture && !capture->token);
        assert(!failed.PixelBytes());
    }
    assert(!allocator.live);
    for(size_t fail=1;fail<=2;++fail) {
        FaultAllocator faults;
        {WindowLayers failed(&faults);faults.failAt=fail;assert(!failed.Enable(64,48));assert(!failed.Enabled());}
        assert(!faults.live);
        {WindowLayers failed(&faults);faults.failAt=0;assert(failed.Enable(64,48));faults.failAt=faults.calls+fail;
            assert(!failed.Capture(1,0,0,16,16)->token && !failed.PixelBytes());}
        assert(!faults.live);
    }
}
static void Budgets() {
    WindowLayers store;assert(store.Enable(64,48));
    for(unsigned n=1;n<=WindowLayers::MaxLayers*3;++n) {
        auto* capture=store.Capture(n,0,0,8,8);assert(capture && capture->token);capture->Write(0,0,n);
        assert(store.PixelBytes()<=WindowLayers::MaxPixelBytes && store.Scene().count<=WindowLayers::MaxLayers);
    }
    // Budget eviction makes former ownership a screen fallback before freeing it.
    auto* large=store.Capture(10000,0,0,2048,2048);assert(large && large->token);large->Write(0,0,0xAA);
    const auto token=large->token;
    large=store.Capture(10001,0,0,2048,2048);assert(large && large->token!=token);
    assert(!store.Scene().owners[0] && store.PixelBytes()==WindowLayers::MaxPixelBytes);
    assert(store.Capture(10002,0,0,0x7FFFFFFF,1)==store.ScreenCapture());
    store.Clear();assert(!store.PixelBytes() && !store.OwnershipBytes());
}
int main() {
    Basic();AllocationFailures();Budgets();
    std::puts("PASSED: raster-owned window pixels, untracked screen-write reconciliation, destroyed-window replay, key/geometry generations, fallible scene/owner/window allocations, bounded eviction and cleanup");
}
