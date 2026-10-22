#include "source/runtime/guest_execution.h"
#include "source/runtime/clock.h"
#include "source/cpu/interpreter.h"
#include "source/win32/user32.h"
#include "source/ui/presentation.h"
#include <cassert>
#include <cstdio>
#include <cstring>
namespace user=win32::user;
static ui::SoftwarePresentation software;
static ui::Presentation presentation(&software);
static std::vector<uint32_t> pixels(1280*720);
static unsigned frames;
static bool corners;
int main(int argc,char** argv) {
    assert(argc==2 || (argc==3 && std::strcmp(argv[2],"--classic")==0));
    const int width=argc==2?1280:800,height=argc==2?720:600;
    rt::Clock::SetFastForward(true);user::EnableUi(width,height);
    user::g_platform.present=[](const ui::Surface& surface) {
        assert(presentation.Submit(surface));
        assert(presentation.Render({pixels.data(),1280,720,1280,pixels.size()}));++frames;
        auto v=ui::FitViewport(surface.width,surface.height,1280,720);
        auto sample=[&](int x,int y) {return pixels[size_t(v.y+y*v.height/surface.height)*1280+v.x+x*v.width/surface.width];};
        if(sample(20,20)==0xFF0000FFu && sample(surface.width-20,20)==0xFF00FF00u &&
           sample(20,surface.height-20)==0xFFFF0000u && sample(surface.width-20,surface.height-20)==0xFFFFFFFFu)corners=true;
    };
    cpu::PagedGuestMemory memory;rt::Process process(memory);rt::g_process=&process;
    std::string error;assert(process.LoadMainExe(argv[1],error));
    assert(process.PrepareMainThread(process.MainThread().cpu,error));
    bool stopped=false;
    {
        rt::GuestExecution execution(process,[&](rt::GuestThread& thread) {
            win32::HostCallOutcome outcome;
            if(win32::TryHandleHostCall(thread.cpu,memory,outcome))return !outcome.requestExit;
            cpu::Interpreter interpreter(memory,thread.teb,thread.teb);
            auto result=interpreter.Step(thread.cpu);
            assert(result==cpu::StepResult::Continue || result==cpu::StepResult::Halted);return true;
        },rt::GuestExecution::Startup::InitializeModules);
        for(unsigned i=0;i<1000000;++i) {
            const auto result=execution.RunOne();assert(result!=rt::GuestExecution::Result::AllocationFailed);
            if(result==rt::GuestExecution::Result::Stopped) {stopped=true;break;}
            if(result==rt::GuestExecution::Result::Idle) {assert(process.Scheduler().HasPendingTimers());rt::Clock::Sleep(1);}
        }
    }
    assert(stopped && win32::g_guestExitCode==0 && frames>0 && corners && presentation.SourceWidth()==width);
    presentation.Detach();user::g_platform.present=nullptr;rt::g_process=nullptr;
    std::printf("PASSED: real x86 GDI/metrics/desktop %dx%d through software output, %u frames, all four corners\n",width,height,frames);
}
