#define GL_GLEXT_PROTOTYPES 1
#include <EGL/egl.h>
#include <GL/glcorearb.h>
#include "source/runtime/guest_session.h"
#include "source/runtime/guest_execution.h"
#include "source/runtime/clock.h"
#include "source/cpu/interpreter.h"
#include "source/win32/window_gpu_bindings.h"
#include "source/win32/user32.h"
#include "source/ui/opengl_presentation.h"
#include <cassert>
#include <cstdio>
#include <vector>
namespace user=win32::user;
using Bindings=user::WindowGpuBindings;
struct Allocator:ui::PresentationAllocator {
    size_t calls=0,failAt=0,live=0;
    void* Allocate(size_t n) override {if(++calls==failAt)return nullptr;auto* p=std::malloc(n);if(p)++live;return p;}
    void Free(void* p) override {if(p){assert(live);--live;std::free(p);}}
};
static uint32_t Tile(unsigned i){return (0x203040u+i*0x12345u)&0xffffff;}
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
static void PrimitiveImport() {
    ui::GpuGdiPrototype image;assert(image.Initialize(31,23));std::vector<uint32_t> rgb(31*23),expected(rgb.size());
    for(size_t i=0;i<rgb.size();++i)expected[i]=(rgb[i]=0xa7000000u|((unsigned(i)*0x9179u)&0xffffff))&0xffffff;
    assert(image.ImportCpu(rgb.data(),rgb.size()));assert(image.Backend()==ui::GpuGdiPrototype::Mode::Gpu);
    assert(image.Counters().cpuImports==1 && image.Counters().uploads==1 && !image.Counters().readbacks && !image.JournalCommands());
    std::fill(rgb.begin(),rgb.end(),0);const auto* s=image.CpuRead();assert(s);
    for(int y=0;y<23;++y)for(int x=0;x<31;++x)assert(s->Get(x,y)==expected[size_t(y)*31+x]);
    assert(image.Fill({1,1,2,2},0x123456));expected[32]=0x123456;
    assert(!image.ImportCpu(rgb.data(),rgb.size()) && !image.ImportCpu(rgb.data(),0));
    s=image.CpuRead();for(int y=0;y<23;++y)for(int x=0;x<31;++x)assert(s->Get(x,y)==expected[size_t(y)*31+x]);
}
static void ExpiredGate() {
    struct Gate:ui::CpuRasterGate {bool Prepare()override{assert(false);return false;}};
    ui::Gdi g;auto gate=std::make_shared<Gate>();assert(g.InstallCpuGate(gate));gate.reset();assert(!g.PrepareCpuAccess());
}
static std::vector<uint32_t> Screen() {
    const auto& s=user::S().screen.s;std::vector<uint32_t> out(size_t(s.width)*s.height);
    for(int y=0;y<s.height;++y)for(int x=0;x<s.width;++x)out[size_t(y)*s.width+x]=s.Get(x,y);
    return out;
}
static void RenderExpected(const std::vector<uint32_t>& expected) {
    const auto& s=user::S().screen.s;assert(Screen()==expected);
    ui::OpenGlPresentation backend;assert(backend.Initialize());ui::Presentation presentation(&backend);
    assert(presentation.Submit(s,user::S().windowLayers.Scene()) && presentation.Render({nullptr,1280,720,0,0}));
    std::vector<uint32_t> rgba(1280*720);glBindFramebuffer(GL_READ_FRAMEBUFFER,0);glBindBuffer(GL_PIXEL_PACK_BUFFER,0);
    glPixelStorei(GL_PACK_ALIGNMENT,4);glPixelStorei(GL_PACK_ROW_LENGTH,0);glPixelStorei(GL_PACK_SKIP_ROWS,0);glPixelStorei(GL_PACK_SKIP_PIXELS,0);
    glReadPixels(0,0,1280,720,GL_RGBA,GL_UNSIGNED_BYTE,rgba.data());assert(glGetError()==GL_NO_ERROR);
    for(int y=0;y<720;++y)for(int x=0;x<1280;++x)assert(rgba[size_t(719-y)*1280+x]==(0xff000000u|ui::RgbToColorref(expected[size_t(y)*1280+x])));
    presentation.Detach();backend.Shutdown();
    GLint program=-1;glGetIntegerv(GL_CURRENT_PROGRAM,&program);assert(program==0 && glGetError()==GL_NO_ERROR);
}
static void Run(const char* path,bool loss) {
    Egl egl;PrimitiveImport();ExpiredGate();
    rt::GuestSession session;user::EnableUi(1280,720);assert(user::EnableWindowLayers(true));
    Allocator allocator;Bindings bindings(&allocator);assert(bindings.Initialize() && bindings.EnableCoherence());
    auto& p=session.ProcessObject();auto& memory=session.Memory();std::string error;
    assert(p.LoadMainExe(path,error) && p.PrepareMainThread(p.MainThread().cpu,error));
    const auto probe=p.GetProcAddress(p.MainModule(),"CoherenceProbe");assert(probe);
    Bindings::Lease lease,other,old;unsigned checked=0;bool stopped=false;std::vector<uint32_t> expected;
    {
        rt::GuestExecution execution(p,[&](rt::GuestThread& t) {
            win32::HostCallOutcome call;if(win32::TryHandleHostCall(t.cpu,memory,call))return !call.requestExit;
            cpu::Interpreter cpu(memory,t.teb,t.teb);const auto step=cpu.Step(t.cpu);
            assert(step==cpu::StepResult::Continue || step==cpu::StepResult::Halted);return true;
        },rt::GuestExecution::Startup::InitializeModules);
        for(unsigned i=0;i<700000;++i) {
            const auto result=execution.RunOne();assert(result!=rt::GuestExecution::Result::AllocationFailed);
            const auto stage=memory.Read32(probe);
            if(stage!=checked && stage>=1 && stage<=10) {
                assert(stage==checked+1);checked=stage;
                const auto hwnd=memory.Read32(probe+4),dc=memory.Read32(probe+8);
                const auto counters=bindings.CoherenceCounters();const auto* gpuCounts=bindings.Counters(lease);
                std::fprintf(stderr,"coherence stage=%u imports=%llu commits=%llu gpu=%d leaseReads=%lld\n",stage,
                    static_cast<unsigned long long>(counters.imports),static_cast<unsigned long long>(counters.commits),
                    int(bindings.GpuActive()),gpuCounts?static_cast<long long>(gpuCounts->readbacks):-1LL);

                if(stage==1) {
                    expected=Screen();assert(bindings.Acquire(dc,lease));assert(bindings.Acquire(memory.Read32(probe+16),other));
                    // Both the seed buffer and the owned upload snapshot may
                    // fail without accepting a GPU write or corrupting CPU data.
                    allocator.failAt=allocator.calls+1;assert(!bindings.Fill(lease,{0,0,1,1},0xffffff));
                    allocator.failAt=allocator.calls+2;assert(!bindings.Fill(lease,{0,0,1,1},0xffffff));
                    allocator.failAt=0;assert(Screen()==expected && !bindings.CoherenceCounters().imports);
                    assert(!bindings.Counters(lease)->draws && !bindings.Counters(lease)->uploads);
                    for(unsigned n=0;n<64;++n)assert(bindings.Fill(lease,{int(n%8)*16,int(n/8)*16,int(n%8+1)*16,int(n/8+1)*16},Tile(n)));
                    assert(bindings.Fill(other,{0,0,16,16},0x4162a3));
                    assert(Screen()==expected);const auto* counts=bindings.Counters(lease);assert(counts && !counts->readbacks && counts->uploads==1);
                    // The suspended outer gate moves with its Gdi store; an
                    // unrelated nested guest must not consume outer GPU work.
                    {rt::GuestSession inner;user::EnableUi(1280,720);const auto sdc=ui::g_gdi.CreateDc(ui::DcKind::Screen);
                     assert(ui::g_gdi.GetPixel(sdc,0,0)!=0xffffffffu);assert(ui::g_gdi.DeleteDc(sdc));}
                    assert(bindings.Valid(lease) && !bindings.Counters(lease)->readbacks && Screen()==expected);
                    for(int y=0;y<128;++y)for(int x=0;x<128;++x)expected[size_t(y+50)*1280+x+40]=Tile(unsigned(y/16*8+x/16));
                    for(int y=0;y<16;++y)for(int x=0;x<16;++x)expected[size_t(y+50)*1280+x+300]=0x4162a3;
                } else if(stage==2) {
                    const auto c=*bindings.Counters(lease);
                    assert(c.draws==2 && c.uploads==1 && c.readbacks==1 && c.batches==1 && c.batchedCommands==64 && c.cpuImports==1);
                    const auto stats=bindings.CoherenceCounters();assert(stats.imports==2 && stats.commits==2 && stats.commitPixels==128*128+16*16);
                    RenderExpected(expected);assert(bindings.Counters(lease)->readbacks==1);
                    assert(bindings.Fill(lease,{5,5,21,21},0x887766));
                } else if(stage==3) {
                    assert(bindings.CoherenceCounters().commits==3 && bindings.Counters(lease)->readbacks==2);
                    assert(user::S().screen.s.Get(46,56)==0x778899);
                    assert(bindings.Fill(lease,{30,30,40,40},0xaabbcc));
                } else if(stage==4) {
                    assert(bindings.CoherenceCounters().commits==4 && bindings.Counters(lease)->readbacks==3);
                    assert(bindings.Fill(lease,{64,64,65,65},0xdead00));
                    // An explicit test read proves CPU ROP/raw-DIB writes were
                    // imported into the whole GPU image, including untouched pixels.
                    const auto* image=bindings.CpuRead(lease);assert(image);
                    assert(image->Get(6,6)==0x778899 && image->Get(31,31)==0x123abc && image->Get(38,38)==0xaabbcc && image->Get(64,64)==0xdead00);
                } else if(stage==5) {
                    assert(bindings.CoherenceCounters().commits==5);
                    assert(bindings.Fill(lease,{1,1,3,3},0x22ee77));old=lease;
                } else if(stage==6) {
                    assert(bindings.CoherenceCounters().commits==6 && !bindings.Valid(old));
                    assert(bindings.Acquire(dc,lease) && lease.target==old.target);
                    assert(bindings.Fill(lease,{0,0,4,4},0x336699));old=lease;
                } else if(stage==7) {
                    assert(bindings.CoherenceCounters().commits==7 && !bindings.Valid(old));
                    assert(bindings.Acquire(dc,lease) && lease.target!=old.target);
                    assert(bindings.Fill(lease,{0,0,4,4},0x55aa00));old=lease;
                } else if(stage==8) {
                    assert(bindings.CoherenceCounters().commits==8 && !bindings.Valid(old));
                    assert(bindings.Acquire(dc,lease));assert(bindings.Fill(lease,{0,0,4,4},0xee6622));assert(bindings.GpuActive());
                    if(loss){egl.Lose();bindings.ContextLost();}else glEnable(0xdead);
                } else if(stage==9) {
                    assert(bindings.CoherenceCounters().commits==9 && !bindings.GpuActive() && bindings.CoherenceHealthy());
                    assert(bindings.Counters(lease)->recoveries>0);
                    assert(bindings.Fill(lease,{2,2,3,3},0x5566aa));
                } else if(stage==10) {
                    assert(bindings.CoherenceCounters().commits==10 && bindings.CoherenceHealthy());
                }
                assert(bindings.CoherenceHealthy());(void)hwnd;
            }
            if(result==rt::GuestExecution::Result::Stopped){assert(p.MainThread().exitCode==0);stopped=true;break;}
            if(result==rt::GuestExecution::Result::Idle)rt::Clock::Sleep(1);
        }
    }
    assert(stopped && checked==10);bindings.Prune();assert(!bindings.Pixels());
    assert(bindings.DisableCoherence() && ui::g_gdi.PrepareCpuAccess());bindings.Shutdown();assert(!allocator.live);
    std::printf("GPU coherence mode=%s: PASS (10 x86 stages, 2 dirty windows, 64 fills/1 import/1 readback for first window, ROP/raw-DIB/GetDIBits, layout, fallback)\n",loss?"actual-loss":"GL-error");
}
int main(int argc,char** argv){assert(argc==2);rt::Clock::SetFastForward(true);Run(argv[1],false);Run(argv[1],true);}
