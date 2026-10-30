#include "source/runtime/guest_session.h"
#include "source/runtime/guest_execution.h"
#include "source/runtime/resources.h"
#include "source/runtime/clock.h"
#include "source/cpu/interpreter.h"
#include "source/win32/host_functions.h"
#include "source/win32/user32.h"
#include <cassert>
#include <cstdio>
int main(int argc,char** argv){
    assert(argc==2);unsigned normal=0;uint32_t registered=0;rt::Clock::SetFastForward(true);
    for(unsigned mode:{0u,1u,0u,1u,2u}){
        rt::GuestSession session;auto& p=session.ProcessObject();auto& m=session.Memory();std::string error;
        if(mode!=1)win32::user::EnableUi(800,600);else win32::EnableHostLayer(win32::kUiLayer,false);
        assert(p.LoadMainExe(argv[1],error) && p.PrepareMainThread(p.MainThread().cpu,error));
        const auto probe=p.GetProcAddress(p.MainModule(),"ResourceProbe");assert(probe);m.Write32(probe,mode);
        bool stopped=false,parked=false;unsigned stage=0;
        {
            rt::GuestExecution execution(p,[&](rt::GuestThread& thread){win32::HostCallOutcome out;if(win32::TryHandleHostCall(thread.cpu,m,out))return !out.requestExit;
                cpu::Interpreter cpu(m,thread.teb,thread.teb);const auto step=cpu.Step(thread.cpu);assert(step==cpu::StepResult::Continue || step==cpu::StepResult::Halted);return true;
            },rt::GuestExecution::Startup::InitializeModules);
            for(unsigned n=0;n<500000;++n){
                const auto result=execution.RunOne();assert(result!=rt::GuestExecution::Result::AllocationFailed);
                const auto next=m.Read32(probe+4);
                if(next && next!=stage){stage=next;const auto ptr=m.Read32(probe+20),module=m.Read32(probe+16);
                    if(stage==1 || stage==9){assert(p.FindByHandle(module) && m.IsRangeMapped(ptr,30) && m.Read16(ptr)=='M');}
                    else{assert(stage==2 && !p.FindByHandle(module) && !m.IsRangeMapped(ptr,30));assert(m.Read8(m.Read32(probe+24))=='M');}
                    m.Write32(probe+8,stage);
                }
                if(mode==2 && stage==9 && p.MainThread().status==rt::GuestThreadStatus::Waiting){parked=true;break;}
                if(result==rt::GuestExecution::Result::Idle)rt::Clock::Sleep(1);
                if(result==rt::GuestExecution::Result::Stopped){stopped=true;break;}
            }
        }
        if(!registered)registered=m.Read32(probe+28);else assert(registered==m.Read32(probe+28));
        if(mode==2)assert(parked && !stopped && !m.Read32(probe+12));
        else{if(!stopped || p.MainThread().exitCode)fprintf(stderr,"Palette/strings mode%u stopped=%d stage=%u exit/source-line=%u\n",mode,stopped,stage,p.MainThread().exitCode);
            assert(stopped && !p.MainThread().exitCode && m.Read32(probe+12)==1);++normal;}
    }
    assert(normal==4);rt::Clock::SetFastForward(false);
    puts("Palette/string APIs PASS (4 normal new/legacy x86 sessions + parked DLL-pointer abort;actual TrueColor/no system palette/pixels/indexed logical DIB,shared LoadString A/W CP1252/truncation/empty/embedded-NUL/guards,EXE+DLL resources/unload/copy lifetime;no Pinball or hardware acceptance)");
}
