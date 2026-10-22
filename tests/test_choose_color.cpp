#include "source/runtime/guest_execution.h"
#include "source/runtime/clock.h"
#include "source/cpu/interpreter.h"
#include "source/win32/user32.h"
#include <cassert>
#include <cstdio>
#include <string>
namespace user=win32::user;
static bool parked=false;
static void Scenario(const char* path,bool guestExit) {
    user::EnableUi(1280,720);rt::Clock::SetFastForward(true);
    cpu::PagedGuestMemory memory;rt::Process process(memory);rt::g_process=&process;
    process.SetCommandLine(std::string(path)+(guestExit?" exit":" hostabort"));
    std::string error;assert(process.LoadMainExe(path,error));
    assert(process.PrepareMainThread(process.MainThread().cpu,error));
    user::g_platform.wait=[](int,std::vector<user::InputEvent>&) {
        bool dialog=false;for(const auto& item:user::S().windows)
            if(item.second->cls && item.second->cls->name==u"SwitchCommonColorDialog")dialog=true;
        assert(dialog);parked=true;rt::GuestExecution::Yield();return true;
    };
    parked=false;
    {
        rt::GuestExecution execution(process,[&](rt::GuestThread& thread) {
            win32::HostCallOutcome call;
            if(win32::TryHandleHostCall(thread.cpu,memory,call))return !call.requestExit;
            cpu::Interpreter interpreter(memory,thread.teb,thread.teb);
            auto step=interpreter.Step(thread.cpu);
            assert(step==cpu::StepResult::Continue || step==cpu::StepResult::Halted);return true;
        },rt::GuestExecution::Startup::InitializeModules);
        bool stopped=false;
        for(unsigned n=0;n<100000 && !parked;++n) {
            auto result=execution.RunOne();assert(result!=rt::GuestExecution::Result::AllocationFailed);
            if(result==rt::GuestExecution::Result::Stopped){stopped=true;break;}
        }
        assert(guestExit?stopped:parked);
        if(!guestExit) {
            auto probe=process.GetProcAddress(process.MainModule(),"ColorProbeStage");assert(probe);
            assert(memory.Read32(probe)==1);
        }
    }
    assert(!process.Scheduler().HasLiveThreads());
    assert(process.MainThread().exitCode==(guestExit?42u:0xc0000001u));
    for(const auto& item:user::S().windows)assert(item.first==user::S().desktop);
    assert(!user::S().foreground && !user::S().capture);
    for(const auto& item:user::S().queues)assert(!item.second.active && !item.second.focus);
    user::g_platform={};win32::g_guestExitRequested=false;win32::g_guestExitCode=0;rt::g_process=nullptr;
}
int main(int argc,char** argv) {
    assert(argc==2 || argc==3);
    // Delay-load must resolve even when ui2 is disabled: return an API error,
    // rather than allowing MFC to jump through a null function pointer.
    win32::EnableHostLayer(win32::kUiLayer,false);
    assert(win32::FindHostFunction("COMDLG32.dll","ChooseColorA")>=0);
    assert(win32::FindHostFunction("COMDLG32.dll","ChooseColorW")>=0);
    assert(win32::FindHostFunction("COMDLG32.dll","CommDlgExtendedError")>=0);
    Scenario(argv[1],argc==3 && std::string(argv[2])=="--exit");
    puts("PASSED: ChooseColor exports without ui2, real x86 hook ExitProcess and parked modal host-abort cleanup");
}
