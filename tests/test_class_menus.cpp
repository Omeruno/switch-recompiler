#include "source/runtime/guest_session.h"
#include "source/runtime/guest_execution.h"
#include "source/runtime/clock.h"
#include "source/cpu/interpreter.h"
#include "source/win32/user32.h"
#include <cassert>
#include <cstdio>
namespace u=win32::user;
static cpu::PagedGuestMemory* memory;
static uint32_t probe;
static bool injected;
static bool Input(int,std::vector<u::InputEvent>& events) {
    rt::Clock::Sleep(4);
    if(injected || memory->Read32(probe+4)!=1)return true;
    bool popup=false;for(const auto& entry:u::S().windows)if(entry.second->cls && entry.second->cls->name==u"#32768")popup=true;
    if(!popup)return true;
    injected=true;
    for(const auto key:{0x28u,0x0du})for(bool down:{true,false}){u::InputEvent event;event.type=down?u::InputEvent::KeyDown:u::InputEvent::KeyUp;event.vk=key;events.push_back(event);}
    return true;
}
int main(int argc,char** argv) {
    assert(argc==2);unsigned normal=0,aborts=0;rt::Clock::SetFastForward(true);
    for(unsigned mode:{0u,0u,1u,0u,2u,0u}) {
        rt::GuestSession session;auto& p=session.ProcessObject();auto& m=session.Memory();std::string error;u::EnableUi(800,600);
        assert(u::MenuObjectCount()==0 && p.LoadMainExe(argv[1],error) && p.PrepareMainThread(p.MainThread().cpu,error));
        probe=p.GetProcAddress(p.MainModule(),"MenuProbe");assert(probe);memory=&m;m.Write32(probe,mode);injected=false;
        const auto saved=u::g_platform;u::g_platform.wait=Input;bool stopped=false,parked=false;
        {
            rt::GuestExecution execution(p,[&](rt::GuestThread& thread){win32::HostCallOutcome call;
                if(win32::TryHandleHostCall(thread.cpu,m,call))return !call.requestExit;
                cpu::Interpreter cpu(m,thread.teb,thread.teb);const auto result=cpu.Step(thread.cpu);
                assert(result==cpu::StepResult::Continue || result==cpu::StepResult::Halted);return true;
            },rt::GuestExecution::Startup::InitializeModules);
            for(unsigned n=0;n<500000;++n) {
                const auto result=execution.RunOne();assert(result!=rt::GuestExecution::Result::AllocationFailed);
                if(mode && m.Read32(probe+4)==9 && p.MainThread().status==rt::GuestThreadStatus::Waiting){assert(u::MenuObjectCount()>0);parked=true;break;}
                if(result==rt::GuestExecution::Result::Stopped){stopped=true;break;}
                if(result==rt::GuestExecution::Result::Idle)rt::Clock::Sleep(1);
            }
        }
        if(mode){assert(parked && !stopped && !m.Read32(probe+16));++aborts;}
        else {
            if(!stopped || p.MainThread().exitCode)fprintf(stderr,"Class menus mode%u stopped=%d source-line=%u stage=%u injected=%d menus=%zu\n",mode,stopped,p.MainThread().exitCode,m.Read32(probe+4),injected,u::MenuObjectCount());
            assert(stopped && !p.MainThread().exitCode && m.Read32(probe+16)==1 && injected && m.Read32(probe+12)==1);++normal;
        }
        assert(u::MenuObjectCount()==0);for(const auto& w:u::S().windows)assert(w.first==u::S().desktop);
        u::g_platform=saved;memory=nullptr;
    }
    rt::Clock::SetFastForward(false);assert(normal==4 && aborts==2);
    puts("Class menus PASS (4 normal x86 sessions + WM_CREATE/popup callback aborts;copied A/W class resource names,numeric/MENUEX/DLL resources,default/override/child-ID,before-create visibility,independent trees,SetClassLongW changes,real keyboard menu WM_COMMAND,detach/destroy/failure/unload/session lifetime;no game/Switch acceptance)");
}
