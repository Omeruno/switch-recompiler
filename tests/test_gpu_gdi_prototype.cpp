#define GL_GLEXT_PROTOTYPES 1
#include <EGL/egl.h>
#include <GL/glcorearb.h>
#include "source/ui/gpu_gdi_prototype.h"
#include "source/ui/gdi.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <vector>
struct Allocator:ui::PresentationAllocator {
    size_t calls=0,failAt=0,live=0;
    void* Allocate(size_t n) override {if(++calls==failAt)return nullptr;auto* p=std::malloc(n);if(p)++live;return p;}
    void Free(void* p) override {if(p){assert(live);--live;std::free(p);}}
};
struct Reference {
    ui::OwnedSurface pixels;ui::Gdi gdi;uint32_t dc;
    Reference(int w,int h){pixels.Allocate(w,h);gdi.SetScreen(&pixels.s);dc=gdi.CreateDc(ui::DcKind::Screen);assert(dc);}
    void Fill(ui::Rect r,uint32_t rgb) {
        auto brush=gdi.CreateSolidBrush(ui::RgbToColorref(rgb));assert(gdi.FillRect(dc,r,brush));assert(gdi.DeleteObject(brush));
    }
    void Copy(ui::Rect r,const std::vector<uint32_t>& rgb) {
        ui::Surface view;view.width=r.Width();view.height=r.Height();view.pitch=view.width*4;
        view.bits=reinterpret_cast<uint8_t*>(const_cast<uint32_t*>(rgb.data()));
        auto bitmap=gdi.CreateDibSection(view,0,{}),src=gdi.CreateCompatibleDc(dc);assert(bitmap && src);
        auto old=gdi.SelectObject(src,bitmap);assert(gdi.BitBlt(dc,r.left,r.top,r.Width(),r.Height(),src,0,0,0xcc0020));
        gdi.SelectObject(src,old);assert(gdi.DeleteDc(src));assert(gdi.DeleteObject(bitmap));
    }
};
static void Compare(ui::GpuGdiPrototype& gpu,const Reference& reference) {
    const auto* actual=gpu.CpuRead();assert(actual && actual->width==reference.pixels.s.width && actual->height==reference.pixels.s.height);
    for(int y=0;y<actual->height;++y)for(int x=0;x<actual->width;++x)
        assert(actual->Get(x,y)==reference.pixels.s.Get(x,y));
}
static EGLContext Context(EGLDisplay d,EGLConfig c) {
    const EGLint attrs[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,3,EGL_CONTEXT_OPENGL_PROFILE_MASK,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,EGL_NONE};
    auto ctx=eglCreateContext(d,c,EGL_NO_CONTEXT,attrs);assert(ctx!=EGL_NO_CONTEXT);return ctx;
}
int main() {
    auto display=eglGetDisplay(EGL_DEFAULT_DISPLAY);assert(display!=EGL_NO_DISPLAY);
    assert(eglInitialize(display,nullptr,nullptr) && eglBindAPI(EGL_OPENGL_API));
    const EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
    EGLConfig config;EGLint count;assert(eglChooseConfig(display,attrs,&config,1,&count) && count==1);
    const EGLint size[]={EGL_WIDTH,8,EGL_HEIGHT,8,EGL_NONE};auto surface=eglCreatePbufferSurface(display,config,size);assert(surface!=EGL_NO_SURFACE);
    auto context=Context(display,config);assert(eglMakeCurrent(display,surface,surface,context));
    using Gpu=ui::GpuGdiPrototype;
    Allocator allocator;
    for(size_t failure:{1u,2u}) {
        allocator.calls=0;allocator.failAt=failure;Gpu gpu(&allocator);assert(!gpu.Initialize(31,23));assert(!allocator.live);
    }
    allocator.failAt=0;
    for(bool batching:{false,true}) {
        Gpu gpu(&allocator);assert(gpu.Initialize(31,23));gpu.EnableBatching(batching);Reference ref(31,23);
        auto fill=[&](ui::Rect r,uint32_t color){assert(gpu.Fill(r,color));ref.Fill(r,color);};
        fill({-2,-2,8,8},0x123456);fill({8,0,16,8},0xabcdef);
        assert(gpu.FlushGpu());assert(gpu.Counters().draws==(batching?1u:2u));
        fill({4,0,12,8},0x719253);fill({0,10,8,18},0x1a2b3c);
        std::vector<uint32_t> input(4,0xa72468ac);assert(gpu.Copy({6,4,8,6},input.data(),4));ref.Copy({6,4,8,6},input);
        fill({18,0,27,7},0x012345);fill({18,8,29,21},0x987654);
        assert(gpu.FlushGpu());assert(gpu.Counters().draws==(batching?4u:7u));
        assert(gpu.Counters().submittedCommands==7 && gpu.Counters().uploads==1 && gpu.Counters().readbacks==0);
        assert(gpu.Counters().batches==(batching?3u:0u) && gpu.Counters().batchedCommands==(batching?6u:0u));
        const auto draws=gpu.Counters().draws;assert(gpu.FlushGpu() && gpu.Counters().draws==draws);
        Compare(gpu,ref);assert(gpu.Backend()==Gpu::Mode::Gpu);
        fill({0,0,8,8},0x123456);fill({8,0,16,8},0xabcdef);assert(gpu.FlushGpu());
        fill({3,3,11,11},0x654321);glEnable(0xffffffffu);assert(!gpu.FlushGpu());Compare(gpu,ref);
        assert(gpu.Backend()==Gpu::Mode::CpuFallback);
    }
    assert(!allocator.live);
    {Gpu invalid;assert(!invalid.Initialize(0,1));assert(!invalid.Initialize(Gpu::MaxSide+1,1));}
    for(auto dimensions:{ui::Point{1,1},ui::Point{31,23},ui::Point{512,512}}) {
        Gpu gpu(&allocator);assert(gpu.Initialize(dimensions.x,dimensions.y));Reference ref(dimensions.x,dimensions.y);
        assert(gpu.Backend()==Gpu::Mode::Gpu);Compare(gpu,ref);
        for(int round=0;round<3;++round) {
            for(int n=0;n<75;++n) {
                ui::Rect r{n%19-4,n%13-3,n%19+12,n%13+9};
                const auto color=uint32_t(n*77591+round*19271)&0xffffff;
                assert(gpu.Fill(r,color));ref.Fill(r,color);
                std::vector<uint32_t> input(8*6);for(size_t i=0;i<input.size();++i)input[i]=0xa7000000u|((i*3817+n*1729)&0xffffff);
                ui::Rect tile{n%17-3,n%11-2,n%17+5,n%11+4};
                assert(gpu.Copy(tile,input.data(),input.size()));ref.Copy(tile,input);
                std::fill(input.begin(),input.end(),0xff00ff); // queued copy must own the original bytes
                if(n%9==0)assert(gpu.FlushGpu()); // flushed commands must survive until checkpoint
                assert(gpu.JournalCommands()<=Gpu::MaxCommands && gpu.InputBytes()<=Gpu::MaxInputBytes);
            }
            Compare(gpu,ref);assert(gpu.Backend()==Gpu::Mode::Gpu && gpu.JournalCommands()==0);
        }
        assert(gpu.Counters().draws && gpu.Counters().readbacks && gpu.Counters().checkpoints);
        assert(!gpu.Copy({0,0,2,2},nullptr,4));uint32_t pixel=0;
        assert(!gpu.Copy({0,0,2,2},&pixel,1));assert(!gpu.Copy({0,0,0x7fffffff,2},&pixel,1));
        assert(gpu.Fill({0,0,1,1},0x719253));ref.Fill({0,0,1,1},0x719253);
        const auto old=gpu.JournalCommands();allocator.failAt=allocator.calls+1;
        assert(!gpu.Copy({0,0,1,1},&pixel,1));assert(gpu.JournalCommands()==old);allocator.failAt=0;Compare(gpu,ref);
        assert(gpu.Fill({0,0,dimensions.x,dimensions.y},0x12ab34));ref.Fill({0,0,dimensions.x,dimensions.y},0x12ab34);
        assert(gpu.FlushGpu());glEnable(0xffffffffu); // readback failure after commands have already run
        Compare(gpu,ref);assert(gpu.Backend()==Gpu::Mode::CpuFallback && gpu.Counters().recoveries==1);
        assert(gpu.Fill({0,0,1,1},0xff0000));ref.Fill({0,0,1,1},0xff0000);Compare(gpu,ref);
        assert(!gpu.FlushGpu());gpu.Shutdown();assert(!allocator.live);
    }
    {
        Gpu gpu(&allocator);assert(gpu.Initialize(512,512));Reference ref(512,512);
        std::vector<uint32_t> input(512*512,0xa7123456);
        for(int n=0;n<7;++n){assert(gpu.Copy({0,0,512,512},input.data(),input.size()));ref.Copy({0,0,512,512},input);}
        assert(gpu.Counters().checkpoints>=1);Compare(gpu,ref); // byte cap, before command cap
        assert(gpu.Fill({3,4,17,21},0xdeadbe));ref.Fill({3,4,17,21},0xdeadbe);
        glEnable(0xffffffffu);assert(!gpu.FlushGpu());Compare(gpu,ref);assert(gpu.Backend()==Gpu::Mode::CpuFallback);
    }
    assert(!allocator.live);
    {
        Gpu gpu(&allocator);assert(gpu.Initialize(8,8));Reference ref(8,8);
        assert(gpu.Fill({0,0,8,8},0x123456));ref.Fill({0,0,8,8},0x123456);
        const auto* borrowed=gpu.CpuRead();assert(borrowed);
        std::vector<uint32_t> old(4,0x123456);
        for(size_t n=0;n<Gpu::MaxCommands;++n){assert(gpu.Fill({0,0,8,8},0xabcdef));ref.Fill({0,0,8,8},0xabcdef);}
        // This copy itself triggers a checkpoint that changes the borrowed
        // backing store. Enqueue must first capture its original contents.
        assert(gpu.Copy({0,0,2,2},reinterpret_cast<const uint32_t*>(borrowed->bits),4));ref.Copy({0,0,2,2},old);Compare(gpu,ref);
    }
    assert(!allocator.live);
    {
        Gpu gpu(&allocator);assert(gpu.Initialize(31,23));Reference ref(31,23);
        assert(gpu.Fill({0,0,15,23},0x654321));ref.Fill({0,0,15,23},0x654321);
        assert(gpu.Fill({15,0,31,23},0x123456));ref.Fill({15,0,31,23},0x123456);
        assert(gpu.FlushGpu() && gpu.Counters().batches==1);
        std::vector<uint32_t> tile(12,0xabcdef);assert(gpu.Copy({2,3,6,6},tile.data(),tile.size()));ref.Copy({2,3,6,6},tile);
        assert(eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT));assert(eglDestroyContext(display,context));
        gpu.ContextLost();Compare(gpu,ref);assert(gpu.Backend()==Gpu::Mode::CpuFallback);
        // Destructor after real context loss must issue zero driver calls.
    }
    assert(!allocator.live);assert(eglDestroySurface(display,surface));assert(eglTerminate(display));
    puts("GPU GDI prototype: PASS (actual EGL fill/copy, full pixels, owned input, bounds/caps, readback, GL failure and real context loss replay)");
}
