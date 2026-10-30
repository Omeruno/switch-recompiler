#include "source/ui/presentation.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

using namespace ui;
static uint32_t Rgba(uint32_t c) {return 0xFF000000u|((c>>16)&255)|(c&0xFF00)|((c&255)<<16);}
// Frozen pre-E Switch algorithm for its supported 4:3 and 16:9 inputs.
// Deliberately does not call the new viewport/backend helpers.
static void Legacy(const Surface& s,std::vector<uint32_t>& out,PresentationCursor cursor={}) {
    const int outW=std::min(1280,s.width*720/s.height),off=(1280-outW)/2;
    static std::vector<int> columns;columns.resize(outW);
    for(int x=0;x<outW;++x)columns[x]=x*s.width/outW;
    for(int y=0;y<720;++y) {
        auto* row=out.data()+size_t(y)*1280;int sy=y*s.height/720;
        for(int x=0;x<off;++x)row[x]=0xFF000000u;
        for(int x=off+outW;x<1280;++x)row[x]=0xFF000000u;
        if(s.bpp==32) {
            const auto* src=reinterpret_cast<const uint32_t*>(s.Row(sy));
            for(int x=0;x<outW;++x)row[off+x]=Rgba(src[columns[x]]);
        } else for(int x=0;x<outW;++x)row[off+x]=Rgba(s.Get(columns[x],sy));
    }
    static const char* const arrow[]={"X","XX","X.X","X..X","X...X","X....X","X.....X","X......X","X.......X","X........X",
        "X.....XXXXX","X..X..X","X.X X..X","XX  X..X","X    X..X","     X..X","      X..X","      X..X","       XX"};
    if(cursor.visible) {
        int px=off+cursor.x*outW/s.width,py=cursor.y*720/s.height;
        for(int y=0;y<19;++y)for(int x=0;arrow[y][x];++x) {
            char c=arrow[y][x];if(c!=' ' && px+x<1280 && py+y<720)
                out[size_t(py+y)*1280+px+x]=c=='X'?0xFF000000u:0xFFFFFFFFu;
        }
    }
}
static void Pattern(Surface& s) {
    for(int y=0;y<s.height;++y)for(int x=0;x<s.width;++x)
        s.Set(x,y,((x*17u+y*3u)&255)<<16|((y*19u)&255)<<8|((x^y)&255));
}
static void CheckViewport() {
    struct Case {int sw,sh,x,y,w,h;};
    for(auto c:{Case{1280,720,0,0,1280,720},Case{800,600,160,0,960,720},
        Case{1920,1080,0,0,1280,720},Case{1600,600,0,120,1280,480},Case{600,800,370,0,540,720}}) {
        auto v=FitViewport(c.sw,c.sh,1280,720);
        assert(v.x==c.x && v.y==c.y && v.width==c.w && v.height==c.h);
        float x=-1,y=-1;assert(v.ToSource(v.x,v.y,c.sw,c.sh,x,y) && x==0 && y==0);
        assert(v.Contains(v.x,v.y) && !v.Contains(v.x-1,v.y) && !v.Contains(v.x+v.width,v.y));
        assert(v.ToSource(-10000,-10000,c.sw,c.sh,x,y) && x==0 && y==0);
        assert(v.ToSource(10000,10000,c.sw,c.sh,x,y) && x==c.sw-1 && y==c.sh-1);
    }
    assert(!FitViewport(0,600,1280,720).Valid());
    assert(!FitViewport(800,600,-1,720).Valid());
    assert(!FitViewport(std::numeric_limits<int>::max(),600,1280,720).Valid());
}
static void Golden(int w,int h) {
    OwnedSurface source;source.Allocate(w,h);Pattern(source.s);
    SoftwarePresentation backend;Presentation p(&backend);
    std::vector<uint32_t> actual(1280*720),expected(actual.size());
    OutputBuffer out{actual.data(),1280,720,1280,actual.size()};
    assert(p.Submit(source.s));
    for(auto c:{PresentationCursor{},PresentationCursor{0,0,true},PresentationCursor{w/2,h/2,true},PresentationCursor{w-1,h-1,true}}) {
        assert(p.Render(out,c));Legacy(source.s,expected,c);assert(actual==expected);
    }
    // Cursor movement clears the previous shape even without guest drawing.
    assert(p.Render(out));Legacy(source.s,expected);assert(actual==expected);
    assert(p.Stats().submitted==1 && p.Stats().rendered==5 && p.Stats().redraws==4);
    assert(p.FrameBytes()==size_t(w)*h*4);
    p.Detach();assert(!p.SourceWidth() && !p.FrameBytes() && !p.Render(out));
}
static void FormatsAndOwnership() {
    SoftwarePresentation backend;Presentation p(&backend);
    const size_t stride=13;
    std::vector<uint32_t> pixels(stride*11+4,0xDEADBEEF);
    OutputBuffer out{pixels.data()+2,9,11,stride,stride*11};
    for(int bpp:{1,4,8,16,24,32})for(bool reverse:{false,true})for(bool rgb565:{false,true}) {
        OwnedSurface source;source.Allocate(7,5,bpp);source.s.rgb565=rgb565;
        if(bpp<=8) {source.s.palette.resize(1u<<bpp);for(size_t i=0;i<source.s.palette.size();++i)source.s.palette[i]=uint32_t(i)*0x010301u;}
        if(reverse) {source.s.bits+=source.s.pitch*4;source.s.pitch=-source.s.pitch;}
        Pattern(source.s);
        std::vector<uint32_t> expected(35);for(int y=0;y<5;++y)for(int x=0;x<7;++x)expected[y*7+x]=source.s.Get(x,y);
        assert(p.Submit(source.s));
        // Invalidate the borrowed buffer and palette BEFORE render/redraw.
        source.storage.clear();source.storage.shrink_to_fit();source.s.palette.clear();source.s.bits=nullptr;
        assert(p.Render(out));auto v=FitViewport(7,5,9,11);
        for(int y=0;y<11;++y)for(int x=0;x<9;++x) {
            uint32_t want=0xFF000000u;
            if(v.Contains(x,y))want=Rgba(expected[(y-v.y)*5/v.height*7+(x-v.x)*7/v.width]);
            assert(out.pixels[y*stride+x]==want);
        }
        for(int y=0;y<11;++y)for(size_t x=9;x<stride;++x)assert(out.pixels[y*stride+x]==0xDEADBEEF);
        assert(pixels[0]==0xDEADBEEF && pixels[1]==0xDEADBEEF && pixels[pixels.size()-1]==0xDEADBEEF);
    }
    OwnedSurface s;s.Allocate(2,2);Pattern(s.s);assert(p.Submit(s.s));
    assert(p.Render(out));auto before=pixels;
    auto invalid=out;invalid.capacityPixels=1;assert(!p.Render(invalid) && pixels==before);
    invalid=out;invalid.stridePixels=std::numeric_limits<size_t>::max();assert(!p.Render(invalid));
    invalid=out;invalid.stridePixels=1;assert(!p.Render(invalid));
    p.Attach(nullptr);assert(!p.Render(out) && pixels==before);p.Attach(&backend);assert(p.Render(out));
    auto malformed=s.s;malformed.pitch=1;assert(!p.Submit(malformed) && !p.Render(out) && !p.FrameBytes());
    malformed=s.s;malformed.bpp=3;assert(!p.Submit(malformed));
    malformed=s.s;malformed.width=8192;malformed.height=8192;malformed.pitch=8192*4;
    assert(!p.Submit(malformed)); // rejected before reading undersized source
    assert(p.Submit(s.s));p.ClearFrame();assert(!p.Render(out));
    struct Failed:PresentationBackend {bool Render(const PresentationFrame&,const OutputBuffer&) override{return false;}} failed;
    assert(p.Submit(s.s));p.Attach(&failed);auto count=p.Stats().rendered;assert(!p.Render(out) && p.Stats().rendered==count);
    p.Attach(&backend);assert(p.Render(out));
    // Direct guest-style writes are observed on the next explicit submission.
    s.s.Set(0,0,0xFF0000);assert(p.Submit(s.s));assert(p.Render(out));
    auto v=FitViewport(2,2,9,11);assert(out.pixels[v.y*stride+v.x]==0xFF0000FFu);
}
static void Benchmark(int w,int h) {
    OwnedSurface source;source.Allocate(w,h);Pattern(source.s);
    SoftwarePresentation backend;Presentation p(&backend);std::vector<uint32_t> pixels(1280*720),old(pixels.size());
    OutputBuffer out{pixels.data(),1280,720,1280,pixels.size()};
    constexpr unsigned rounds=12;
    auto start=std::chrono::steady_clock::now();
    for(unsigned i=0;i<rounds;++i)Legacy(source.s,old);
    auto oldNs=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
    for(unsigned i=0;i<rounds;++i) {assert(p.Submit(source.s));assert(p.Render(out));}
    assert(old==pixels);auto s=p.Stats();
    std::printf("BENCH host-only %dx%d ->1280x720 rounds=%u legacy_ns=%llu snapshot_ns=%llu render_ns=%llu frame_bytes=%zu output_bytes=%llu\n",
        w,h,rounds,(unsigned long long)oldNs,(unsigned long long)s.snapshotNs,(unsigned long long)s.renderNs,p.FrameBytes(),(unsigned long long)s.outputBytes);
}
static void NativeRows() {
    // Native-width conversion must tolerate byte-aligned, padded DIB rows,
    // ignore guest alpha, preserve output padding, and handle bottom-up input.
    for(bool reverse:{false,true}) {
        std::vector<uint8_t> bytes(1+29*5);
        Surface source;source.width=7;source.height=5;source.bpp=32;
        source.pitch=reverse?-29:29;source.bits=bytes.data()+1+(reverse?29*4:0);
        for(int y=0;y<5;++y)for(int x=0;x<7;++x) {
            uint32_t pixel=0x7F000000u|uint32_t(x*31)<<16|uint32_t(y*47)<<8|uint32_t(x+y);
            std::memcpy(source.Row(y)+x*4,&pixel,4);
        }
        std::vector<uint32_t> pixels(9*9,0xDEADBEEF);
        OutputBuffer out{pixels.data(),7,9,9,pixels.size()};
        SoftwarePresentation backend;
        assert(backend.Render({source,FitViewport(7,5,7,9),{},1},out));
        for(int y=0;y<9;++y)for(int x=0;x<9;++x) {
            uint32_t want=0xDEADBEEF;
            if(x<7)want=(y<2 || y>=7)?0xFF000000u:
                (0xFF000000u|uint32_t(x*31)|uint32_t((y-2)*47)<<8|uint32_t(x+y-2)<<16);
            assert(pixels[y*9+x]==want);
        }
    }
}
int main() {
    {const auto viewport=ui::FitViewport(320,180,1280,720);float x=-1,y=-1;
        assert(viewport.ToSource(0,0,ui::Rect(20,30,340,210),x,y) && x==20 && y==30);
        assert(viewport.ToSource(640,360,ui::Rect(20,30,340,210),x,y) && x==180 && y==120);
        assert(viewport.ToSource(1279,719,ui::Rect(20,30,340,210),x,y) && x==339 && y==209);
        assert(!viewport.ToSource(0,0,ui::Rect{},x,y) && x==339 && y==209);
        assert(!viewport.ToSource(0,0,ui::Rect(INT32_MIN,0,INT32_MAX,720),x,y) && x==339 && y==209);
        const auto boxed=ui::FitViewport(320,240,1280,720);assert(!boxed.Contains(0,100));
        assert(boxed.ToSource(-500,100,ui::Rect(15,25,335,265),x,y) && x==15);
    }

    CheckViewport();Golden(800,600);Golden(1280,720);FormatsAndOwnership();NativeRows();Benchmark(800,600);Benchmark(1280,720);
    puts("PASSED: old-output pixel equivalence, Lite full width, aspect/input mapping, DIB formats, padding, owned redraw, detach/resize/failure and bounded metrics");
}
