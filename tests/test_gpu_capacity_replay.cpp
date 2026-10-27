#define GL_GLEXT_PROTOTYPES 1
#include <EGL/egl.h>
#include <GL/glcorearb.h>
#include "source/ui/gpu_gdi_prototype.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <vector>
struct Allocator:ui::PresentationAllocator {
    size_t live=0;bool refuse=false;
    void* Allocate(size_t n) override {if(refuse)return nullptr;void* p=std::malloc(n);if(p)++live;return p;}
    void Free(void* p) override {if(p){assert(live);--live;std::free(p);}}
};
struct Egl {
    EGLDisplay display;EGLSurface surface;EGLContext context;
    Egl() {
        display=eglGetDisplay(EGL_DEFAULT_DISPLAY);assert(eglInitialize(display,nullptr,nullptr) && eglBindAPI(EGL_OPENGL_API));
        EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_NONE};
        EGLConfig config;EGLint n;assert(eglChooseConfig(display,attrs,&config,1,&n) && n==1);
        EGLint size[]={EGL_WIDTH,1280,EGL_HEIGHT,720,EGL_NONE};surface=eglCreatePbufferSurface(display,config,size);assert(surface!=EGL_NO_SURFACE);
        EGLint ver[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,3,EGL_CONTEXT_OPENGL_PROFILE_MASK,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,EGL_NONE};
        context=eglCreateContext(display,config,EGL_NO_CONTEXT,ver);assert(context!=EGL_NO_CONTEXT);
        assert(eglMakeCurrent(display,surface,surface,context));
    }
    void Lose(){assert(eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT));assert(eglDestroyContext(display,context));context=EGL_NO_CONTEXT;}
    ~Egl(){if(context!=EGL_NO_CONTEXT)Lose();assert(eglDestroySurface(display,surface) && eglTerminate(display));}
};
static void Fill(ui::GpuGdiPrototype& image,ui::OwnedSurface& oracle,ui::Rect r,uint32_t rgb) {
    assert(image.Fill(r,rgb));const auto clip=r.Intersect({0,0,oracle.s.width,oracle.s.height});
    for(int y=clip.top;y<clip.bottom;++y)for(int x=clip.left;x<clip.right;++x)oracle.s.Set(x,y,rgb);
}
static void Check(ui::GpuGdiPrototype& image,ui::OwnedSurface& oracle) {
    const auto* actual=image.CpuRead();assert(actual);
    for(int y=0;y<oracle.s.height;++y)for(int x=0;x<oracle.s.width;++x)assert(actual->Get(x,y)==oracle.s.Get(x,y));
}
static void Core(int mode) {
    Egl egl;Allocator allocator;ui::GpuGdiPrototype image(&allocator);assert(image.Initialize(64,64));image.EnableCapacityReplay(true);
    ui::OwnedSurface oracle;oracle.Allocate(64,64);std::vector<uint32_t> input(64,0x315a79);
    for(int n=0;n<64;++n) {
        const ui::Rect r{n%8*8,n/8*8,(n%8+1)*8,(n/8+1)*8};
        if(n==0 && mode==1){assert(image.Copy(r,input.data(),input.size()));for(int y=0;y<8;++y)for(int x=0;x<8;++x)oracle.s.Set(x,y,0x315a79);std::fill(input.begin(),input.end(),0xdead00);}
        else Fill(image,oracle,r,0x100000u+unsigned(n)*0x917u);
    }
    if(mode==2)assert(image.FlushGpu());
    if(mode==3)glEnable(0xdead);
    Fill(image,oracle,{1,1,7,7},0xa1b2c3);
    assert(image.Counters().capacityCheckpoints==1 && image.JournalCommands()<=64 && !image.InputBytes());
    if(mode==3)assert(image.Backend()==ui::GpuGdiPrototype::Mode::CpuFallback && image.Counters().replayFailures==1 && !image.Counters().replayCheckpoints);
    else assert(image.Counters().replayCheckpoints==1 && image.Counters().replayCommands==64 && image.Counters().replayPixels==4096 && !image.Counters().readbacks);
    assert(allocator.live==2); // checkpoint and scratch, owned copy released
    if(mode==1){egl.Lose();image.ContextLost();}
    Check(image,oracle);
    if(mode!=1 && mode!=3)assert(image.Counters().readbacks==1); // explicit read still really reads GPU
    image.Shutdown();assert(!allocator.live);printf("Capacity replay case %d PASS\n",mode);
}
static void Budget() {
    Egl egl;ui::GpuGdiPrototype image;assert(image.Initialize(65,65));image.EnableCapacityReplay(true);
    ui::OwnedSurface oracle;oracle.Allocate(65,65);
    for(unsigned n=0;n<64;++n)Fill(image,oracle,{0,0,65,65},0x400000+n);
    Fill(image,oracle,{2,2,3,3},0x123456);
    assert(image.Counters().replayBudgetFallbacks==1 && !image.Counters().replayCheckpoints && image.Counters().readbacks==1);
    Check(image,oracle);image.Shutdown();puts("Capacity replay work budget PASS");
}
static void Alias() {
    Egl egl;Allocator allocator;ui::GpuGdiPrototype image(&allocator);assert(image.Initialize(512,512));image.EnableCapacityReplay(true);
    assert(image.Fill({0,0,512,512},0x112233));const auto* borrowed=image.CpuRead();assert(borrowed);
    std::vector<uint32_t> input(512*512);
    for(unsigned n=1;n<=4;++n){std::fill(input.begin(),input.end(),0x100000+n);assert(image.Copy({0,0,512,512},input.data(),input.size()));}
    assert(image.InputBytes()==image.MaxInputBytes && image.JournalCommands()==4);
    allocator.refuse=true;assert(!image.Copy({-3,-2,509,510},reinterpret_cast<const uint32_t*>(borrowed->bits),input.size()));
    assert(image.InputBytes()==image.MaxInputBytes && image.JournalCommands()==4 && !image.Counters().capacityCheckpoints);allocator.refuse=false;
    assert(image.Copy({-3,-2,509,510},reinterpret_cast<const uint32_t*>(borrowed->bits),input.size()));
    assert(image.Counters().replayBudgetFallbacks==1 && !image.Counters().replayCheckpoints && image.InputBytes()==input.size()*4);
    const auto* actual=image.CpuRead();assert(actual);
    for(int y=0;y<512;++y)for(int x=0;x<512;++x)assert(actual->Get(x,y)==(x<509 && y<510?0x112233u:0x100004u));
    image.Shutdown();assert(!allocator.live);puts("Capacity input budget/failed allocation/clipped owned alias PASS");
}
int main(){for(int mode=0;mode<4;++mode)Core(mode);Budget();Alias();puts("Capacity checkpoint replay PASS (GPU submission/order/owned copy/real read/work budget/input alias/allocation/GL fault/actual loss)");}
