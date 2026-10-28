#include "source/runtime/guest_session.h"
#include "source/runtime/guest_execution.h"
#include "source/cpu/interpreter.h"
#include "source/win32/host_functions.h"
#include "source/win32/user32.h"
#include <cassert>
#include <cstdio>
namespace w=win32;
namespace u=win32::user;
static bool injected;
static cpu::PagedGuestMemory* probeMemory;
static uint32_t probeAddress;
static bool Input(int,std::vector<u::InputEvent>& events) {
    if(injected || !probeMemory->Read32(probeAddress))return true;
    injected=true;
    const uint32_t keys[19]={'A','A','A',0xa3,0xa3,0x25,0x25,0x64,0x64,0x10,0x10,0xa5,0xa5,0x0d,0x0d,0xa0,0xa1,0xa0,0xa1};
    for(unsigned n=0;n<19;++n){u::InputEvent e;e.vk=keys[n];e.type=(n==2 || n==4 || n==6 || n==8 || n==10 || n==12 || n==14 || n==17 || n==18)?u::InputEvent::KeyUp:u::InputEvent::KeyDown;
        if(n==9 || n==10)e.scanCode=0x36;
        if(n==13 || n==14)e.scanCode=0xe01c;
        events.push_back(e);
    }
    return true;
}
int main(int argc,char** argv){
    assert(argc==2);
    for(unsigned mode=0;mode<4;++mode){
        rt::GuestSession session;auto& process=session.ProcessObject();auto& memory=session.Memory();std::string error;
        u::EnableUi(800,600);injected=false;u::g_platform.wait=Input;
        assert(process.LoadMainExe(argv[1],error) && process.PrepareMainThread(process.MainThread().cpu,error));
        const auto probe=process.GetProcAddress(process.MainModule(),"KeyboardProbe");assert(probe);probeMemory=&memory;probeAddress=probe;bool stopped=false;
        {
            rt::GuestExecution execution(process,[&](rt::GuestThread& t){
                w::HostCallOutcome call;if(w::TryHandleHostCall(t.cpu,memory,call))return !call.requestExit;
                cpu::Interpreter cpu(memory,t.teb,t.teb);const auto r=cpu.Step(t.cpu);assert(r==cpu::StepResult::Continue || r==cpu::StepResult::Halted);return true;
            },rt::GuestExecution::Startup::InitializeModules);
            for(unsigned n=0;n<500000;++n){const auto r=execution.RunOne();assert(r!=rt::GuestExecution::Result::AllocationFailed);if(r==rt::GuestExecution::Result::Stopped){stopped=true;break;}}
        }
        if(process.MainThread().exitCode)fprintf(stderr,"Keyboard fixture failed source line %u\n",process.MainThread().exitCode);
        assert(stopped && !process.MainThread().exitCode && memory.Read32(probe+4)==1);
        u::g_platform.wait=nullptr;
    }
    puts("Keyboard APIs PASS (4 normal x86 sessions, real A/W map/names/bounds/errors, USER scans/E0/repeat/sided modifiers/PM_NOREMOVE)");
}
