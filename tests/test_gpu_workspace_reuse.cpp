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
using Shell=ui::DesktopShell;
static void ShellClick(user::ShellSession& bridge,Shell& shell,int target) {
    const auto r=shell.ControlBounds(target);assert(!r.Empty());
    for(auto type:{user::InputEvent::MouseDown,user::InputEvent::MouseUp}) {
        user::InputEvent e;e.type=type;e.x=r.left+r.Width()/2;e.y=r.top+r.Height()/2;assert(bridge.Filter(e));
    }
}
static std::vector<uint32_t> NativeExpected(Shell& shell,const std::vector<uint32_t>& guest,const std::u16string& clock=u"14:32") {
    ui::OwnedSurface chrome;chrome.Allocate(1280,720);shell.Render(chrome.s,clock);auto result=guest;
    for(int y=0;y<720;++y)for(int x=0;x<1280;++x)
        if(y>=680 || shell.PanelBounds().Contains(x,y) || shell.DialogBounds().Contains(x,y) ||
           (shell.StartOpen() && x<484 && y>=284 && y<680))result[size_t(y)*1280+x]=chrome.s.Get(x,y);
    return result;
}
static void Run(const char* path,int mode,unsigned sample,int workload) {
    Egl egl;rt::GuestSession session;user::EnableUi(1280,720);assert(user::EnableWindowLayers(true));
    char temp[]="/tmp/sr-frame-XXXXXX";assert(mkdtemp(temp));
    struct Cleanup{std::string path;~Cleanup(){std::filesystem::remove_all(path);}}cleanup{temp};
    Shell shell;std::string error;assert(shell.ConfigureFilesystem(temp,"","",error));
    user::ShellSession bridge(shell);assert(bridge.Attach("Public retained frames"));bridge.SetClock(u"14:32");
    Bindings bindings;assert(bindings.Initialize() && bindings.EnableCoherence());
    ui::OpenGlPresentation renderer;assert(renderer.Initialize());
    struct Sink{ui::OpenGlPresentation& renderer;double output=0;uint64_t copied=0;}sink{renderer};
    user::MixedWorkspace workspace(bindings,bridge,[](void* p,Bindings& b,ui::MixedPresentation& frame){
        auto& sink=*static_cast<Sink*>(p);const auto start=Clock::now();
        const bool ok=b.RenderMixed(frame,sink.renderer,{nullptr,1280,720,0,0});
        sink.output=Us(start);sink.copied=frame.CopiedBytes();return ok;
    },&sink);
    assert(workspace.Attach());workspace.EnableSnapshotReuse(mode!=0);
    auto& process=session.ProcessObject();auto& memory=session.Memory();
    assert(process.LoadMainExe(path,error) && process.PrepareMainThread(process.MainThread().cpu,error));
    const auto probe=process.GetProcAddress(process.MainModule(),"WarmProbe");assert(probe);
    Bindings::Lease lease;unsigned checked=0;bool stopped=false;std::vector<uint32_t> guest;
    auto phase=Clock::now();
    {
        rt::GuestExecution execution(process,[&](rt::GuestThread& t){
            win32::HostCallOutcome call;if(win32::TryHandleHostCall(t.cpu,memory,call))return !call.requestExit;
            cpu::Interpreter cpu(memory,t.teb,t.teb);const auto result=cpu.Step(t.cpu);
            assert(result==cpu::StepResult::Continue || result==cpu::StepResult::Halted);return true;
        },rt::GuestExecution::Startup::InitializeModules);
        for(unsigned steps=0;steps<2000000;++steps){
            const auto result=execution.RunOne();assert(result!=rt::GuestExecution::Result::AllocationFailed);
            const auto stage=memory.Read32(probe);
            if(stage!=checked && stage>=1 && stage<=12){
                assert(stage==checked+1);checked=stage;
                if(stage==1){guest=Screen();assert(bindings.EnableSolidDispatch());phase=Clock::now();}
                else if(stage<=10){
                    const auto raster=Us(phase);const unsigned index=stage-2;
                    for(int y=0;y<128;++y)for(int x=0;x<128;++x) {
                        const int off=4+int(index&1);
                        if(workload!=1 || (x%16>=off && x%16<off+8 && y%16>=off && y%16<off+8))
                            guest[size_t(y+50)*1280+x+40]=Tile(index,unsigned(y/16*8+x/16));
                    }
                    if(workload==2){for(int y=0;y<4;++y)for(int x=0;x<4;++x)guest[size_t(y+50)*1280+x+40]=0x100000u+index*0x101u;guest[size_t(150)*1280+140]=0xabc000u+index;}
                    assert(bindings.Acquire(memory.Read32(probe+8),lease));
                    const auto a=Clock::now();assert(workspace.Refresh()==user::NativePresentResult::Submitted);const auto total=Us(a);
                    CompareGl(NativeExpected(shell,guest));
                    const auto* c=bindings.Counters(lease);
                    constexpr unsigned partialReads[]={0,2,3,4,6,7,8,10,11},partialCommits[]={0,1,1,2,3,3,4,5,5};
                    assert(c && c->readbacks==uint64_t(workload==2?1+index*2:workload==1?partialReads[index]:0));
                    assert(c->cpuImports==uint64_t(workload==2?index+2:1));
                    assert(bindings.CoherenceCounters().commits==uint64_t(workload==2?index+1:workload==1?partialCommits[index]:0));
                    if(workload==0)assert(c->batches==index+1);
                    constexpr unsigned partialSnapshots[]={1,2,2,3,4,4,5,6,6};
                    const auto snapshots=mode?(workload==2?index+1:workload==1?partialSnapshots[index]:1):index+1;
                    assert(workspace.Submissions()==snapshots && workspace.ReusedFrames()==index+1-snapshots);
                    printf("FRAME %u %d %d %u %.3f %.3f %.3f %llu %llu %llu %llu\n",sample,mode,workload,index,raster,total,sink.output,
                        static_cast<unsigned long long>(workspace.Submissions()),static_cast<unsigned long long>(sink.copied),static_cast<unsigned long long>(c->readbacks),static_cast<unsigned long long>(bindings.CoherenceCounters().commits));
                    phase=Clock::now();
                }else if(stage==11){assert(Screen()==guest);assert(bindings.Counters(lease)->readbacks==uint64_t(workload==2?18:workload==1?12:1));}
            }
            if(result==rt::GuestExecution::Result::Stopped){stopped=true;break;}
            if(result==rt::GuestExecution::Result::Idle)rt::Clock::Sleep(1);
        }
    }
    assert(stopped && checked==12 && process.MainThread().exitCode==0 && !workspace.Faulted() && !user::S().presentationFault);
    workspace.Detach();assert(bindings.DisableCoherence());bindings.Shutdown();renderer.Shutdown();
}
static void Transitions(const char* path) {
    Egl egl;rt::GuestSession session;user::EnableUi(1280,720);assert(user::EnableWindowLayers(true));
    char temp[]="/tmp/sr-epoch-XXXXXX";assert(mkdtemp(temp));
    struct Cleanup{std::string path;~Cleanup(){std::filesystem::remove_all(path);}}cleanup{temp};
    Shell shell;std::string error;assert(shell.ConfigureFilesystem(temp,"","",error));
    user::ShellSession bridge(shell);assert(bridge.Attach("Transition oracle"));bridge.SetClock(u"14:32");
    Bindings bindings;assert(bindings.Initialize() && bindings.EnableCoherence());
    ui::OpenGlPresentation renderer;assert(renderer.Initialize());
    auto& process=session.ProcessObject();auto& memory=session.Memory();
    assert(process.LoadMainExe(path,error) && process.PrepareMainThread(process.MainThread().cpu,error));
    const auto probe=process.GetProcAddress(process.MainModule(),"CoherenceProbe");assert(probe);bool checked=false;
    {
        rt::GuestExecution execution(process,[&](rt::GuestThread& t){
            win32::HostCallOutcome call;if(win32::TryHandleHostCall(t.cpu,memory,call))return !call.requestExit;
            cpu::Interpreter cpu(memory,t.teb,t.teb);const auto result=cpu.Step(t.cpu);
            assert(result==cpu::StepResult::Continue || result==cpu::StepResult::Halted);return true;
        },rt::GuestExecution::Startup::InitializeModules);
        for(unsigned steps=0;steps<700000 && !checked;++steps){
            const auto result=execution.RunOne();assert(result!=rt::GuestExecution::Result::AllocationFailed);
            if(memory.Read32(probe)==1){
                checked=true;auto guest=Screen();Bindings::Lease a,b;
                assert(bindings.Acquire(memory.Read32(probe+8),a) && bindings.Acquire(memory.Read32(probe+16),b));
                auto draw=[&](Bindings::Lease& lease,ui::Rect r,uint32_t rgb){
                    assert(bindings.Fill(lease,r,rgb));
                    for(int y=r.top;y<r.bottom;++y)for(int x=r.left;x<r.right;++x)
                        guest[size_t(y+lease.raster.originY)*1280+x+lease.raster.originX]=rgb;
                };
                draw(a,{1,1,2,2},0x567890);
                struct Sink{ui::OpenGlPresentation& renderer;Shell& shell;std::vector<uint32_t>& guest;unsigned calls=0;bool reject=false;std::u16string clock=u"14:32";}sink{renderer,shell,guest};
                user::MixedWorkspace workspace(bindings,bridge,[](void* p,Bindings& binding,ui::MixedPresentation& frame){
                    auto& sink=*static_cast<Sink*>(p);++sink.calls;if(sink.reject)return false;
                    assert(binding.RenderMixed(frame,sink.renderer,{nullptr,1280,720,0,0}));CompareGl(NativeExpected(sink.shell,sink.guest,sink.clock));return true;
                },&sink);assert(workspace.Attach());
                auto present=[&](){assert(workspace.Refresh()==user::NativePresentResult::Submitted && !workspace.Faulted());};
                present();const auto bytes=workspace.SnapshotBytes();assert(workspace.Submissions()==1);
                const auto cpu=ui::g_gdi.ScreenChangeCounter(),layout=ui::g_gdi.LayoutGeneration(),owners=bindings.OwnershipRevision();
                draw(a,{2,2,4,4},0xaabbcc);assert(!workspace.FrameCurrent());present();
                assert(workspace.Submissions()==1 && workspace.ReusedFrames()==1 && workspace.SnapshotBytes()==bytes && bindings.OwnershipRevision()==owners);
                draw(b,{0,0,2,2},0xbeef42);assert(ui::g_gdi.ScreenChangeCounter()==cpu && ui::g_gdi.LayoutGeneration()==layout && bindings.OwnershipRevision()!=owners);present();
                assert(workspace.Submissions()==2); // new resident owner, unchanged CPU/layout
                assert(ui::g_gdi.PrepareCpuAccess() && Screen()==guest);present();assert(workspace.Submissions()==3);
                const auto stableOwners=bindings.OwnershipRevision();auto* raw=ui::g_gdi.ScreenForWrite();assert(raw);raw->Set(1000,110,0xc0ffee);guest[size_t(110)*1280+1000]=0xc0ffee;
                assert(bindings.OwnershipRevision()==stableOwners);present();assert(workspace.Submissions()==4);
                assert(bindings.Acquire(a.raster.hdc,a));draw(a,{4,4,6,6},0x123456);present();assert(workspace.Submissions()==5); // same resource becomes resident again
                const auto calls=sink.calls;
                {rt::GuestSession inner;user::EnableUi(1280,720);assert(!workspace.FrameCurrent());assert(workspace.Refresh()==user::NativePresentResult::Unavailable && !workspace.Faulted() && !user::S().presentationFault);}
                assert(workspace.FrameCurrent() && sink.calls==calls);
                assert(ui::g_gdi.PrepareCpuAccess());present();assert(workspace.Submissions()==6);
                ShellClick(bridge,shell,Shell::Start);present();assert(workspace.Submissions()==7);
                const auto counter=ui::g_gdi.ScreenChangeCounter();ui::g_gdi.InvalidateLayout();assert(!workspace.FrameCurrent());present();assert(workspace.Submissions()==8 && ui::g_gdi.ScreenChangeCounter()==counter);
                bridge.SetClock(u"14:33");sink.clock=u"14:33";present();assert(workspace.Submissions()==9);
                // Same CPU/shell/layout with a new manager generation must not
                // reuse an old frame, even when it has zero resident layers.
                assert(bindings.Initialize() && bindings.EnableCoherence());present();assert(workspace.Submissions()==10);
                assert(bindings.Acquire(memory.Read32(probe+8),a));draw(a,{7,7,9,9},0x987654);present();assert(workspace.Submissions()==11);
                draw(a,{9,9,10,10},0xabc123);const auto copied=workspace.SnapshotBytes(),reused=workspace.ReusedFrames();sink.reject=true;
                assert(workspace.Refresh()==user::NativePresentResult::Failed && workspace.Faulted() && user::S().presentationFault);
                assert(workspace.Submissions()==11 && workspace.SnapshotBytes()==copied && workspace.ReusedFrames()==reused);
                const auto failedCalls=sink.calls;assert(workspace.Redraw()==user::NativePresentResult::Failed && sink.calls==failedCalls);
                workspace.Detach();assert(bindings.DisableCoherence());
            }
            if(result==rt::GuestExecution::Result::Idle)rt::Clock::Sleep(1);
        }
    }
    assert(checked);bindings.Shutdown();renderer.Shutdown();
    puts("Retained transitions PASS (new owner/reimport/CPU/shell/layout/reset/foreign-session/failure; parked guest drained)");
}
int main(int argc,char** argv){
    assert(argc==5);ui::FontCache::SetDirectory("romfs/fonts");rt::Clock::SetFastForward(true);
    for(unsigned sample=0;sample<4;++sample)for(int workload=0;workload<3;++workload)for(int i=0;i<2;++i)Run(argv[workload+1],int((sample+i)%2),sample,workload);
    Transitions(argv[4]);
    puts("Retained workspace: PASS (real x86, owning native shell frames and pixel/read gates)");
}
