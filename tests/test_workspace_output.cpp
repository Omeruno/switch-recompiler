#include "source/ui/output_lifetime.h"
#include "source/ui/presentation.h"
#include "source/ui/font.h"
#include "source/runtime/guest_session.h"
#include "source/runtime/guest_execution.h"
#include "source/runtime/clock.h"
#include "source/cpu/interpreter.h"
#include "source/win32/shell_session.h"
#include "source/win32/presentation_session.h"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <vector>
namespace user=win32::user;
using Mode=ui::OutputPreference;
struct SoftwareDevice {
    ui::SoftwarePresentation backend;
    ui::Presentation presenter{&backend};
    std::vector<uint32_t> pixels;
    unsigned opens=0,closes=0;
    bool fail=false;
    bool Open(Mode) {
        ++opens;pixels.assign(1287*720,0xDEADBEEF);
        presenter.Attach(&backend);return !fail;
    }
    void Close(){++closes;presenter.Detach();pixels.clear();}
    bool Frame(const ui::Surface& s) {return presenter.Submit(s) && Replay();}
    bool Replay(){return presenter.Render({pixels.data(),1280,720,1287,pixels.size(),true,1});}
    void Check(const ui::Surface& source) {
        for(int y=0;y<720;++y)for(int x=0;x<1287;++x) {
            const uint32_t expected=x>=1280?0xDEADBEEF:0xFF000000u|ui::RgbToColorref(source.Get(x,y));
            assert(pixels[size_t(y)*1287+x]==expected);
        }
    }
};
static void DeviceFailures() {
    SoftwareDevice device;
    {ui::OutputLifetime<SoftwareDevice> output(device);
     device.fail=true;assert(!output.Ensure(Mode::GpuPreferred));assert(device.opens==1&&device.closes==1&&!output.Ready());
     device.fail=false;assert(output.Ensure(Mode::GpuPreferred));
     for(int i=0;i<30;++i)assert(output.Ensure(Mode::GpuPreferred));
     assert(device.opens==2&&device.closes==1); // GPU preference may actually use software fallback.
     assert(output.Ensure(Mode::Software));assert(device.opens==3&&device.closes==2);
     device.fail=true;assert(!output.Ensure(Mode::GpuPreferred));assert(device.opens==4&&device.closes==4&&!output.Ready());
     device.fail=false;assert(output.Ensure(Mode::GpuPreferred));assert(device.opens==5);}
    assert(device.closes==5 && device.pixels.empty());
}
struct Workspace {
    SoftwareDevice device;
    ui::OutputLifetime<SoftwareDevice> output{device};
    ui::DesktopShell shell;
    ui::OwnedSurface native;
    const char* exe;
    unsigned picks=0,runs=0,ends=0,returns=0,closeCalls=0;
    bool alive=true,shutdownGuest=false,shutdownPick=false;
    std::vector<bool> initial;
    explicit Workspace(const char* path,const char* root):exe(path) {
        std::string error;assert(shell.ConfigureFilesystem(root,"","",error));native.Allocate(1280,720);shell.Enter();
    }
    bool Alive(){return alive;}
    void NativeFrame() {shell.Render(native.s,u"12:45");assert(device.Frame(native.s));device.Check(native.s);}
    bool Pick(bool first) {
        initial.push_back(first);assert(!rt::g_process);assert(output.Ensure(Mode::GpuPreferred));
        NativeFrame();if(shutdownPick){alive=false;return true;}return picks++<4;
    }
    bool BeginGuest(){assert(output.Ensure(Mode::GpuPreferred));return true;}
    void RunGuest() {
        ++runs;
        {
            rt::GuestSession session;auto& p=session.ProcessObject();auto& memory=session.Memory();std::string error;
            user::PresentationSession platform(true,nullptr);user::EnableUi(1280,720);
            user::g_platform.wait=[](int,std::vector<user::InputEvent>&){return true;};
            user::ShellSession bridge(shell);assert(bridge.Attach("Persistent output probe"));
            if(runs==2){assert(!p.LoadMainExe("/definitely-missing/switch-recompiler.exe",error));return;}
            assert(p.LoadMainExe(exe,error)&&p.PrepareMainThread(p.MainThread().cpu,error));
            const auto stage=p.GetProcAddress(p.MainModule(),"BusyStage"),resume=p.GetProcAddress(p.MainModule(),"BusyResume"),finish=p.GetProcAddress(p.MainModule(),"BusyFinish");
            assert(stage&&resume&&finish);bool captured=false,completed=false;
            rt::GuestExecution execution(p,[&](rt::GuestThread& t){
                win32::HostCallOutcome result;if(win32::TryHandleHostCall(t.cpu,memory,result))return !result.requestExit;
                cpu::Interpreter cpu(memory,t.teb,t.teb);assert(cpu.Step(t.cpu)==cpu::StepResult::Continue);return true;
            },rt::GuestExecution::Startup::InitializeModules);
            for(unsigned i=0;i<500000;++i) {
                const auto result=execution.RunOne();assert(result!=rt::GuestExecution::Result::AllocationFailed);
                if(!captured&&memory.Read32(stage)==1) {
                    captured=true;assert(device.Frame(bridge.Compose(user::S().screen.s)));
                    assert(device.pixels[250*1287+400]==0xFF468C1E); // actual guest GDI pixels
                    if(runs==3||shutdownGuest){completed=true;if(shutdownGuest)alive=false;break;}
                    memory.Write32(resume,1);memory.Write32(finish,1);
                }
                if(result==rt::GuestExecution::Result::Stopped){assert(captured&&win32::g_guestExitCode==0);completed=true;break;}
                if(result==rt::GuestExecution::Result::Idle)rt::Clock::Sleep(1);
            }
            assert(completed);
        }
        // All guest windows/memory have disappeared. Renderer owns its snapshot.
        assert(!rt::g_process&&!shell.GuestActive());
        const auto owned=device.pixels;device.backend.Reset();assert(device.Replay()&&device.pixels==owned);
    }
    void EndGuest(){++ends;assert(!rt::g_process);}
    void ReturnToDesktop(){++returns;shell.Enter();NativeFrame();assert(device.opens==1&&device.closes==0);}
    void Close(){++closeCalls;output.Close();}
};
int main(int argc,char**argv) {
    assert(argc==2);ui::FontCache::SetDirectory("romfs/fonts");rt::Clock::SetFastForward(true);DeviceFailures();
    char name[]="/tmp/sr-output-XXXXXX";assert(mkdtemp(name));
    struct Cleanup{const char* path;~Cleanup(){std::filesystem::remove_all(path);}}cleanup{name};
    {Workspace f(argv[1],name);ui::RunWorkspace(f);
     assert(f.runs==4&&f.ends==4&&f.returns==4&&f.closeCalls==1);
     assert(f.device.opens==1&&f.device.closes==1);
     assert((f.initial==std::vector<bool>{true,false,false,false,false}));}
    {Workspace f(argv[1],name);f.shutdownGuest=true;ui::RunWorkspace(f);assert(f.runs==1&&f.ends==1&&!f.returns&&f.device.closes==1);}
    {Workspace f(argv[1],name);f.shutdownPick=true;ui::RunWorkspace(f);assert(!f.runs&&!f.ends&&f.device.closes==1);}
    std::puts("persistent workspace: one real output across x86 normal/load-error/abort/restart, owned redraw, full desktop pixels, mode/failure/shutdown cleanup PASS");
}
