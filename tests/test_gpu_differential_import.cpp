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
static void Check(ui::GpuGdiPrototype& image,const std::vector<uint32_t>& expected,int w,int h) {
    const auto* actual=image.CpuRead();assert(actual);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x)assert(actual->Get(x,y)==(expected[size_t(y)*w+x]&0xffffff));
}
static void Run(int mode,int w,int h) {
    Egl egl;Allocator allocator;ui::GpuGdiPrototype image(&allocator);assert(image.Initialize(w,h));
    image.EnableDifferentialImport(mode!=0);
    std::vector<uint32_t> pixels(size_t(w)*h);
    for(size_t i=0;i<pixels.size();++i)pixels[i]=0xa0000000u|uint32_t((i*0x719bu+0x124567u)&0xffffff);
    assert(image.ImportCpu(pixels.data(),pixels.size()));
    assert(image.Counters().uploadBytes==pixels.size()*4 && !image.Counters().readbacks);
    assert(image.Fill({0,0,1,1},0x876543));
    const auto before=image.Counters().importScanPixels;
    assert(!image.ImportCpu(pixels.data(),pixels.size()) && image.Counters().importScanPixels==before);
    pixels[0]=0x876543;Check(image,pixels,w,h); // real device boundary, not an import shortcut
    for(auto& p:pixels)p^=0xff000000; // unused bytes must not create changes
    const auto upload=image.Counters().uploadBytes;
    assert(image.ImportCpu(pixels.data(),pixels.size()));
    assert(image.Counters().uploadBytes==upload+(mode==0?pixels.size()*4:0));
    assert(image.Counters().importUnchanged==uint64_t(mode!=0));
    const auto* alias=image.CpuRead();assert(alias);
    assert(image.ImportCpu(reinterpret_cast<const uint32_t*>(alias->bits),pixels.size()));
    // Two separated changed pixels force a bounding rectangle with unchanged interior.
    const size_t last=size_t(h-1)*w+w-1;pixels[0]=0xfedcba;pixels[last]=0x102030;
    allocator.refuse=true;const auto old=image.Counters().cpuImports;
    assert(!image.ImportCpu(pixels.data(),pixels.size()) && image.Counters().cpuImports==old && !image.JournalCommands());
    allocator.refuse=false;
    auto prior=pixels;prior[0]=0x876543;prior[last]=uint32_t((last*0x719bu+0x124567u)&0xffffff);
    if(last==0)prior[0]=0x876543;
    Check(image,prior,w,h); // failed input allocation accepted no changed pixels
    if(mode==2)glEnable(0xdead);
    assert(image.ImportCpu(pixels.data(),pixels.size()));
    if(mode==2)assert(image.Backend()==ui::GpuGdiPrototype::Mode::CpuFallback);
    else assert(image.Backend()==ui::GpuGdiPrototype::Mode::Gpu);
    auto expected=pixels;std::fill(pixels.begin(),pixels.end(),0xdead00); // source may immediately mutate
    Check(image,expected,w,h);
    if(mode!=2){
        const size_t at=size_t(h/2)*w+w/2;expected[at]=0x152637;
        const auto bytes=image.Counters().uploadBytes;
        assert(image.ImportCpu(expected.data(),expected.size()));
        assert(image.Counters().uploadBytes==bytes+(mode==0?expected.size()*4:4));
        if(mode==3){
            assert(image.Fill({0,0,1,1},0x554433));expected[0]=0x554433;
            egl.Lose();image.ContextLost();assert(image.Backend()==ui::GpuGdiPrototype::Mode::CpuFallback);
        }
        Check(image,expected,w,h);
    }
    assert(!image.ImportCpu(nullptr,expected.size()) && !image.ImportCpu(expected.data(),expected.size()-1));
    image.Shutdown();assert(!allocator.live);
    printf("Differential import mode%d %dx%d PASS\n",mode,w,h);
}
static void PartialFault() {
    Egl egl;Allocator allocator;ui::GpuGdiPrototype image(&allocator);assert(image.Initialize(31,23));image.EnableDifferentialImport(true);
    std::vector<uint32_t> pixels(31*23,0xabcdef);assert(image.ImportCpu(pixels.data(),pixels.size()));
    pixels[31*7+11]=0x125634;glEnable(0xdead);
    assert(image.ImportCpu(pixels.data(),pixels.size()) && image.Backend()==ui::GpuGdiPrototype::Mode::CpuFallback);
    assert(image.Counters().recoveries==1 && image.Counters().cpuImports==2 && image.Counters().importPackedPixels==1);
    Check(image,pixels,31,23);image.Shutdown();assert(!allocator.live);
}
static void State() {
    Egl egl;ui::GpuGdiPrototype image;assert(image.Initialize(31,23));image.EnableDifferentialImport(true);
    std::vector<uint32_t> pixels(31*23,0x314159);assert(image.ImportCpu(pixels.data(),pixels.size()));
    GLuint pbo;glGenBuffers(1,&pbo);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,pbo);glBufferData(GL_PIXEL_UNPACK_BUFFER,16,nullptr,GL_STATIC_DRAW);
    glPixelStorei(GL_UNPACK_ALIGNMENT,8);glPixelStorei(GL_UNPACK_ROW_LENGTH,47);glPixelStorei(GL_UNPACK_SKIP_ROWS,3);glPixelStorei(GL_UNPACK_SWAP_BYTES,1);
    glViewport(3,4,13,17);glEnable(GL_SCISSOR_TEST);glScissor(1,2,3,4);
    pixels[31*11+15]=0xabcdef;assert(image.ImportCpu(pixels.data(),pixels.size()));
    GLint value=0;glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&value);assert(value==GLint(pbo));
    glGetIntegerv(GL_UNPACK_ALIGNMENT,&value);assert(value==8);glGetIntegerv(GL_UNPACK_ROW_LENGTH,&value);assert(value==47);
    glGetIntegerv(GL_UNPACK_SKIP_ROWS,&value);assert(value==3);glGetIntegerv(GL_UNPACK_SWAP_BYTES,&value);assert(value==1);
    GLint viewport[4];glGetIntegerv(GL_VIEWPORT,viewport);assert(viewport[0]==3 && viewport[1]==4 && viewport[2]==13 && viewport[3]==17 && glIsEnabled(GL_SCISSOR_TEST));
    Check(image,pixels,31,23);assert(image.Counters().uploadBytes==(31*23+1)*4 && image.Counters().readbacks==1);
    image.Shutdown();glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);glDeleteBuffers(1,&pbo);assert(glGetError()==GL_NO_ERROR);
}
int main(){for(int mode=0;mode<4;++mode)for(int side:{1,31,512})Run(mode,side,side==31?23:side);PartialFault();State();puts("Differential CPU import PASS (actual GPU pixels/empty journal/no-change/high byte/owned source/rectangle gaps/alias/refusal/state/GL error/actual loss)");}
