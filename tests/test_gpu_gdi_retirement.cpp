#define GL_GLEXT_PROTOTYPES 1
#include <EGL/egl.h>
#include <GL/glcorearb.h>
#include "source/ui/gpu_gdi_prototype.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <vector>
struct Allocator:ui::PresentationAllocator {
    size_t live=0;
    void* Allocate(size_t n) override {void* p=std::malloc(n);if(p)++live;return p;}
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
static void Check(ui::GpuGdiPrototype& image,const ui::OwnedSurface& oracle) {
    const auto* pixels=image.CpuRead();assert(pixels);
    for(int y=0;y<64;++y)for(int x=0;x<64;++x)assert(pixels->Get(x,y)==oracle.s.Get(x,y));
}
static void Fill(ui::GpuGdiPrototype& image,ui::OwnedSurface& oracle,ui::Rect r,uint32_t color) {
    assert(image.Fill(r,color));const auto c=r.Intersect({0,0,64,64});
    for(int y=c.top;y<c.bottom;++y)for(int x=c.left;x<c.right;++x)oracle.s.Set(x,y,color);
}
static void Run(int mode) {
    Egl egl;Allocator allocator;ui::GpuGdiPrototype image(&allocator);assert(image.Initialize(64,64));image.EnableCoveredRetirement(true);
    ui::OwnedSurface oracle;oracle.Allocate(64,64);
    std::vector<uint32_t> copy(64,0x314159);
    assert(image.Copy({0,0,8,8},copy.data(),copy.size()));
    for(int y=0;y<8;++y)for(int x=0;x<8;++x)oracle.s.Set(x,y,0x314159);
    copy.clear(); // image owns its snapshot
    for(int n=1;n<64;++n)Fill(image,oracle,{n%8*8,n/8*8,(n%8+1)*8,(n/8+1)*8},0x123400+unsigned(n));
    assert(image.JournalCommands()==64 && image.InputBytes()==256);
    if(mode!=4)assert(image.FlushGpu());
    if(mode==0) {
        Fill(image,oracle,{1,1,7,7},0x778899); // partial copy coverage: checkpoint required
        assert(image.Counters().capacityCheckpoints==1 && !image.Counters().coveredRetired);
    } else {
        Fill(image,oracle,{0,0,8,8},0xaabbcc);
        if(mode==4)assert(image.Counters().capacityCheckpoints==1 && !image.Counters().coveredRetired);
        else {
            assert(image.Counters().coveredRetired==1 && !image.Counters().capacityCheckpoints);
            assert(image.InputBytes()==0 && image.JournalCommands()==64 && allocator.live==2);
        }
        if(mode==1) {
            Fill(image,oracle,{4,0,16,8},0x998877); // retain overlapping pending suffix order
            assert(image.Counters().coveredRetired==2 && !image.Counters().capacityCheckpoints);
            Fill(image,oracle,{1,1,7,7},0x918273); // no fully covered flushed write
            assert(image.Counters().capacityCheckpoints==1);
        }
        if(mode==2){egl.Lose();image.ContextLost();assert(image.Backend()==ui::GpuGdiPrototype::Mode::CpuFallback);}
        if(mode==3)glEnable(0xdead); // real GL error → checkpoint+journal recovery
    }
    Check(image,oracle);
    if(mode==2 || mode==3)assert(image.Counters().recoveries==1);
    image.Shutdown();assert(!allocator.live);printf("Covered journal case %d PASS\n",mode);
}
int main(){for(int mode=0;mode<5;++mode)Run(mode);puts("Covered journal retirement: PASS (partial/pending/copy storage/order/GL error/actual loss)");}
