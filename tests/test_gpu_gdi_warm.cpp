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
static uint32_t Tile(unsigned frame,unsigned i){return (0x203040u+i*0x12345u+frame*0x102019u)&0xffffff;}
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
static void Run(const char* path,int mode,unsigned sample) {
    Egl egl;rt::GuestSession session;user::EnableUi(1280,720);assert(user::EnableWindowLayers(true));
    Bindings bindings;assert(bindings.Initialize() && bindings.EnableCoherence());
    ui::OpenGlPresentation renderer;assert(renderer.Initialize());ui::MixedPresentation frame;
    auto& p=session.ProcessObject();auto& memory=session.Memory();std::string error;
    assert(p.LoadMainExe(path,error) && p.PrepareMainThread(p.MainThread().cpu,error));
    const auto probe=p.GetProcAddress(p.MainModule(),"WarmProbe");assert(probe);
    Bindings::Lease lease;unsigned checked=0;bool stopped=false;std::vector<uint32_t> expected;
    auto phase=Clock::now();double raster=0,submit=0,output=0;
    {
        rt::GuestExecution execution(p,[&](rt::GuestThread& t) {
            win32::HostCallOutcome call;if(win32::TryHandleHostCall(t.cpu,memory,call))return !call.requestExit;
            cpu::Interpreter cpu(memory,t.teb,t.teb);const auto result=cpu.Step(t.cpu);
            assert(result==cpu::StepResult::Continue || result==cpu::StepResult::Halted);return true;
        },rt::GuestExecution::Startup::InitializeModules);
        for(unsigned steps=0;steps<2000000;++steps) {
            const auto result=execution.RunOne();assert(result!=rt::GuestExecution::Result::AllocationFailed);
            const auto stage=memory.Read32(probe);
            if(stage!=checked && stage>=1 && stage<=12) {
                assert(stage==checked+1);checked=stage;
                if(stage==1) {
                    expected=Screen();
                    if(mode){assert(bindings.EnableSolidDispatch());bindings.EnableCoveredRetirement(mode==2);}
                    phase=Clock::now();
                } else if(stage<=10) {
                    raster=Us(phase);const unsigned index=stage-2;
                    for(int y=0;y<128;++y)for(int x=0;x<128;++x)expected[size_t(y+50)*1280+x+40]=Tile(index,unsigned(y/16*8+x/16));
                    if(!mode)assert(Screen()==expected);
                    else {
                        assert(bindings.Acquire(memory.Read32(probe+8),lease));const auto* c=bindings.Counters(lease);
                        assert(c && c->cpuImports==1 && c->uploads==1 && c->readbacks==(mode==2?0:index) && c->capacityCheckpoints==(mode==2?0:index) && c->coveredRetired==(mode==2?index*64:0));
                        assert(bindings.DispatchCounters().accepted==uint64_t(index+1)*64);
                        // No canonical raster or commit while targets stay live.
                        assert(!bindings.CoherenceCounters().commits);
                        for(int y=50;y<178;++y)for(int x=40;x<168;++x)assert(user::S().screen.s.Get(x,y)==0x112233);
                    }
                    const auto a=Clock::now();assert(bindings.SubmitMixed(frame));submit=Us(a);
                    const auto b=Clock::now();assert(bindings.RenderMixed(frame,renderer,{nullptr,1280,720,0,0}));output=Us(b);
                    CompareGl(expected);
                    const auto* c=bindings.Counters(lease);
                    if(mode)assert(c && c->batches==index+1 && c->draws==index+2 &&
                        c->batchedCommands==uint64_t(index+1)*64 && c->submittedCommands==uint64_t(index+1)*64+1);
                    printf("WARM %u %d %u %.3f %.3f %.3f %llu\n",sample,mode,index,raster,submit,output,c?static_cast<unsigned long long>(c->readbacks):0ULL);
                    phase=Clock::now();
                } else if(stage==11) {
                    const auto readUs=Us(phase);assert(Screen()==expected);
                    const auto* c=bindings.Counters(lease);
                    printf("WARM_READ %u %d %.3f %llu\n",sample,mode,readUs,c?static_cast<unsigned long long>(c->readbacks):0ULL);
                    if(mode){const auto* c=bindings.Counters(lease);assert(c && c->readbacks==uint64_t(mode==2?1:9));assert(bindings.CoherenceCounters().commits==1);}
                }
            }
            if(result==rt::GuestExecution::Result::Stopped){stopped=true;break;}
            if(result==rt::GuestExecution::Result::Idle)rt::Clock::Sleep(1);
        }
    }
    assert(stopped && checked==12 && p.MainThread().exitCode==0 && !ui::g_gdi.CpuAccessFault() && !user::S().presentationFault);
    frame.Clear();assert(bindings.DisableCoherence());bindings.Shutdown();renderer.Shutdown();
}
int main(int argc,char** argv) {
    assert(argc==2);ui::FontCache::SetDirectory("romfs/fonts");rt::Clock::SetFastForward(true);
    for(unsigned sample=0;sample<4;++sample)for(int n=0;n<3;++n)Run(argv[1],int((sample+n)%3),sample);
    puts("Warm ordinary GDI: PASS (persistent resources, independent CPU/EGL pixels, actual x86 reads and normal exits)");
}
