#define GL_GLEXT_PROTOTYPES 1
#include <EGL/egl.h>
#include <GL/glcorearb.h>
#include "source/ui/gpu_gdi_targets.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <vector>
using Targets=ui::GpuGdiTargets;
struct Allocator:ui::PresentationAllocator {
    size_t calls=0,failAt=0,live=0;
    void* Allocate(size_t n) override {if(++calls==failAt)return nullptr;auto* p=std::malloc(n);if(p)++live;return p;}
    void Free(void* p) override {if(p){assert(live);--live;std::free(p);}}
};
struct Image {
    int w,h;std::vector<uint32_t> pixels;
    Image(int width,int height):w(width),h(height),pixels(size_t(w)*h) {}
    void Fill(uint32_t c){std::fill(pixels.begin(),pixels.end(),c);}
};
static std::vector<uint32_t> Read(int w,int h) {
    glBindFramebuffer(GL_READ_FRAMEBUFFER,0);glPixelStorei(GL_PACK_ALIGNMENT,4);glPixelStorei(GL_PACK_ROW_LENGTH,0);
    glPixelStorei(GL_PACK_SKIP_ROWS,0);glPixelStorei(GL_PACK_SKIP_PIXELS,0);
    std::vector<uint32_t> b(size_t(w)*h),rgb(b.size());glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,b.data());assert(glGetError()==GL_NO_ERROR);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){auto p=b[size_t(h-1-y)*w+x];rgb[size_t(y)*w+x]=((p&255)<<16)|(p&0xff00)|((p>>16)&255);}
    return rgb;
}
static void Verify(Targets& t,const std::array<Targets::Layer,4>& layers,const std::array<Image,4>& images,int w,int h,bool cpu=false) {
    std::vector<uint32_t> actual;
    if(cpu) {
        const auto* s=t.ComposeCpu(layers.data(),layers.size(),w,h);assert(s);actual.resize(size_t(w)*h);
        for(int y=0;y<h;++y)for(int x=0;x<w;++x)actual[size_t(y)*w+x]=s->Get(x,y);
    } else {assert(t.Present(layers.data(),layers.size(),w,h));actual=Read(w,h);}
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        uint32_t expected=0;
        for(size_t i=0;i<layers.size();++i)if(layers[i].bounds.Contains(x,y)) {
            const auto& r=layers[i].bounds;const auto& s=images[i];
            const int sx=int(int64_t(x-r.left)*s.w/r.Width()),sy=int(int64_t(y-r.top)*s.h/r.Height());
            expected=s.pixels[size_t(sy)*s.w+sx]&0xffffff;
        }
        if(actual[size_t(y)*w+x]!=expected){std::fprintf(stderr,"targets mismatch %dx%d (%d,%d) %06x != %06x\n",w,h,x,y,actual[size_t(y)*w+x],expected);assert(false);}
    }
}
static EGLContext Context(EGLDisplay d,EGLConfig c) {
    EGLint a[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,3,EGL_CONTEXT_OPENGL_PROFILE_MASK,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,EGL_NONE};
    auto x=eglCreateContext(d,c,EGL_NO_CONTEXT,a);assert(x!=EGL_NO_CONTEXT);return x;
}
int main() {
    auto display=eglGetDisplay(EGL_DEFAULT_DISPLAY);assert(display!=EGL_NO_DISPLAY && eglInitialize(display,nullptr,nullptr) && eglBindAPI(EGL_OPENGL_API));
    const EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
    EGLConfig config;EGLint n;assert(eglChooseConfig(display,attrs,&config,1,&n) && n==1);
    const EGLint size[]={EGL_WIDTH,1280,EGL_HEIGHT,720,EGL_NONE};auto pbuffer=eglCreatePbufferSurface(display,config,size);assert(pbuffer!=EGL_NO_SURFACE);
    auto context=Context(display,config);assert(eglMakeCurrent(display,pbuffer,pbuffer,context));
    Allocator allocator;
    {
        Targets t(&allocator);assert(t.Initialize());
        assert(t.Present(nullptr,0,131,97));assert(Read(131,97)==std::vector<uint32_t>(131*97));
        assert(!t.Create(0,1) && !t.Create(513,1));
        for(size_t offset:{1u,2u}){allocator.failAt=allocator.calls+offset;assert(!t.Create(31,23) && t.Pixels()==0 && !allocator.live);allocator.failAt=0;}
        std::array<Image,4> images={Image(31,23),Image(17,19),Image(8,8),Image(1,1)};
        std::array<Targets::Layer,4> layers;
        for(size_t i=0;i<images.size();++i) {
            auto& s=images[i];layers[i].target=t.Create(s.w,s.h);assert(layers[i].target);
            for(int y=0;y<s.h;++y)for(int x=0;x<s.w;++x)s.pixels[size_t(y)*s.w+x]=(uint32_t(x*17119+y*5971+i*31741)&0xffffff)|0xa7000000;
            assert(t.Copy(layers[i].target,{0,0,s.w,s.h},s.pixels.data(),s.pixels.size()));
        }
        assert(!t.Create(1,1));
        for(int frame=0;frame<12;++frame) {
            layers[0].bounds={-3+frame,4,90+frame,73};layers[1].bounds={20,17+frame,88,93+frame};
            layers[2].bounds={55,29,79+frame,53+frame};layers[3].bounds={5,61,19,75};
            Verify(t,layers,images,131,97);
            for(const auto& layer:layers){const auto* s=t.TargetStats(layer.target);assert(s && s->readbacks==0 && s->uploads==1);}
        }
        layers[0].bounds={0,0,1280,720};layers[1].bounds={600,300,800,500};layers[2].bounds={-4,-4,12,12};layers[3].bounds={1270,710,1290,730};
        Verify(t,layers,images,1280,720);
        const auto frame=Read(1280,720);
        auto invalid=layers;invalid[2].target=0;assert(!t.Present(invalid.data(),4,1280,720));assert(Read(1280,720)==frame);
        invalid=layers;invalid[1].bounds.right=0x7fffffff;assert(!t.Present(invalid.data(),4,1280,720));assert(Read(1280,720)==frame);
        assert(!t.Present(nullptr,1,1280,720) && !t.Present(layers.data(),5,1280,720));
        Verify(t,layers,images,1280,720,true); // explicit read barrier; previously no readbacks
        for(const auto& layer:layers)assert(t.TargetStats(layer.target)->readbacks>0);
        const auto staleLayers=layers;const auto stale=layers[2].target;
        assert(t.Present(layers.data(),4,1280,720)); // retire a texture without an explicit GPU completion wait
        assert(t.Destroy(stale) && !t.Destroy(stale));assert(Read(1280,720)==frame);
        layers[2].target=t.Create(8,8);assert(layers[2].target && layers[2].target!=stale);
        assert(!t.Present(staleLayers.data(),4,1280,720) && !t.ComposeCpu(staleLayers.data(),4,1280,720));assert(Read(1280,720)==frame);
        assert(!t.Fill(stale,{0,0,1,1},0) && !t.CpuRead(stale) && !t.TargetStats(stale));
        assert(t.Copy(layers[2].target,{0,0,8,8},images[2].pixels.data(),64));Verify(t,layers,images,1280,720);
        {Targets other;assert(other.Initialize());const auto foreign=other.Create(8,8);assert(foreign && foreign!=layers[2].target);
         assert(!other.Fill(layers[2].target,{0,0,1,1},0));assert(!t.Fill(foreign,{0,0,1,1},0));
         other.Shutdown();assert(other.Initialize());assert(other.Create(8,8)!=foreign && !other.CpuRead(foreign));}
        // Explicit cross-target read barrier, then owned destination input.
        assert(t.CopyTargetViaCpu(layers[2].target,{0,0,8,8},layers[0].target,{2,3,10,11}));
        for(int y=0;y<8;++y)for(int x=0;x<8;++x)images[2].pixels[size_t(y)*8+x]=images[0].pixels[size_t(y+3)*31+x+2];
        assert(t.Fill(layers[0].target,{0,0,31,23},0x719253));images[0].Fill(0x719253);Verify(t,layers,images,1280,720);
        const auto overlapSource=images[2].pixels;
        assert(t.CopyTargetViaCpu(layers[2].target,{1,1,7,7},layers[2].target,{0,0,6,6}));
        for(int y=0;y<6;++y)for(int x=0;x<6;++x)images[2].pixels[size_t(y+1)*8+x+1]=overlapSource[size_t(y)*8+x];
        Verify(t,layers,images,1280,720);
        allocator.failAt=allocator.calls+1;uint32_t pixel=0;assert(!t.Copy(layers[2].target,{0,0,1,1},&pixel,1));allocator.failAt=0;
        Verify(t,layers,images,1280,720);
        const auto reads=t.TargetStats(layers[0].target)->readbacks;
        allocator.failAt=allocator.calls+1;assert(!t.ComposeCpu(layers.data(),4,640,360));allocator.failAt=0;
        assert(t.GpuActive() && t.TargetStats(layers[0].target)->readbacks==reads);
        assert(t.Fill(layers[1].target,{0,0,17,19},0x114477));images[1].Fill(0x114477);
        glEnable(0xffffffffu);assert(!t.Present(layers.data(),4,1280,720));assert(!t.GpuActive());Verify(t,layers,images,1280,720,true);
        assert(!t.Create(1,1));assert(t.Fill(layers[3].target,{0,0,1,1},0xfedcba));images[3].Fill(0xfedcba);Verify(t,layers,images,1280,720,true);
    }
    assert(!allocator.live);
    {
        Targets t(&allocator);assert(t.Initialize());auto a=t.Create(8,8),b=t.Create(8,8);assert(a && b);
        assert(t.Fill(a,{0,0,8,8},0x123456) && t.Fill(b,{0,0,8,8},0xabcdef));
        const Targets::Layer layers[]={{a,{0,0,8,8}},{b,{8,0,16,8}}};assert(t.Present(layers,2,64,32));
        glEnable(0xffffffffu);assert(t.CpuRead(a));assert(!t.GpuActive());
        assert(t.CpuRead(a)->Get(3,3)==0x123456 && t.CpuRead(b)->Get(3,3)==0xabcdef);
    }
    assert(!allocator.live);
    {
        Targets t(&allocator);assert(t.Initialize());auto a=t.Create(8,8);assert(a && t.Fill(a,{0,0,8,8},0x719253));
        const Targets::Layer layer{a,{0,0,8,8}};assert(t.Present(&layer,1,64,32));
        glEnable(0xffffffffu);assert(!t.Present(&layer,1,64,32));assert(!t.GpuActive()); // presenter failure, no target work pending
        assert(t.ComposeCpu(&layer,1,64,32)->Get(3,3)==0x719253);
    }
    assert(!allocator.live);
    {
        Targets t(&allocator);assert(t.Initialize());auto a=t.Create(512,256),b=t.Create(512,256);assert(a && b && t.Pixels()==Targets::MaxPixels);
        assert(!t.Create(1,1));assert(t.Destroy(a));assert(t.Create(1,1));
    }
    assert(!allocator.live);
    {
        Targets t(&allocator);assert(t.Initialize());std::array<Image,4> images={Image(8,8),Image(8,8),Image(8,8),Image(8,8)};
        std::array<Targets::Layer,4> layers;
        for(size_t i=0;i<4;++i){layers[i]={t.Create(8,8),{int(i)*8,0,int(i+1)*8,8}};assert(layers[i].target);assert(t.Fill(layers[i].target,{0,0,8,8},uint32_t(i*0x174311)));images[i].Fill(uint32_t(i*0x174311));}
        Verify(t,layers,images,64,32);
        assert(t.Fill(layers[1].target,{0,0,8,8},0x123456));images[1].Fill(0x123456); // pending after prior GPU presentation
        assert(eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT));assert(eglDestroyContext(display,context));
        t.ContextLost();assert(!t.GpuActive());Verify(t,layers,images,64,32,true); // no driver calls after destruction
        assert(!t.Create(1,1) && !t.Present(layers.data(),4,64,32));
        assert(t.Fill(layers[3].target,{0,0,8,8},0xabcdef));images[3].Fill(0xabcdef);Verify(t,layers,images,64,32,true);
    }
    assert(!allocator.live && eglDestroySurface(display,pbuffer) && eglTerminate(display));
    puts("GPU GDI targets: PASS (generation/budgets, GPU composition without readback, CPU barriers, stale/foreign IDs, fallback and context loss)");
}
