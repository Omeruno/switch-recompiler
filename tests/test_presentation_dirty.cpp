#include "source/ui/presentation.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace ui;
static uint32_t randomState=0x918347;
static uint32_t Next() {randomState^=randomState<<13;randomState^=randomState>>17;return randomState^=randomState<<5;}
static void Pattern(Surface& s) {
    for(int y=0;y<s.height;++y)for(int x=0;x<s.width;++x)s.Set(x,y,(x*17113u+y*97231u)&0xFFFFFF);
}
struct Target {
    int width,height;size_t stride;
    std::vector<uint32_t> pixels;
    uint64_t generation=0;
    Target(int w,int h):width(w),height(h),stride(w+3),pixels(stride*h+4,0xDEADBEEF){}
    OutputBuffer View() {return {pixels.data()+2,width,height,stride,stride*height,true,generation};}
    void Compare(const Surface& source,PresentationCursor cursor) {
        SoftwarePresentation reference;
        std::vector<uint32_t> expected(size_t(width)*height);
        assert(reference.Render({source,FitViewport(source.width,source.height,width,height),cursor,1},
            {expected.data(),width,height,size_t(width),expected.size()}));
        for(int y=0;y<height;++y) {
            assert(std::memcmp(pixels.data()+2+size_t(y)*stride,expected.data()+size_t(y)*width,width*4)==0);
            for(size_t x=width;x<stride;++x)assert(pixels[2+size_t(y)*stride+x]==0xDEADBEEF);
        }
        assert(pixels.front()==0xDEADBEEF && pixels[1]==0xDEADBEEF && pixels.back()==0xDEADBEEF);
    }
};
static void Alternating(int sw,int sh,int ow,int oh) {
    OwnedSurface source;source.Allocate(sw,sh);Pattern(source.s);
    SoftwarePresentation software;Presentation p(&software);
    Target a(ow,oh),b(ow,oh),c(ow,oh),d(ow,oh);
    Target* targets[]={&a,&b,&c,&d}; // includes bounded-history eviction
    assert(p.Submit(source.s));
    for(auto* t:targets) {assert(p.Render(t->View()));t->Compare(source.s,{});}
    for(unsigned n=0;n<80;++n) {
        int x=Next()%sw,y=Next()%sh;
        const int right=std::min(sw,x+1+int(Next()%23)),bottom=std::min(sh,y+1+int(Next()%19));
        for(int py=y;py<bottom;++py)for(int px=x;px<right;++px)source.s.Set(px,py,Next()&0xFFFFFF);
        // Raw guest-style memory writes also have to enter the owned snapshot.
        uint32_t raw=Next();std::memcpy(source.s.Row(y)+x*4,&raw,4);
        assert(p.Submit(source.s));
        PresentationCursor cursor{int(Next()%(sw+20))-10,int(Next()%(sh+20))-10,n%7!=0};
        auto& t=*targets[n%3];assert(p.Render(t.View(),cursor));t.Compare(source.s,cursor);
        if(n%9==0) {
            const auto bytes=p.Stats().outputBytes,copyBytes=p.Stats().snapshotBytes,serial=p.ContentSerial();
            assert(p.Submit(source.s) && p.ContentSerial()==serial && p.Stats().snapshotBytes==copyBytes);
            assert(p.Render(t.View(),cursor) && p.Stats().outputBytes==bytes);
        }
        if(n==40) {
            auto invalid=t.View();invalid.capacityPixels=1;
            auto old=t.pixels;assert(!p.Render(invalid,cursor) && t.pixels==old);
            assert(p.Render(t.View(),cursor));t.Compare(source.s,cursor);
        }
        if(n%19==0) {assert(p.Render(d.View(),cursor));d.Compare(source.s,cursor);}
    }
    // A buffer skipped for many submissions catches every changed tile.
    assert(p.Render(c.View()));
    for(int n=0;n<15;++n) {source.s.Set(n%sw,(n*7)%sh,Next());assert(p.Submit(source.s));assert(p.Render(a.View()));}
    assert(p.Render(c.View()));c.Compare(source.s,{});
    // External invalidation with the same address/geometry forces a full rebuild.
    auto view=c.View();for(int y=0;y<oh;++y)std::fill(view.pixels+y*view.stridePixels,view.pixels+y*view.stridePixels+ow,0);
    ++c.generation;assert(p.Render(c.View()));c.Compare(source.s,{});
    // A raw/unpreserved writer must invalidate any cached history for that address.
    auto unpreserved=c.View();unpreserved.preserved=false;
    assert(p.Render(unpreserved,{0,0,true}));assert(p.Render(c.View()));c.Compare(source.s,{});
    p.Detach();assert(!p.FrameBytes() && !p.TrackingBytes() && !software.CacheBytes());
}
static void NativeWork() {
    OwnedSurface source;source.Allocate(1280,720);Pattern(source.s);
    SoftwarePresentation software;Presentation p(&software);Target target(1280,720);
    assert(p.Submit(source.s) && p.Render(target.View()));
    const auto fullBytes=1280u*720u*4;
    assert(software.LastWrittenBytes()==fullBytes);
    const auto snapshot=p.Stats().snapshotBytes;
    assert(p.Submit(source.s) && p.Stats().snapshotBytes==snapshot);
    assert(p.Render(target.View()) && !software.LastWrittenBytes());
    source.s.Set(63,63,0xFF1234);assert(p.Submit(source.s));
    auto invalid=target.View();invalid.capacityPixels=1;
    auto before=target.pixels;assert(!p.Render(invalid) && target.pixels==before);
    assert(p.Render(target.View()) && software.LastWrittenBytes()==32*32*4);target.Compare(source.s,{});
    assert(p.Render(target.View(),{60,60,true}) && software.LastWrittenBytes()<4096);target.Compare(source.s,{60,60,true});
    assert(p.Render(target.View(),{900,500,true}) && software.LastWrittenBytes()<4096);target.Compare(source.s,{900,500,true});
    assert(p.Render(target.View()) && software.LastWrittenBytes()<4096);target.Compare(source.s,{});
    const size_t versions=40*23*sizeof(uint64_t);
    assert(p.TrackingBytes()==versions && software.CacheBytes()<=SoftwarePresentation::MaxBuffers*versions+1280*sizeof(int));
    // Excessive fragmented damage falls back to one full image, without holes.
    for(int y=0;y<720;y+=32)for(int x=0;x<1280;x+=32)if((x/32+y/32)%2)source.s.Set(x,y,0xAA9988);
    assert(p.Submit(source.s) && p.Render(target.View()) && software.LastWrittenBytes()==fullBytes);target.Compare(source.s,{});
    // Resize and detach/reattach cannot reuse stale pixels even at one address.
    source.Allocate(37,73);Pattern(source.s);assert(p.Submit(source.s) && p.Render(target.View()));target.Compare(source.s,{});
    p.ClearFrame();source.s.Set(0,0,0xABCDEF);assert(p.Submit(source.s) && p.Render(target.View()));target.Compare(source.s,{});
    p.Attach(nullptr);assert(!p.Render(target.View()));p.Attach(&software);
    assert(p.Render(target.View()));target.Compare(source.s,{});
    struct Fail:PresentationBackend {bool Render(const PresentationFrame&,const OutputBuffer&) override {return false;}} fail;
    source.s.Set(0,0,0x112233);assert(p.Submit(source.s));p.Attach(&fail);
    before=target.pixels;assert(!p.Render(target.View()) && target.pixels==before);
    p.Attach(&software);assert(p.Render(target.View()));target.Compare(source.s,{});
    puts("WORK: 1280x720 single-pixel change writes 4096 bytes; cursor-only update <4096; identical frame writes 0");
}
static void PaletteAndLifetime() {
    SoftwarePresentation software;Presentation p(&software);Target target(95,71);
    for(int bpp:{1,4,8,16,24,32}) {
        OwnedSurface source;source.Allocate(67,45,bpp);
        if(bpp<=8) {source.s.palette.resize(1u<<bpp);for(size_t i=0;i<source.s.palette.size();++i)source.s.palette[i]=uint32_t(i)*0x010203;}
        // Negative pitch is retained while comparing and copying changed rows.
        source.s.bits+=44*source.s.pitch;source.s.pitch=-source.s.pitch;Pattern(source.s);
        assert(p.Submit(source.s) && p.Render(target.View()));target.Compare(source.s,{});
        source.s.Set(33,20,0xFFFF00);
        if(bpp<=8)for(auto& color:source.s.palette)color^=0xABCDEF;
        assert(p.Submit(source.s) && p.Render(target.View(),{33,20,true}));target.Compare(source.s,{33,20,true});
        std::vector<uint32_t> expected=target.pixels;
        source.storage.clear();source.storage.shrink_to_fit();source.s.palette.clear();
        assert(p.Render(target.View(),{33,20,true}) && target.pixels==expected);
    }
}
int main() {
    Alternating(67,45,131,97);Alternating(257,193,67,49);Alternating(9,37,131,97);
    Alternating(37,9,131,97);Alternating(800,600,1280,720);NativeWork();PaletteAndLifetime();
    puts("PASSED: dirty/full pixel equivalence, alternating/evicted buffers, missed revisions, cursor trails, DIB/palette/lifetime, resize/reset, fallback and bounded work");
}
