#define GL_GLEXT_PROTOTYPES 1
#include <EGL/egl.h>
#include <GL/glcorearb.h>
#include "source/ui/opengl_presentation.h"
#include <cassert>
#include <cstdio>
#include <vector>
using namespace ui;
struct CacheAllocator:PresentationAllocator {
    bool fail=false;
    void* Allocate(size_t n) override {return fail?nullptr:std::malloc(n);}
    void Free(void* p) override {std::free(p);}
};
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
    CacheAllocator cache;OpenGlPresentation gpu(&cache);assert(gpu.Initialize());Presentation p(&gpu);
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
    if(bpp==32 && sw==1280 && sh==720) {
        source.s.Set(63,63,0x13579B);assert(p.Submit(source.s));
        auto before=gpu.UploadedBytes();Compare(gpu,p,source.s,w,h,{});
        assert(gpu.UploadedBytes()-before==4096);
        auto calls=gpu.UploadCalls();
        for(int y=0;y<sh;y+=32)for(int x=0;x<sw;x+=32)source.s.Set(x,y,0xABCDEF);
        assert(p.Submit(source.s));before=gpu.UploadedBytes();Compare(gpu,p,source.s,w,h,{});
        assert(gpu.UploadedBytes()-before==size_t(sw)*sh*4 && gpu.UploadCalls()==calls+1);
    }
    // Optional texture metadata can fail without making the output unusable.
    gpu.Reset();cache.fail=true;
    Compare(gpu,p,source.s,w,h,{});assert(!gpu.CacheBytes());
    Compare(gpu,p,source.s,w,h,{0,0,true});assert(!gpu.CacheBytes());
    cache.fail=false;Compare(gpu,p,source.s,w,h,{});
    const auto cached=gpu.UploadedBytes();Compare(gpu,p,source.s,w,h,{1,1,true});
    assert(gpu.UploadedBytes()==cached);
    // Invalid direct backend views are rejected before pointer arithmetic/GL writes.
    Surface bad=source.s;bad.width=0x7FFFFFFF;bad.bpp=32;
    assert(!gpu.Render({bad,{0,0,w,h},{},1},{nullptr,w,h,0,0}));
    assert(!gpu.Render({source.s,{-1,0,w,h},{},1},{nullptr,w,h,0,0}));
    Compare(gpu,p,source.s,w,h,{});
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
static void IndependentLayers(EGLDisplay display,EGLConfig config,EGLContext context,int sw,int sh,int w,int h) {
    EGLint attrs[]={EGL_WIDTH,w,EGL_HEIGHT,h,EGL_NONE};
    auto surface=eglCreatePbufferSurface(display,config,attrs);assert(surface!=EGL_NO_SURFACE);
    assert(eglMakeCurrent(display,surface,surface,context));
    OpenGlPresentation gpu;assert(gpu.Initialize());
    OwnedSurface base,lower,upper,expected;base.Allocate(sw,sh);expected.Allocate(sw,sh);
    lower.Allocate(15,11);upper.Allocate(18,9);
    for(int y=0;y<11;++y)for(int x=0;x<15;++x)lower.s.Set(x,y,0xD31A48);
    for(int y=0;y<9;++y)for(int x=0;x<18;++x)upper.s.Set(x,y,0x1BCC73);
    std::vector<uint32_t> owners(size_t(sw)*sh),actual(size_t(w)*h),reference(actual.size());
    PresentationLayerFrame layers[]={{&lower.s,101,-3,4,1,1,nullptr},{&upper.s,202,9,7,1,1,nullptr}};
    PresentationSceneFrame scene{owners.data(),sw,sh,1,1,nullptr,layers,2};
    const auto viewport=FitViewport(sw,sh,w,h);SoftwarePresentation software;
    // The base image deliberately lacks ALL window pixels. A renderer which
    // ignores the independent textures or shows the flattened base must fail.
    for(int pass=0;pass<4;++pass) {
        for(int y=0;y<sh;++y)for(int x=0;x<sw;++x) {
            uint32_t token=0,rgb=0x182030;
            if(x<12 && y>=4 && y<15){token=101;rgb=0xD31A48;}
            if(x>=9 && x<27 && y>=7 && y<16 && (pass!=1 || token==0)){token=202;rgb=0x1BCC73;}
            owners[size_t(y)*sw+x]=token;base.s.Set(x,y,token?0xA010E0:rgb);expected.s.Set(x,y,rgb);
        }
        PresentationCursor cursor{11,8,pass!=0};
        PresentationFrame frame{base.s,viewport,cursor,1,nullptr,nullptr,42,1,&scene};
        assert(gpu.Render(frame,{nullptr,w,h,0,0}));
        assert(software.Render({expected.s,viewport,cursor,1},{reference.data(),w,h,size_t(w),reference.size()}));
        glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,actual.data());assert(glGetError()==GL_NO_ERROR);
        for(int y=0;y<h;++y)for(int x=0;x<w;++x)assert(actual[size_t(h-1-y)*w+x]==reference[size_t(y)*w+x]);
        assert(gpu.LayerDraws()>0);
        if(pass==1) {
            glEnable(0xFFFFFFFFu);assert(!gpu.Render(frame,{nullptr,w,h,0,0}));
            assert(gpu.Render(frame,{nullptr,w,h,0,0}));
        }
        if(pass==2)gpu.Reset();
    }
    // Geometry/token/budget validation happens before a texture reads the view.
    PresentationFrame frame{base.s,viewport,{},1,nullptr,nullptr,42,1,&scene};
    layers[0].token=0;assert(!gpu.Render(frame,{nullptr,w,h,0,0}));layers[0].token=101;
    layers[0].left=0x7FFFFFFF;assert(!gpu.Render(frame,{nullptr,w,h,0,0}));layers[0].left=-3;
    // Presentation, not the guest/window store, owns data for later redraw.
    WindowLayers raster;assert(raster.Enable(sw,sh));OwnedSurface canvas;canvas.Allocate(sw,sh);
    canvas.s.writeCapture=raster.ScreenCapture();
    for(int y=0;y<sh;++y)for(int x=0;x<sw;++x)canvas.s.Set(x,y,0x182030);
    Surface draw=canvas.s;draw.writeCapture=raster.Capture(9,2,3,10,9);
    for(int y=3;y<12;++y)for(int x=2;x<12;++x)draw.Set(x,y,0xAA7711);
    for(int y=0;y<sh;++y)for(int x=0;x<sw;++x)expected.s.Set(x,y,canvas.s.Get(x,y));
    Presentation retained(&gpu);assert(retained.Submit(canvas.s,raster.Scene()));
    raster.Clear();canvas.storage.clear();canvas.s={}; // all borrowed inputs gone
    for(int pass=0;pass<3;++pass) {
        PresentationCursor cursor{11,8,pass!=0};const auto before=gpu.UploadedBytes();
        assert(retained.Render({nullptr,w,h,0,0},cursor));
        if(pass)assert(gpu.UploadedBytes()==before);
        assert(software.Render({expected.s,viewport,cursor,1},{reference.data(),w,h,size_t(w),reference.size()}));
        glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,actual.data());assert(glGetError()==GL_NO_ERROR);
        for(int y=0;y<h;++y)for(int x=0;x<w;++x)assert(actual[size_t(h-1-y)*w+x]==reference[size_t(y)*w+x]);
    }
    glEnable(0xFFFFFFFFu);assert(!retained.Render({nullptr,w,h,0,0}));assert(retained.Render({nullptr,w,h,0,0}));
    retained.Detach();assert(!retained.LayerBytes());
    gpu.Shutdown();assert(!gpu.CacheBytes());
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
    IndependentLayers(display,config,context,37,29,131,97);
    IndependentLayers(display,config,context,800,600,1280,720);
    IndependentLayers(display,config,context,1280,720,1280,720);
    assert(eglDestroyContext(display,context) && eglTerminate(display));
    puts("PASSED: actual OpenGL/EGL pixels match software, Lite/native/scaled/letterbox, formats/palettes/negative pitch, dirty uploads, independent cursor, reset and cleanup");
    puts("PASSED: independent window textures replace poisoned base pixels, overlap ownership, negative origins, cursor, GL failure/retry and reset");
}
