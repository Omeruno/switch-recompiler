#define GL_GLEXT_PROTOTYPES 1
#include <EGL/egl.h>
#include <GL/glcorearb.h>
#include "source/ui/opengl_presentation.h"
#include <cassert>
#include <cstdio>
#include <vector>
using namespace ui;
static void Compare(OpenGlPresentation& gpu,Presentation& p,const Surface& source,int w,int h,PresentationCursor cursor) {
    assert(p.Render({nullptr,w,h,0,0},cursor));
    std::vector<uint32_t> actual(size_t(w)*h),expected(actual.size());
    glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,actual.data());
    assert(glGetError()==GL_NO_ERROR);
    SoftwarePresentation software;
    assert(software.Render({source,FitViewport(source.width,source.height,w,h),cursor,1},
        {expected.data(),w,h,size_t(w),expected.size()}));
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        auto a=actual[size_t(h-1-y)*w+x],e=expected[size_t(y)*w+x];
        if(a!=e) {std::fprintf(stderr,"GPU mismatch %dx%d -> %dx%d (%d,%d) %08x != %08x; %s\n",
            source.width,source.height,w,h,x,y,a,e,gpu.Error());assert(false);}
    }
}
static void Scenario(EGLDisplay display,EGLConfig config,EGLContext context,int sw,int sh,int w,int h,int bpp) {
    EGLint attrs[]={EGL_WIDTH,w,EGL_HEIGHT,h,EGL_NONE};
    EGLSurface surface=eglCreatePbufferSurface(display,config,attrs);assert(surface!=EGL_NO_SURFACE);
    assert(eglMakeCurrent(display,surface,surface,context));
    OpenGlPresentation gpu;assert(gpu.Initialize());Presentation p(&gpu);
    OwnedSurface source;source.Allocate(sw,sh,bpp);
    if(bpp<=8) {source.s.palette.resize(1u<<bpp);for(size_t i=0;i<source.s.palette.size();++i)source.s.palette[i]=uint32_t(i*0x030507);}
    if(bpp!=32) {source.s.bits+=source.s.pitch*(sh-1);source.s.pitch=-source.s.pitch;}
    for(int y=0;y<sh;++y)for(int x=0;x<sw;++x)source.s.Set(x,y,(x*17113u+y*97231u)&0xFFFFFF);
    assert(p.Submit(source.s));Compare(gpu,p,source.s,w,h,{});
    const auto upload=gpu.UploadedBytes();
    for(int n=0;n<16;++n)Compare(gpu,p,source.s,w,h,{n*17%sw,n*7%sh,true});
    assert(gpu.UploadedBytes()==upload); // cursor motion never reuploads desktop
    for(int n=0;n<12;++n) {
        source.s.Set(n*13%sw,n*17%sh,0xFF1010+n);
        if(bpp<=8 && n==4)source.s.palette[0]^=0xFFFFFF;
        assert(p.Submit(source.s));Compare(gpu,p,source.s,w,h,{n*7%sw,n*13%sh,n%3!=0});
    }
    assert(p.Submit(source.s));const auto bytes=gpu.UploadedBytes();Compare(gpu,p,source.s,w,h,{});
    assert(gpu.UploadedBytes()==bytes);
    // A generation reset rebuilds a texture without losing the retained source.
    p.Attach(&gpu);Compare(gpu,p,source.s,w,h,{});
    // A real GL error must reject the frame, then permit a complete retry.
    glEnable(0xFFFFFFFFu);
    assert(!p.Render({nullptr,w,h,0,0}));
    Compare(gpu,p,source.s,w,h,{});
    // A backend rebind is also the software fallback path: retain exact pixels.
    SoftwarePresentation fallback;p.Attach(&fallback);
    std::vector<uint32_t> cpuOutput(size_t(w)*h),reference(cpuOutput.size());
    assert(p.Render({cpuOutput.data(),w,h,size_t(w),cpuOutput.size()}));
    SoftwarePresentation expected;
    assert(expected.Render({source.s,FitViewport(sw,sh,w,h),{},1},
        {reference.data(),w,h,size_t(w),reference.size()}));
    assert(cpuOutput==reference);p.Attach(&gpu);Compare(gpu,p,source.s,w,h,{});
    p.Detach();assert(!p.FrameBytes() && !gpu.CacheBytes());gpu.Shutdown();
    assert(eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT));
    assert(eglDestroySurface(display,surface));
}
int main() {
    EGLDisplay display=eglGetDisplay(EGL_DEFAULT_DISPLAY);assert(display!=EGL_NO_DISPLAY);
    assert(eglInitialize(display,nullptr,nullptr) && eglBindAPI(EGL_OPENGL_API));
    EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_BIT,
        EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
    EGLConfig config;EGLint count=0;assert(eglChooseConfig(display,attrs,&config,1,&count) && count==1);
    EGLint ctxAttrs[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,3,
        EGL_CONTEXT_OPENGL_PROFILE_MASK,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,EGL_NONE};
    EGLContext context=eglCreateContext(display,config,EGL_NO_CONTEXT,ctxAttrs);assert(context!=EGL_NO_CONTEXT);
    for(int bpp:{1,4,8,16,24,32})Scenario(display,config,context,67,45,131,97,bpp);
    Scenario(display,config,context,800,600,1280,720,32);
    Scenario(display,config,context,1280,720,1280,720,32);
    Scenario(display,config,context,37,9,131,97,32);
    Scenario(display,config,context,9,37,131,97,32);
    assert(eglDestroyContext(display,context) && eglTerminate(display));
    puts("PASSED: actual OpenGL/EGL pixels match software, Lite/native/scaled/letterbox, formats/palettes/negative pitch, dirty uploads, independent cursor, reset and cleanup");
}
