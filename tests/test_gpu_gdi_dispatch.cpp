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
static uint32_t Tile(unsigned i){return (0x203040u+i*0x12345u)&0xffffff;}
using Clock=std::chrono::steady_clock;
static double Us(Clock::time_point a){return std::chrono::duration<double,std::micro>(Clock::now()-a).count();}
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
using Oracle=std::array<std::vector<uint32_t>,7>;
static void Run(const char* path,bool gpu,Oracle& oracle,unsigned sample,int mode=0) {
    Egl egl;rt::GuestSession session;user::EnableUi(1280,720);assert(user::EnableWindowLayers(true));
    Allocator allocator;Bindings bindings(&allocator);assert(bindings.Initialize() && bindings.EnableCoherence());
    ui::OpenGlPresentation renderer;assert(renderer.Initialize());ui::MixedPresentation frame;
    auto& p=session.ProcessObject();auto& memory=session.Memory();std::string error;
    assert(p.LoadMainExe(path,error) && p.PrepareMainThread(p.MainThread().cpu,error));
    const auto probe=p.GetProcAddress(p.MainModule(),"DispatchProbe");assert(probe);
    Bindings::Lease lease,stale;unsigned checked=0;bool stopped=false;double raster=0,submit=0,output=0,checkpoint=0;
    auto begin=Clock::now(),phase=begin;
    {
        rt::GuestExecution execution(p,[&](rt::GuestThread& t) {
            win32::HostCallOutcome call;if(win32::TryHandleHostCall(t.cpu,memory,call))return !call.requestExit;
            cpu::Interpreter cpu(memory,t.teb,t.teb);const auto result=cpu.Step(t.cpu);
            assert(result==cpu::StepResult::Continue || result==cpu::StepResult::Halted);return true;
        },rt::GuestExecution::Startup::InitializeModules);
        for(unsigned steps=0;steps<700000;++steps) {
            const auto result=execution.RunOne();assert(result!=rt::GuestExecution::Result::AllocationFailed);
            const auto stage=memory.Read32(probe);
            if(stage!=checked && stage>=1 && stage<=6) {
                assert(stage==checked+1);checked=stage;const auto dc=memory.Read32(probe+8);
                if(stage==1) {
                    if(gpu)assert(bindings.EnableSolidDispatch());
                    assert(!bindings.DispatchCounters().accepted);
                    if(mode==1){assert(bindings.Acquire(dc,lease));allocator.failAt=allocator.calls+1;}
                    phase=Clock::now();
                } else if(stage==2) {
                    raster=Us(phase);
                    if(gpu) {
                        assert(bindings.DispatchCounters().accepted==uint64_t(mode==1?63:64) && !bindings.DispatchCounters().failed);
                        assert(bindings.Acquire(dc,lease));stale=lease;
                        const auto* c=bindings.Counters(lease);assert(c && c->uploads==1 && c->cpuImports==1 && !c->readbacks);
                        // Canonical CPU bytes must still be the old seed, proving
                        // there was no CPU raster plus redundant GPU work.
                        for(int y=50;y<178;++y)for(int x=40;x<168;++x)assert(user::S().screen.s.Get(x,y)==(mode==1 && x<56 && y<66?Tile(0):0x112233));
                    } else {
                        oracle[stage]=Screen();
                        for(int y=0;y<128;++y)for(int x=0;x<128;++x)assert(oracle[stage][size_t(y+50)*1280+x+40]==Tile(unsigned(y/16*8+x/16)));
                    }
                } else if(stage==3) {
                    checkpoint=Us(phase);
                    if(gpu) {
                        const auto* c=bindings.Counters(lease);assert(c && c->readbacks==uint64_t(mode==2?0:1));
                        assert(bindings.CoherenceCounters().commits==1 && bindings.CoherenceCounters().commitPixels==uint64_t(mode==1?63*256:128*128));
                    }
                } else if(stage==4 && gpu) {
                    assert(bindings.DispatchCounters().refused>=4); // child, side, combined pixels and image slots
                    assert(mode==2?bindings.DispatchCounters().accepted==64:bindings.DispatchCounters().accepted>64);
                } else if(stage==5 && gpu)assert(!bindings.Valid(stale));
                if(stage>=3) {
                    assert(ui::g_gdi.PrepareCpuAccess());
                    if(!gpu)oracle[stage]=Screen();else assert(Screen()==oracle[stage]);
                }
                if(stage>=2 && stage<=5) {
                    const auto before=bindings.CoherenceCounters();const auto t=Clock::now();assert(bindings.SubmitMixed(frame));
                    const auto submission=Us(t);const auto draw=Clock::now();
                    if(mode==2 && stage>2) {
                        ui::SoftwarePresentation cpu;std::vector<uint32_t> rgba(1280*720);
                        assert(bindings.RenderMixedCpu(frame,cpu,{rgba.data(),1280,720,1280,rgba.size()}));
                        for(size_t n=0;n<rgba.size();++n)assert(rgba[n]==(0xff000000u|ui::RgbToColorref(oracle[stage][n])));
                    } else assert(bindings.RenderMixed(frame,renderer,{nullptr,1280,720,0,0}));
                    const auto drawing=Us(draw);
                    if(stage==2){submit=submission;output=drawing;}
                    const auto after=bindings.CoherenceCounters();assert(before.commits==after.commits && before.cpuBarriers==after.cpuBarriers);
                    if(stage==2 && gpu) {
                        const auto* c=bindings.Counters(lease);assert(c && c->batches==1 && c->batchedCommands==uint64_t(mode==1?63:64) && c->draws==2 && !c->readbacks);
                    }
                    if(!(mode==2 && stage>2))CompareGl(oracle[stage]);
                    if(mode==2 && stage==2){renderer.Shutdown();egl.Lose();bindings.ContextLost();}
                    if(stage==2)phase=Clock::now(); // oracle FBO read is explicitly excluded
                }
            }
            if(result==rt::GuestExecution::Result::Stopped){stopped=true;break;}
            if(result==rt::GuestExecution::Result::Idle)rt::Clock::Sleep(1);
        }
    }
    assert(stopped && checked==6 && p.MainThread().exitCode==0 && !ui::g_gdi.CpuAccessFault() && !user::S().presentationFault);
    const auto stats=bindings.DispatchCounters();frame.Clear();assert(bindings.DisableCoherence());bindings.Shutdown();renderer.Shutdown();assert(!allocator.live);
    if(mode){printf("CASE %s PASS\n",mode==1?"pre-draw seed allocation refusal":"actual context destruction then ordinary CPU GDI recovery");return;}
    printf("SAMPLE %u %s %.3f %.3f %.3f %.3f %llu %llu\n",sample,gpu?"gpu":"cpu",raster,submit,output,checkpoint,
        static_cast<unsigned long long>(stats.accepted),static_cast<unsigned long long>(stats.refused));
}
int main(int argc,char** argv) {
    assert(argc==2);ui::FontCache::SetDirectory("romfs/fonts");rt::Clock::SetFastForward(true);
    Oracle oracle;
    // First reference also establishes exact later-phase CPU pixels. Remaining
    // repetitions rotate order, using identical PE and same-build pipelines.
    Run(argv[1],false,oracle,0);Run(argv[1],true,oracle,0);
    for(unsigned n=1;n<=6;++n){if(n&1){Run(argv[1],true,oracle,n);Run(argv[1],false,oracle,n);}else {Run(argv[1],false,oracle,n);Run(argv[1],true,oracle,n);}}
    Run(argv[1],true,oracle,0,1);Run(argv[1],true,oracle,0,2);
    puts("Ordinary x86 GDI dispatch: PASS (16 normal-exit sessions, CPU/EGL pixels, 64 common calls, one seed/one batch/no presentation readback, real reads/ROP/DIB/mapping/fallback/layout/lifetime)");
}
