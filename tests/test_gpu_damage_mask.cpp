#define GL_GLEXT_PROTOTYPES 1
#include <EGL/egl.h>
#include <GL/glcorearb.h>
#include "source/runtime/guest_session.h"
#include "source/runtime/guest_execution.h"
#include "source/runtime/clock.h"
#include "source/cpu/interpreter.h"
#include "source/win32/window_gpu_bindings.h"
#include "source/win32/user32.h"
#include "source/win32/mixed_workspace.h"
#include "source/win32/mixed_output.h"
#include <filesystem>
#include <unistd.h>
#include "source/ui/opengl_presentation.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include <chrono>
namespace user=win32::user;
using Bindings=user::WindowGpuBindings;
struct Allocator:ui::PresentationAllocator {
    size_t calls=0,failAt=0,live=0;
    void* Allocate(size_t n) override {if(++calls==failAt)return nullptr;auto* p=std::malloc(n);if(p)++live;return p;}
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
static std::vector<uint32_t> Screen() {
    const auto& s=user::S().screen.s;std::vector<uint32_t> out(size_t(s.width)*s.height);
    for(int y=0;y<s.height;++y)for(int x=0;x<s.width;++x)out[size_t(y)*s.width+x]=s.Get(x,y);
    return out;
}
static void CompareGl(const std::vector<uint32_t>& expected) {
    std::vector<uint32_t> rgba(1280*720);glBindFramebuffer(GL_READ_FRAMEBUFFER,0);glBindBuffer(GL_PIXEL_PACK_BUFFER,0);
    glPixelStorei(GL_PACK_ALIGNMENT,4);glPixelStorei(GL_PACK_ROW_LENGTH,0);glPixelStorei(GL_PACK_SKIP_ROWS,0);glPixelStorei(GL_PACK_SKIP_PIXELS,0);
    glReadPixels(0,0,1280,720,GL_RGBA,GL_UNSIGNED_BYTE,rgba.data());assert(glGetError()==GL_NO_ERROR);
    for(int y=0;y<720;++y)for(int x=0;x<1280;++x)if(rgba[size_t(719-y)*1280+x]!=(0xff000000u|ui::RgbToColorref(expected[size_t(y)*1280+x]))) {
        fprintf(stderr,"dispatch GL mismatch %d,%d actual=%08x expected=%06x\n",x,y,rgba[size_t(719-y)*1280+x],expected[size_t(y)*1280+x]);assert(false);
    }
}
using Oracle=std::vector<uint32_t>;
static void Run(const char* path,int mode,Oracle& expected) {
    Egl egl;rt::GuestSession session;user::EnableUi(1280,720);assert(user::EnableWindowLayers(true));
    struct FailAllocator:ui::PresentationAllocator {
        size_t live=0;bool refuse=false;
        void* Allocate(size_t n)override{if(refuse && n==135)return nullptr;void* p=std::malloc(n);if(p)++live;return p;}
        void Free(void* p)override{if(p){assert(live);--live;std::free(p);}}
    } allocator;
    Bindings bindings(&allocator);assert(bindings.Initialize() && bindings.EnableCoherence());
    ui::OpenGlPresentation renderer;assert(renderer.Initialize());ui::MixedPresentation frame;
    auto& process=session.ProcessObject();auto& memory=session.Memory();std::string error;
    assert(process.LoadMainExe(path,error) && process.PrepareMainThread(process.MainThread().cpu,error));
    const auto probe=process.GetProcAddress(process.MainModule(),"DamageProbe");assert(probe);
    Oracle before;std::vector<uint32_t> ownerBefore;Bindings::Lease lease;unsigned checked=0;bool stopped=false,aborted=false;
    {
        rt::GuestExecution execution(process,[&](rt::GuestThread& t){
            win32::HostCallOutcome call;if(win32::TryHandleHostCall(t.cpu,memory,call))return !call.requestExit;
            cpu::Interpreter cpu(memory,t.teb,t.teb);const auto result=cpu.Step(t.cpu);
            assert(result==cpu::StepResult::Continue || result==cpu::StepResult::Halted);return true;
        },rt::GuestExecution::Startup::InitializeModules);
        for(unsigned steps=0;steps<2000000;++steps){
            const auto result=execution.RunOne();assert(result!=rt::GuestExecution::Result::AllocationFailed);
            const auto stage=memory.Read32(probe);
            if(stage!=checked && stage>=1 && stage<=4){
                assert(stage==checked+1);checked=stage;
                if(stage==1){
                    before=Screen();const auto scene=user::S().windowLayers.Scene();assert(scene.owners);ownerBefore.assign(scene.owners,scene.owners+1280*720);
                    if(mode){assert(bindings.EnableSolidDispatch());allocator.refuse=mode==3;}
                }else if(stage==2){
                    if(!mode)expected=Screen();
                    else {
                        assert(bindings.Acquire(memory.Read32(probe+8),lease));
                        size_t changed=0;for(size_t n=0;n<before.size();++n)if(before[n]!=expected[n])++changed;assert(changed>128);
                        if(mode!=3){assert(bindings.DamageMaskBytes()==135 && !bindings.CoherenceCounters().commits && Screen()==before);}
                        else assert(!bindings.DamageMaskBytes() && bindings.CoherenceCounters().maskFallbacks>0);
                        if(mode==4){
                            const auto canonical=Screen();ui::g_gdi.InvalidateLayout();assert(!bindings.CheckpointCpu() && !bindings.CoherenceHealthy() && user::S().presentationFault && Screen()==canonical);
                            aborted=true;break;
                        }
                        assert(bindings.SubmitMixed(frame));assert(bindings.RenderMixed(frame,renderer,{nullptr,1280,720,0,0}));CompareGl(expected);
                        if(mode==2){renderer.Shutdown();egl.Lose();bindings.ContextLost();ui::SoftwarePresentation cpu;std::vector<uint32_t> rgba(1280*720);assert(bindings.RenderMixedCpu(frame,cpu,{rgba.data(),1280,720,1280,rgba.size()}));for(size_t n=0;n<rgba.size();++n)assert(rgba[n]==(0xff000000u|ui::RgbToColorref(expected[n])));}
                        assert(bindings.CheckpointCpu() && Screen()==expected);
                        const auto stats=bindings.CoherenceCounters();assert(stats.commitPixels==changed);
                        if(mode!=3)assert(stats.maskCommits==1 && stats.maskPixels==changed);
                        else assert(!stats.maskCommits);
                        const auto scene=user::S().windowLayers.Scene();
                        for(size_t n=0;n<before.size();++n)if(before[n]==expected[n])assert(scene.owners[n]==ownerBefore[n]);
                        if(mode==1){
                            // Policy switches/repeated checkpoints retain only current accepted writes.
                            assert(bindings.EnableExactDamage(false));
                            assert(bindings.Fill(lease,{0,0,1,1},0x112233) && bindings.CheckpointCpu());
                            assert(bindings.EnableExactDamage(true));
                            for(int cycle=0;cycle<2;++cycle) {
                                const auto prior=bindings.CoherenceCounters().commitPixels;size_t written=0;
                                const auto& r=lease.raster;
                                for(int y=-2;y<29;y+=2)for(int x=-1+cycle;x<37;x+=2) {
                                    assert(bindings.Fill(lease,{x,y,x+1,y+1},0xabcdefu+unsigned(cycle)));
                                    const int sx=r.originX+r.viewportOrg.x-r.windowOrg.x+x,sy=r.originY+r.viewportOrg.y-r.windowOrg.y+y;
                                    if(r.clip.Contains(sx,sy))++written;
                                }
                                assert(written>128 && bindings.CheckpointCpu());
                                assert(bindings.CoherenceCounters().commitPixels==prior+written); // old bits were cleared
                            }
                            // Promote again, then mark full row-byte spans in an
                            // odd-width image; commit the exact clip, not its box.
                            const auto canonical=Screen();const auto prior=bindings.CoherenceCounters().commitPixels;
                            const auto& raster=lease.raster;
                            for(int y=-2;y<29;y+=2)for(int x=-1;x<37;x+=2)assert(bindings.Fill(lease,{x,y,x+1,y+1},0x135790));
                            assert(bindings.Fill(lease,{-5,-5,50,50},0xf00d42));
                            size_t clipPixels=0;auto fullExpected=canonical;
                            for(const auto& clip:raster.clip.Rects())for(int y=clip.top;y<clip.bottom;++y)for(int x=clip.left;x<clip.right;++x){fullExpected[size_t(y)*1280+x]=0xf00d42;++clipPixels;}
                            assert(bindings.CheckpointCpu() && Screen()==fullExpected && bindings.CoherenceCounters().commitPixels==prior+clipPixels);
                            {rt::GuestSession inner;user::EnableUi(1280,720);assert(!bindings.EnableExactDamage(true));}
                            assert(bindings.CoherenceHealthy());
                        }
                    }
                }
            }
            if(result==rt::GuestExecution::Result::Stopped){stopped=true;break;}
            if(result==rt::GuestExecution::Result::Idle)rt::Clock::Sleep(1);
        }
    }
    assert((aborted && mode==4) || (stopped && checked==4 && process.MainThread().exitCode==0 && !user::S().presentationFault));
    frame.Clear();bindings.Shutdown();renderer.Shutdown();assert(!allocator.live && !bindings.DamageMaskBytes() && bindings.CoherenceCounters().maskPeakBytes<=Bindings::MaxDamageBytes);
    printf("Exact damage case %d PASS\n",mode);
}
int main(int argc,char** argv){assert(argc==2);ui::FontCache::SetDirectory("romfs/fonts");rt::Clock::SetFastForward(true);Oracle expected;for(int mode=0;mode<5;++mode)Run(argv[1],mode,expected);puts("Exact damage x86 PASS (4 normal exits + lost-authority abort, odd width/clip/child/occlusion/capture, allocation fallback, bit reset, real context loss)");}
