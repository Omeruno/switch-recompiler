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
static void CheckMixedPixels(Bindings& bindings,ui::MixedPresentation& frame,
    const std::vector<uint32_t>& expected,bool cpu=false) {
    std::vector<uint32_t> rgba(1280*720);
    if(cpu) {
        ui::SoftwarePresentation renderer;
        assert(bindings.RenderMixedCpu(frame,renderer,{rgba.data(),1280,720,1280,rgba.size()}));
    } else {
        ui::OpenGlPresentation renderer;assert(renderer.Initialize());
        const auto before=bindings.CoherenceCounters();
        for(int n=0;n<3;++n)assert(bindings.RenderMixed(frame,renderer,{nullptr,1280,720,0,0}));
        const auto after=bindings.CoherenceCounters();assert(before.commits==after.commits && before.cpuBarriers==after.cpuBarriers);
        glBindFramebuffer(GL_READ_FRAMEBUFFER,0);glBindBuffer(GL_PIXEL_PACK_BUFFER,0);
        glPixelStorei(GL_PACK_ALIGNMENT,4);glPixelStorei(GL_PACK_ROW_LENGTH,0);
        glPixelStorei(GL_PACK_SKIP_ROWS,0);glPixelStorei(GL_PACK_SKIP_PIXELS,0);
        glReadPixels(0,0,1280,720,GL_RGBA,GL_UNSIGNED_BYTE,rgba.data());assert(glGetError()==GL_NO_ERROR);
        for(int y=0;y<360;++y)for(int x=0;x<1280;++x)std::swap(rgba[size_t(y)*1280+x],rgba[size_t(719-y)*1280+x]);
        renderer.Shutdown();
    }
    for(size_t i=0;i<rgba.size();++i)if(rgba[i]!=(0xff000000u|ui::RgbToColorref(expected[i]))) {
        std::fprintf(stderr,"mixed cpu=%d pixel=%zu,%zu actual=%08x expected=%06x\n",int(cpu),i%1280,i/1280,rgba[i],expected[i]);assert(false);
    }
}
using Shell=ui::DesktopShell;
static void ShellClick(user::ShellSession& bridge,Shell& shell,int target) {
    const auto r=shell.ControlBounds(target);assert(!r.Empty());
    for(auto type:{user::InputEvent::MouseDown,user::InputEvent::MouseUp}) {
        user::InputEvent e;e.type=type;e.x=r.left+r.Width()/2;e.y=r.top+r.Height()/2;assert(bridge.Filter(e));
    }
}
static std::vector<uint32_t> NativeExpected(Shell& shell,const std::vector<uint32_t>& guest) {
    ui::OwnedSurface chrome;chrome.Allocate(1280,720);shell.Render(chrome.s,u"14:32");auto result=guest;
    for(int y=0;y<720;++y)for(int x=0;x<1280;++x)
        if(y>=680 || shell.PanelBounds().Contains(x,y) || shell.DialogBounds().Contains(x,y) ||
           (shell.StartOpen() && x<484 && y>=284 && y<680))result[size_t(y)*1280+x]=chrome.s.Get(x,y);
    return result;
}
static void ExerciseWorkspace(Bindings& bindings,user::ShellSession& bridge,Shell& shell,
    const std::vector<uint32_t>& guest,const Bindings::Lease& lease) {
    struct Sink {std::vector<uint32_t> expected;unsigned calls=0;};Sink sink{NativeExpected(shell,guest),0};
    const auto previous=user::g_platform;const auto cpu=Screen();const auto counts=bindings.CoherenceCounters();
    user::MixedWorkspace workspace(bindings,bridge,[](void* p,Bindings& b,ui::MixedPresentation& frame) {
        auto& sink=*static_cast<Sink*>(p);CheckMixedPixels(b,frame,sink.expected);++sink.calls;return true;
    },&sink);
    assert(workspace.Attach());user::PresentIfNeeded(true);assert(workspace.Frames()==1 && sink.calls==1);
    user::S().lastPresentMs-=16;user::PresentIfNeeded();assert(sink.calls==1); // no changed epoch
    auto present=[&]() {
        sink.expected=NativeExpected(shell,guest);const auto before=sink.calls;
        user::S().lastPresentMs-=16;user::PresentIfNeeded();assert(sink.calls==before+1);
        assert(!workspace.Faulted() && !user::S().presentationFault && Screen()==cpu);
        const auto now=bindings.CoherenceCounters();assert(now.commits==counts.commits && now.cpuBarriers==counts.cpuBarriers);
    };
    const auto epoch=bindings.PresentationRevision();
    assert(bindings.Fill(lease,{30,30,40,40},0xaabbcc) && bindings.PresentationRevision()>epoch);present();
    ShellClick(bridge,shell,Shell::Start);ShellClick(bridge,shell,Shell::MenuPrograms);assert(shell.ActivePanel()==Shell::Panel::Programs);present();
    auto title=shell.ControlBounds(Shell::TitleBar);
    for(auto type:{user::InputEvent::MouseDown,user::InputEvent::MouseMove,user::InputEvent::MouseUp}) {
        user::InputEvent e;e.type=type;e.x=title.left+40+(type==user::InputEvent::MouseDown?0:-80);
        e.y=title.top+12+(type==user::InputEvent::MouseDown?0:30);assert(bridge.Filter(e));
    }
    present();ShellClick(bridge,shell,Shell::Maximize);present();
    ShellClick(bridge,shell,Shell::Minimize);present();ShellClick(bridge,shell,Shell::NativeTask);present();ShellClick(bridge,shell,Shell::Maximize);present();
    ShellClick(bridge,shell,Shell::Start);assert(shell.StartOpen());present();
    ShellClick(bridge,shell,Shell::Settings);assert(shell.ActiveDialog()==Shell::Dialog::Preferences);present();
    const auto dialog=shell.DialogBounds();
    for(auto type:{user::InputEvent::MouseDown,user::InputEvent::MouseMove,user::InputEvent::MouseUp}) {
        user::InputEvent e;e.type=type;e.x=type==user::InputEvent::MouseDown?dialog.left+80:100;
        e.y=type==user::InputEvent::MouseDown?dialog.top+12:32;assert(bridge.Filter(e));
    }
    assert(shell.DialogBounds().left==20 && shell.DialogBounds().top==20);present();
    ShellClick(bridge,shell,Shell::DialogClose);present();
    ShellClick(bridge,shell,Shell::Close);present();
    assert(user::Q().input.empty());workspace.Detach();
    assert(user::g_platform.tryPresent==previous.tryPresent && user::g_platform.presentationContext==previous.presentationContext);
    puts("Mixed native workspace: PASS (real USER selection, shell/taskbar/Start/panel/dialog ownership, move/max/min/restore, no CPU checkpoint)");
}
static void Run(const char* path,bool loss) {
    Egl egl;PrimitiveImport();ExpiredGate();
    rt::GuestSession session;user::EnableUi(1280,720);assert(user::EnableWindowLayers(true));
    char temp[]="/tmp/sr-mixed-XXXXXX";assert(mkdtemp(temp));
    struct Cleanup {std::string path;~Cleanup(){std::filesystem::remove_all(path);}} cleanup{temp};
    Shell shell;std::string shellError;assert(shell.ConfigureFilesystem(temp,"","",shellError));
    user::ShellSession bridge(shell);assert(bridge.Attach("Public coherent windows"));bridge.SetClock(u"14:32");
    assert(!user::S().presentationFault);
    Allocator allocator;Bindings bindings(&allocator);assert(bindings.Initialize() && bindings.EnableCoherence());
    auto& p=session.ProcessObject();auto& memory=session.Memory();std::string error;
    assert(p.LoadMainExe(path,error) && p.PrepareMainThread(p.MainThread().cpu,error));
    const auto probe=p.GetProcAddress(p.MainModule(),"CoherenceProbe");assert(probe);
    ui::MixedPresentation held(&allocator);
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
                     assert(!bindings.SubmitMixed(held));
                     assert(ui::g_gdi.GetPixel(sdc,0,0)!=0xffffffffu);assert(ui::g_gdi.DeleteDc(sdc));}
                    assert(bindings.Valid(lease) && !bindings.Counters(lease)->readbacks && Screen()==expected);
                    for(int y=0;y<128;++y)for(int x=0;x<128;++x)expected[size_t(y+50)*1280+x+40]=Tile(unsigned(y/16*8+x/16));
                    for(int y=0;y<16;++y)for(int x=0;x<16;++x)expected[size_t(y+50)*1280+x+300]=0x4162a3;
                    const auto stats=bindings.CoherenceCounters();const auto prior=*bindings.Counters(lease);
                    assert(bindings.SubmitMixed(held));
                    const auto kept=held.CpuBytes();allocator.failAt=allocator.calls+1;
                    assert(!bindings.SubmitMixed(held) && held.CpuBytes()==kept && bindings.CoherenceHealthy());
                    allocator.failAt=0;CheckMixedPixels(bindings,held,expected);

                    assert(bindings.Counters(lease)->readbacks==0 && bindings.Counters(other)->readbacks==0);
                    assert(bindings.Counters(lease)->uploads==prior.uploads);
                    assert(bindings.CoherenceCounters().commits==stats.commits && bindings.CoherenceCounters().cpuBarriers==stats.cpuBarriers);
                } else if(stage==2) {
                    const auto c=*bindings.Counters(lease);
                    assert(c.draws==2 && c.uploads==1 && c.readbacks==1 && c.batches==1 && c.batchedCommands==64 && c.cpuImports==1);
                    const auto stats=bindings.CoherenceCounters();assert(stats.imports==2 && stats.commits==2 && stats.commitPixels==128*128+16*16);
                    RenderExpected(expected);assert(bindings.Counters(lease)->readbacks==1);
                    assert(bindings.Fill(lease,{5,5,21,21},0x887766));
                } else if(stage==3) {
                    assert(!bindings.Valid(lease) && bindings.Acquire(dc,lease));
                    assert(bindings.CoherenceCounters().commits==3 && bindings.Counters(lease)->readbacks==2);
                    assert(user::S().screen.s.Get(46,56)==0x778899);
                    const auto cpuBefore=Screen();assert(cpuBefore[size_t(160)*1280+150]==0x6912cd);
                    assert(bindings.Fill(lease,{30,30,40,40},0xaabbcc));
                    auto mixedExpected=cpuBefore;
                    for(int y=80;y<90;++y)for(int x=70;x<80;++x)mixedExpected[size_t(y)*1280+x]=0xaabbcc;
                    const auto prior=*bindings.Counters(lease);assert(bindings.SubmitMixed(held));
                    CheckMixedPixels(bindings,held,mixedExpected);
                    ExerciseWorkspace(bindings,bridge,shell,mixedExpected,lease);
                    assert(bindings.Counters(lease)->readbacks==prior.readbacks && Screen()==cpuBefore);
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
                    assert(bindings.SubmitMixed(held));
                    if(loss){egl.Lose();bindings.ContextLost();}else glEnable(0xdead);
                } else if(stage==9) {
                    assert(bindings.CoherenceCounters().commits==9 && !bindings.GpuActive() && bindings.CoherenceHealthy());
                    assert(bindings.Counters(lease)->recoveries>0);
                    CheckMixedPixels(bindings,held,Screen(),true);
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
    assert(stopped && checked==10);bindings.Prune();assert(bindings.Pixels()==128*128); // held frame pins the destroyed window
    unsigned attempts=0;
    {
        user::MixedWorkspace failed(bindings,bridge,[](void* p,Bindings&,ui::MixedPresentation&) {
            ++*static_cast<unsigned*>(p);return false;
        },&attempts);
        assert(failed.Attach());const auto acknowledged=user::S().presentedChanges;const auto acknowledgedProducer=user::S().presentedProducer;const auto acknowledgedTime=user::S().lastPresentMs;
        user::PresentIfNeeded(true);assert(attempts==1 && failed.Faulted() && user::S().presentationFault);
        assert(user::S().presentedChanges==acknowledged && user::S().presentedProducer==acknowledgedProducer && user::S().lastPresentMs==acknowledgedTime);user::PresentIfNeeded(true);assert(attempts==1);
    }
    assert(bindings.DisableCoherence() && ui::g_gdi.PrepareCpuAccess());bindings.Shutdown();held.Clear();assert(!allocator.live);
    std::printf("GPU coherence mode=%s: PASS (10 x86 stages, 2 dirty windows, 64 fills/1 import/1 readback for first window, ROP/raw-DIB/GetDIBits, layout, owned live mixed output/occlusion, fallback)\n",loss?"actual-loss":"GL-error");
}
struct OutputDeviceProbe:user::MixedOutputDevice {
    Egl& egl;ui::OpenGlPresentation& renderer;
    std::vector<uint32_t> expected;
    ui::PresentationCursor cursor;
    std::string events;bool gpu=true,swapOk=true,openOk=true,queueOk=true;
    OutputDeviceProbe(Egl& e,ui::OpenGlPresentation& r):egl(e),renderer(r) {}
    bool GpuActive() const override {return gpu;}
    bool SwapGpu() override {
        events+='S';std::vector<uint32_t> actual(1280*720);
        glBindFramebuffer(GL_READ_FRAMEBUFFER,0);glBindBuffer(GL_PIXEL_PACK_BUFFER,0);
        glPixelStorei(GL_PACK_ALIGNMENT,4);glPixelStorei(GL_PACK_ROW_LENGTH,0);
        glPixelStorei(GL_PACK_SKIP_ROWS,0);glPixelStorei(GL_PACK_SKIP_PIXELS,0);
        glReadPixels(0,0,1280,720,GL_RGBA,GL_UNSIGNED_BYTE,actual.data());assert(glGetError()==GL_NO_ERROR);
        for(int y=0;y<720;++y)for(int x=0;x<1280;++x)assert(actual[size_t(719-y)*1280+x]==expected[size_t(y)*1280+x]);
        return swapOk;
    }
    void LeaveGpu() override {
        events+='L';assert(user::EnableWindowLayers(false));renderer.Shutdown();egl.Lose();gpu=false;
    }
    bool EnsureSoftware() override {events+='E';assert(!gpu && eglGetCurrentContext()==EGL_NO_CONTEXT);return openOk;}
    bool QueueSoftware(const ui::OutputBuffer& out) override {
        events+='Q';assert(out.Valid());
        for(int y=0;y<720;++y)for(int x=0;x<1280;++x)assert(out.pixels[size_t(y)*out.stridePixels+x]==expected[size_t(y)*1280+x]);
        return queueOk;
    }
};
static void DeviceRun(const char* path,int mode) {
    Egl egl;rt::GuestSession session;user::EnableUi(1280,720);assert(user::EnableWindowLayers(true));
    Shell shell;user::ShellSession bridge(shell);assert(bridge.Attach("Public device adapter"));bridge.SetClock(u"14:32");
    Allocator allocator;Bindings bindings;assert(bindings.Initialize() && bindings.EnableCoherence());
    ui::OpenGlPresentation renderer;assert(renderer.Initialize());ui::SoftwarePresentation software;
    OutputDeviceProbe device(egl,renderer);user::MixedOutput adapter(device,renderer,software,&allocator);
    struct Context {user::MixedOutput& adapter;OutputDeviceProbe& device;uint64_t snapshot=0;} output{adapter,device,0};
    user::MixedWorkspace workspace(bindings,bridge,[](void* p,Bindings& b,ui::MixedPresentation& frame) {
        auto& context=*static_cast<Context*>(p);context.snapshot=frame.CopiedBytes();
        return context.adapter.Present(b,frame,1280,720,context.device.cursor);
    },&output);assert(workspace.Attach());
    auto& process=session.ProcessObject();auto& memory=session.Memory();std::string error;
    assert(process.LoadMainExe(path,error) && process.PrepareMainThread(process.MainThread().cpu,error));
    const auto probe=process.GetProcAddress(process.MainModule(),"CoherenceProbe");assert(probe);bool checked=false;
    {
        rt::GuestExecution execution(process,[&](rt::GuestThread& t) {
            win32::HostCallOutcome call;if(win32::TryHandleHostCall(t.cpu,memory,call))return !call.requestExit;
            cpu::Interpreter cpu(memory,t.teb,t.teb);const auto result=cpu.Step(t.cpu);
            assert(result==cpu::StepResult::Continue || result==cpu::StepResult::Halted);return true;
        },rt::GuestExecution::Startup::InitializeModules);
        for(unsigned i=0;i<300000 && !checked;++i) {
            const auto result=execution.RunOne();assert(result!=rt::GuestExecution::Result::AllocationFailed);
            if(memory.Read32(probe)==1) {
                checked=true;Bindings::Lease a,b;
                assert(bindings.Acquire(memory.Read32(probe+8),a) && bindings.Acquire(memory.Read32(probe+16),b));
                auto canonical=Screen();assert(bindings.Fill(a,{0,0,128,128},0x1279ad));assert(bindings.Fill(b,{0,0,16,16},0xa94317));
                auto rgb=canonical;
                for(int y=50;y<178;++y)for(int x=40;x<168;++x)rgb[size_t(y)*1280+x]=0x1279ad;
                for(int y=50;y<66;++y)for(int x=300;x<316;++x)rgb[size_t(y)*1280+x]=0xa94317;
                rgb=NativeExpected(shell,rgb);ui::Surface reference;reference.width=1280;reference.height=720;reference.pitch=1280*4;
                reference.bits=reinterpret_cast<uint8_t*>(rgb.data());ui::SoftwarePresentation oracle;
                auto expected=[&](int n) {
                    device.cursor={15+n,24+n,true};device.expected.resize(1280*720);
                    ui::PresentationFrame frame{reference,{0,0,1280,720},device.cursor};
                    assert(oracle.Render(frame,{device.expected.data(),1280,720,1280,device.expected.size()}));
                };
                expected(0);user::PresentIfNeeded(true);assert(workspace.Submissions()==1 && workspace.Frames()==1);
                const auto snapshot=output.snapshot,uploads=renderer.Timings().screenUploadBytes;
                for(int n=1;n<12;++n) {
                    expected(n);assert(workspace.Redraw()==user::NativePresentResult::Submitted);
                    assert(workspace.Submissions()==1 && output.snapshot==snapshot && workspace.Redraws()==uint64_t(n));
                    assert(renderer.Timings().screenUploadBytes==uploads && Screen()==canonical);
                    assert(bindings.Counters(a)->readbacks==0 && bindings.Counters(b)->readbacks==0);
                }
                assert(adapter.Counters().gpuFrames==12 && device.events==std::string(12,'S'));
                device.events.clear();expected(12);
                if(mode==5) {renderer.Shutdown();egl.Lose();bindings.ContextLost();device.gpu=false;}
                else if(mode==1)device.swapOk=false;
                else if(mode==6) {
                    assert(ui::g_gdi.PrepareCpuAccess());assert(workspace.Refresh()==user::NativePresentResult::Submitted);
                    const auto before=workspace.Submissions();const auto pixels=ui::g_gdi.ScreenChangeCounter();
                    ui::g_gdi.InvalidateLayout();assert(!workspace.FrameCurrent() && ui::g_gdi.ScreenChangeCounter()==pixels);
                    assert(workspace.Redraw()==user::NativePresentResult::Submitted && workspace.Submissions()==before+1);
                } else glEnable(0xdead);
                if(mode==2)allocator.failAt=allocator.calls+1;
                if(mode==3)device.openOk=false;
                if(mode==4)device.queueOk=false;
                if(mode!=6) {
                    const bool success=mode==0 || mode==1 || mode==5;
                    const auto result=workspace.Redraw();
                    assert((result==user::NativePresentResult::Submitted)==success);
                    if(success) {
                        assert(adapter.Counters().cpuFrames==1 && !adapter.Faulted());
                        assert(adapter.Counters().gpuFrames==12 && adapter.Counters().swapAttempts==uint64_t(mode==1?13:12));
                        assert(adapter.Counters().fallbacks==uint64_t(mode==5?0:1));
                        assert(device.events==(mode==1?"SLEQ":mode==5?"EQ":"LEQ"));
                        expected(13);assert(workspace.Redraw()==user::NativePresentResult::Submitted);
                        assert(adapter.Counters().cpuFrames==2);
                    } else {
                        assert(adapter.Faulted() && workspace.Faulted() && user::S().presentationFault);
                        assert(device.events==(mode==2?"":mode==3?"LE":"LEQ"));
                        const auto events=device.events;assert(workspace.Redraw()==user::NativePresentResult::Failed && device.events==events);
                    }
                }
                // Scope destruction drains the parked native continuation.
            }
            if(result==rt::GuestExecution::Result::Idle)rt::Clock::Sleep(1);
        }
    }
    assert(checked);workspace.Detach();bindings.Shutdown();renderer.Shutdown();adapter.Release();assert(!allocator.live);
    std::printf("Mixed output device mode=%d: PASS (actual x86/EGL, 12 retained cursor frames, no readback/upload/snapshot, recovery/queue/fault/layout ordering)\n",mode);
}
int main(int argc,char** argv){assert(argc==2);ui::FontCache::SetDirectory("romfs/fonts");ui::FontDesc font;font.face="Liberation Sans";font.height=-16;assert(!ui::FontCache::Get(font)->Metrics().fixedPitch);rt::Clock::SetFastForward(true);Run(argv[1],false);Run(argv[1],true);for(int mode=0;mode<7;++mode)DeviceRun(argv[1],mode);}
