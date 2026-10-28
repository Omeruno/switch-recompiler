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
#include <utility>
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
static std::vector<uint32_t> NativeExpected(Shell& shell,const std::vector<uint32_t>& guest,const std::u16string& clock=u"14:32") {
    ui::OwnedSurface chrome;chrome.Allocate(1280,720);shell.Render(chrome.s,clock);auto result=guest;
    for(int y=0;y<720;++y)for(int x=0;x<1280;++x)
        if(y>=680 || shell.PanelBounds().Contains(x,y) || shell.DialogBounds().Contains(x,y) ||
           (shell.StartOpen() && x<484 && y>=284 && y<680))result[size_t(y)*1280+x]=chrome.s.Get(x,y);
    return result;
}
struct CpuSink final:user::MixedWorkspace::CpuOutput {
    std::vector<uint32_t>* cpuPixels=nullptr;
    ui::Presentation& frames;double& output;bool reject=false;unsigned calls=0,resets=0;
    CpuSink(ui::Presentation& f,double& o):frames(f),output(o) {}
    bool Present(const ui::Surface& s,const ui::WindowSceneView& scene) override {
        ++calls;if(reject)return false;
        if(!(scene.owners?frames.Submit(s,scene):frames.Submit(s)))return false;
        return Redraw();
    }
    bool Redraw() override {const auto start=Clock::now();if(reject)return false;const ui::OutputBuffer target=cpuPixels?ui::OutputBuffer{cpuPixels->data(),1280,720,1280,cpuPixels->size()}:ui::OutputBuffer{nullptr,1280,720,0,0};const bool ok=frames.Render(target);output=Us(start);return ok;}
    void Reset() override {++resets;frames.ClearFrame();}
};
// Compare actual default CPU shell submission with the current native experiment.
// The final FBO oracle and host bookkeeping are outside timed RunOne/output calls.
static void Run(const char* path,unsigned mode,unsigned sample,int workload) {
    const bool gpu=mode!=0;
    Egl egl;rt::GuestSession session;user::EnableUi(1280,720);assert(user::EnableWindowLayers(true));
    char temp[]="/tmp/sr-cycles-XXXXXX";assert(mkdtemp(temp));
    struct Cleanup{std::string path;~Cleanup(){std::filesystem::remove_all(path);}}cleanup{temp};
    Shell shell;std::string error;assert(shell.ConfigureFilesystem(temp,"","",error));
    user::ShellSession bridge(shell);assert(bridge.Attach("Public CPU/native cycles"));bridge.SetClock(u"14:32");
    Bindings bindings;if(gpu)assert(bindings.Initialize() && bindings.EnableCoherence());
    ui::OpenGlPresentation renderer;assert(renderer.Initialize());ui::Presentation cpuFrames(&renderer);
    struct Sink{ui::OpenGlPresentation& renderer;double output=0;}sink{renderer};
    CpuSink cpuSink(cpuFrames,sink.output);
    user::MixedWorkspace workspace(bindings,bridge,[](void* p,Bindings& b,ui::MixedPresentation& frame){
        auto& sink=*static_cast<Sink*>(p);const auto begin=Clock::now();
        const bool ok=b.RenderMixed(frame,sink.renderer,{nullptr,1280,720,0,0});sink.output=Us(begin);return ok;
    },&sink);
    if(gpu)assert(workspace.Attach());
    if(mode==2)assert(workspace.EnableCpuRouting(&cpuSink));
    auto& process=session.ProcessObject();auto& memory=session.Memory();
    assert(process.LoadMainExe(path,error) && process.PrepareMainThread(process.MainThread().cpu,error));
    const auto probe=process.GetProcAddress(process.MainModule(),"WarmProbe");assert(probe);
    Bindings::Lease lease;unsigned checked=0;bool stopped=false;std::vector<uint32_t> guest;
    double raster=0;uint64_t outputFrames=0;
    {
        rt::GuestExecution execution(process,[&](rt::GuestThread& t){
            win32::HostCallOutcome call;if(win32::TryHandleHostCall(t.cpu,memory,call))return !call.requestExit;
            cpu::Interpreter cpu(memory,t.teb,t.teb);const auto result=cpu.Step(t.cpu);
            assert(result==cpu::StepResult::Continue || result==cpu::StepResult::Halted);return true;
        },rt::GuestExecution::Startup::InitializeModules);
        for(unsigned steps=0;steps<2000000;++steps){
            const auto begin=Clock::now();const auto result=execution.RunOne();raster+=Us(begin);
            assert(result!=rt::GuestExecution::Result::AllocationFailed);
            const auto stage=memory.Read32(probe);
            if(stage!=checked && stage>=1 && stage<=12){
                assert(stage==checked+1);checked=stage;
                if(stage==1){guest=Screen();if(gpu)assert(bindings.EnableSolidDispatch());raster=0;}
                else if(stage<=10){
                    const unsigned index=stage-2;
                    for(int y=0;y<128;++y)for(int x=0;x<128;++x){
                        const int off=4+int(index&1);
                        if(workload!=1 || (x%16>=off && x%16<off+8 && y%16>=off && y%16<off+8))
                            guest[size_t(y+50)*1280+x+40]=Tile(index,unsigned(y/16*8+x/16));
                    }
                    if(workload==2){for(int y=0;y<4;++y)for(int x=0;x<4;++x)guest[size_t(y+50)*1280+x+40]=0x100000u+index*0x101u;guest[size_t(150)*1280+140]=0xabc000u+index;}
                    const auto start=Clock::now();double output=0;
                    if(gpu){assert(workspace.Refresh()==user::NativePresentResult::Submitted);output=sink.output;}
                    else{
                        const auto* composed=bridge.TryCompose(user::S().screen.s);assert(composed);
                        const auto scene=bridge.Scene();assert(scene.owners && cpuFrames.Submit(*composed,scene));
                        const auto draw=Clock::now();assert(cpuFrames.Render({nullptr,1280,720,0,0}));output=Us(draw);
                    }
                    const auto refresh=Us(start);++outputFrames;
                    CompareGl(NativeExpected(shell,guest));
                    const auto& d=bindings.DispatchCounters();const auto& c=bindings.CoherenceCounters();
                    if(!gpu)assert(!bindings.Pixels() && !d.candidates && !d.accepted && !c.imports && Screen()==guest);
                    else if(workload==3)assert(!bindings.Pixels() && !d.accepted && d.refused==64*(index+1) && !c.imports && Screen()==guest);
                    else{
                        assert(bindings.Acquire(memory.Read32(probe+8),lease));const auto* stats=bindings.Counters(lease);assert(stats);
                        assert(d.accepted==uint64_t((workload==2?65:64)*(index+1)) && !d.refused);
                        assert(stats->readbacks==uint64_t(workload==2?index+1:0));
                        assert(c.imports==uint64_t(workload==2?index+2:1));
                        assert(c.seedUploadBytes==uint64_t(workload==2?(16384+16*(index+1))*4:65536));
                        assert(workspace.Submissions()==uint64_t(workload==2?index+1:1));
                    }
                    assert(!d.failed && d.candidates==d.accepted+d.refused);
                    assert(workspace.CpuFrames()==uint64_t(mode==2 && workload==3?index+1:0));
                    if(mode==2 && workload==3)assert(!workspace.Submissions() && !workspace.SnapshotBytes());
                    const auto& transfers=renderer.Timings();
                    printf("CYCLE %u %u %d %u %.3f %.3f %.3f %llu %llu %llu %llu %llu %llu %llu %llu %llu\n",sample,mode,workload,index,raster,refresh,output,
                        (unsigned long long)d.candidates,(unsigned long long)d.accepted,(unsigned long long)d.refused,
                        (unsigned long long)c.imports,(unsigned long long)c.commits,(unsigned long long)c.seedUploadBytes,
                        (unsigned long long)(transfers.screenUploadBytes+transfers.ownershipUploadBytes+transfers.layerUploadBytes),
                        (unsigned long long)(gpu?(workspace.Submissions()+workspace.CpuSubmissions()):cpuFrames.Stats().submitted),
                        (unsigned long long)(gpu?workspace.ReusedFrames():0));
                    raster=0;
                }else if(stage==11){
                    assert(Screen()==guest);
                    if(gpu && workload!=3)assert(bindings.Counters(lease)->readbacks==uint64_t(workload==2?10:1));
                    printf("READ %u %u %d %.3f %llu\n",sample,mode,workload,raster,
                        (unsigned long long)(gpu && workload!=3?bindings.Counters(lease)->readbacks:0));
                    raster=0;
                }
            }
            if(result==rt::GuestExecution::Result::Stopped){stopped=true;break;}
            if(result==rt::GuestExecution::Result::Idle)rt::Clock::Sleep(1);
        }
    }
    assert(stopped && checked==12 && outputFrames==9 && process.MainThread().exitCode==0);
    assert(!workspace.Faulted() && !user::S().presentationFault && !ui::g_gdi.CpuAccessFault());
    workspace.Detach();if(gpu)assert(bindings.DisableCoherence());bindings.Shutdown();cpuFrames.Detach();renderer.Shutdown();
    printf("EXIT %u %u %d 0 %.3f\n",sample,mode,workload,raster);
}
static void Routing(const char* path,int failure) {
    Egl egl;rt::GuestSession session;user::EnableUi(1280,720);assert(user::EnableWindowLayers(true));
    char temp[]="/tmp/sr-routing-XXXXXX";assert(mkdtemp(temp));
    struct Cleanup{std::string path;~Cleanup(){std::filesystem::remove_all(path);}}cleanup{temp};
    Shell shell;std::string error;assert(shell.ConfigureFilesystem(temp,"","",error));
    user::ShellSession bridge(shell);assert(bridge.Attach("Route transitions"));bridge.SetClock(u"14:32");
    Bindings bindings;assert(bindings.Initialize() && bindings.EnableCoherence());
    ui::OpenGlPresentation renderer;assert(renderer.Initialize());ui::Presentation frames(&renderer);double output=0;
    CpuSink cpuSink(frames,output);unsigned native=0;
    std::pair<ui::OpenGlPresentation*,unsigned*> context{&renderer,&native};
    user::MixedWorkspace routed(bindings,bridge,[](void* p,Bindings& b,ui::MixedPresentation& f){
        auto& state=*static_cast<std::pair<ui::OpenGlPresentation*,unsigned*>*>(p);++*state.second;
        return b.RenderMixed(f,*state.first,{nullptr,1280,720,0,0});
    },&context);
    assert(routed.Attach() && routed.EnableCpuRouting(&cpuSink));
    auto& process=session.ProcessObject();auto& memory=session.Memory();
    assert(process.LoadMainExe(path,error) && process.PrepareMainThread(process.MainThread().cpu,error));
    const auto probe=process.GetProcAddress(process.MainModule(),"CoherenceProbe");assert(probe);bool checked=false;
    {
        rt::GuestExecution execution(process,[&](rt::GuestThread& t){
            win32::HostCallOutcome call;if(win32::TryHandleHostCall(t.cpu,memory,call))return !call.requestExit;
            cpu::Interpreter cpu(memory,t.teb,t.teb);assert(cpu.Step(t.cpu)==cpu::StepResult::Continue);return true;
        },rt::GuestExecution::Startup::InitializeModules);
        for(unsigned steps=0;steps<700000 && !checked;++steps){
            const auto result=execution.RunOne();assert(result!=rt::GuestExecution::Result::AllocationFailed);
            if(memory.Read32(probe)==1){
                checked=true;auto guest=Screen();Bindings::Lease lease;assert(bindings.Acquire(memory.Read32(probe+8),lease));
                user::PresentIfNeeded(true);assert(!user::S().presentationFault && routed.CpuRoute() && routed.CpuSubmissions()==1 && !native && !routed.Submissions() && !routed.SnapshotBytes());CompareGl(NativeExpected(shell,guest));
                const auto barriers=bindings.CoherenceCounters().cpuBarriers;
                assert(routed.Redraw()==user::NativePresentResult::Submitted && routed.CpuSubmissions()==1 && bindings.CoherenceCounters().cpuBarriers==barriers);CompareGl(NativeExpected(shell,guest));
                {rt::GuestSession inner;user::EnableUi(1280,720);assert(!routed.EnableCpuRouting(nullptr));assert(routed.Refresh()==user::NativePresentResult::Unavailable && !routed.Faulted());}
                assert(routed.FrameCurrent());
                assert(bindings.Fill(lease,{2,2,3,3},0x123456));guest[size_t(lease.raster.originY+2)*1280+lease.raster.originX+2]=0x123456;
                user::PresentIfNeeded(true);assert(!routed.CpuRoute() && native==1 && routed.Submissions()==1 && routed.CpuSubmissions()==1);CompareGl(NativeExpected(shell,guest));
                const auto reads=bindings.Counters(lease)->readbacks;assert(ui::g_gdi.PrepareCpuAccess() && Screen()==guest && bindings.Counters(lease)->readbacks==reads+1);
                user::PresentIfNeeded(true);assert(routed.CpuRoute() && native==1 && routed.CpuSubmissions()==2);CompareGl(NativeExpected(shell,guest));
                auto* raw=ui::g_gdi.ScreenForWrite();assert(raw);raw->Set(1000,110,0xc0ffee);guest[size_t(110)*1280+1000]=0xc0ffee;
                user::PresentIfNeeded(true);assert(routed.CpuSubmissions()==3);CompareGl(NativeExpected(shell,guest));
                bridge.SetClock(u"14:33");user::PresentIfNeeded(true);assert(routed.CpuSubmissions()==4);CompareGl(NativeExpected(shell,guest,u"14:33"));
                ui::SoftwarePresentation software;std::vector<uint32_t> cpuPixels(1280*720);
                if(failure==2){
                    renderer.Shutdown();egl.Lose();bindings.ContextLost();frames.Attach(&software);frames.ClearLayers();cpuSink.cpuPixels=&cpuPixels;
                    assert(routed.Redraw()==user::NativePresentResult::Submitted && !bindings.GpuActive());
                    const auto expected=NativeExpected(shell,guest,u"14:33");
                    for(size_t n=0;n<expected.size();++n)assert(cpuPixels[n]==(0xff000000u|ui::RgbToColorref(expected[n])));
                }
                const auto presented=user::S().presentedChanges,producer=user::S().presentedProducerRevision;const auto calls=cpuSink.calls;
                cpuSink.reject=true;
                if(failure==0)user::PresentIfNeeded(true);
                else assert(routed.Redraw()==user::NativePresentResult::Failed);
                assert(routed.Faulted() && user::S().presentationFault && routed.CpuSubmissions()==4 && user::S().presentedChanges==presented && user::S().presentedProducerRevision==producer);
                assert(routed.Refresh()==user::NativePresentResult::Failed && cpuSink.calls==calls+unsigned(failure==0));
                routed.Detach();assert(cpuSink.resets==1 && !frames.SourceWidth());frames.Detach();assert(bindings.DisableCoherence());
            }
            if(result==rt::GuestExecution::Result::Idle)rt::Clock::Sleep(1);
        }
    }
    assert(checked);bindings.Shutdown();frames.Detach();renderer.Shutdown();
    printf("CPU route transitions mode%d PASS (CPU/GPU/CPU, real read, redraw/clock/raw-write, suspended owner, failed output/no retry/no ack; parked guest drained)\n",failure);
}
int main(int argc,char** argv){
    assert(argc==6);ui::FontCache::SetDirectory("romfs/fonts");rt::Clock::SetFastForward(true);
    for(unsigned sample=0;sample<4;++sample)for(int workload=0;workload<4;++workload)for(int i=0;i<3;++i)
        Run(argv[workload+1],(sample+i)%3,sample,workload);
    Routing(argv[5],0);Routing(argv[5],1);Routing(argv[5],2);
    puts("CPU/native workspace cycles PASS (48 normal exits/432 actual EGL frames, production CPU/native routes, real reads/DIB and size refusal)");
}
