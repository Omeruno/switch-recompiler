#include "source/runtime/guest_session.h"
#include "source/runtime/guest_execution.h"
#include "source/cpu/interpreter.h"
#include "source/win32/host_functions.h"
#include <cassert>
#include <cstdio>
int main(int argc,char**argv){
    assert(argc==2);uint32_t stale=0;unsigned normal=0,aborted=0;
    for(unsigned mode=0;mode<6;++mode){
        rt::GuestSession session;auto& process=session.ProcessObject();auto& memory=session.Memory();std::string error;
        win32::EnableHostLayer(win32::kUiLayer,(mode&1)==0);
        assert(process.LoadMainExe(argv[1],error) && process.PrepareMainThread(process.MainThread().cpu,error));
        const auto probe=process.GetProcAddress(process.MainModule(),"MmioProbe");assert(probe);memory.Write32(probe,stale);memory.Write32(probe+12,mode>=4);
        bool stopped=false,parked=false;
        {
            rt::GuestExecution execution(process,[&](rt::GuestThread& t){win32::HostCallOutcome call;
                if(win32::TryHandleHostCall(t.cpu,memory,call))return !call.requestExit;
                cpu::Interpreter cpu(memory,t.teb,t.teb);const auto r=cpu.Step(t.cpu);assert(r==cpu::StepResult::Continue || r==cpu::StepResult::Halted);return true;
            },rt::GuestExecution::Startup::InitializeModules);
            for(unsigned n=0;n<500000;++n){const auto r=execution.RunOne();assert(r!=rt::GuestExecution::Result::AllocationFailed);
                if(mode>=4 && memory.Read32(probe+8)){parked=true;break;}
                if(r==rt::GuestExecution::Result::Stopped){stopped=true;break;}}
        }
        if(mode<4){if(process.MainThread().exitCode)fprintf(stderr,"MMIO fixture mode%u failed line%u\n",mode,process.MainThread().exitCode);
            assert(stopped && !process.MainThread().exitCode);++normal;}else {assert(parked && !stopped);++aborted;}
        assert(memory.Read32(probe+8));stale=memory.Read32(probe+4);assert(stale);
    }
    assert(normal==4 && aborted==2);
    puts("MMIO read PASS (4 normal new/legacy x86 sessions +2 owning-handle aborts;real file bytes,RIFF/LIST/chunk bounds,odd padding,read/EOF/ascending,A/W paths,invalid/stale handles,malformed length/parent/output refusal,Process ownership;not full multimedia I/O or hardware/game acceptance)");
}
