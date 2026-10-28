#include "source/runtime/guest_session.h"
#include "source/runtime/guest_execution.h"
#include "source/cpu/interpreter.h"
#include "source/win32/host_functions.h"

#include <cassert>
#include <cstdio>
#include "tests/audio_capture_device.h"
int main(int argc,char** argv){
    assert(argc==2);AudioCaptureDevice device;assert(rt::Audio().Bind(&device));
    for(unsigned mode=0;mode<4;++mode){
        device.ready=mode!=0;device.submitFails=mode==2;device.submitted.clear();
        rt::GuestSession session;auto& process=session.ProcessObject();auto& memory=session.Memory();std::string error;
        assert(process.LoadMainExe(argv[1],error) && process.PrepareMainThread(process.MainThread().cpu,error));
        const auto probe=process.GetProcAddress(process.MainModule(),"AudioProbe");assert(probe);memory.Write32(probe,mode);bool stopped=false,released=false;
        {
            rt::GuestExecution execution(process,[&](rt::GuestThread& t){
                win32::HostCallOutcome call;if(win32::TryHandleHostCall(t.cpu,memory,call))return !call.requestExit;
                cpu::Interpreter cpu(memory,t.teb,t.teb);const auto r=cpu.Step(t.cpu);assert(r==cpu::StepResult::Continue || r==cpu::StepResult::Halted);return true;
            },rt::GuestExecution::Startup::InitializeModules);
            for(unsigned n=0;n<500000;++n){
                const auto r=execution.RunOne();assert(r!=rt::GuestExecution::Result::AllocationFailed);
                if(memory.Read32(probe+4) && !released){
                    assert(device.active.size()==1 && device.submitted.size()==4);
                    for(const auto& entry:device.active){const auto* p=static_cast<const int16_t*>(entry.second->Data());assert(p[0]==1000 && p[1]==-2000 && p[2]==3000 && p[3]==-4000);}
                    auto completed=device.active.begin();device.released.push_back(completed->first);released=true;memory.Write32(probe+8,1);
                }
                if(r==rt::GuestExecution::Result::Stopped){stopped=true;break;}
            }
        }
        if(process.MainThread().exitCode)fprintf(stderr,"Audio fixture mode%u failed source line %u\n",mode,process.MainThread().exitCode);
        assert(stopped && !process.MainThread().exitCode && memory.Read32(probe+12)==1);
        assert(device.active.empty() && !rt::Audio().Pending());
        if(mode==1 || mode==3){assert(device.submitted.size()==5 && device.submitted[0]==std::vector<int16_t>({1000,-2000,3000,-4000}));assert(device.submitted.back()[0]==775 && device.submitted.back()[1]==-2000);}
        else assert(device.submitted.empty());
    }
    assert(rt::Audio().Accepted()==10 && rt::Audio().Completed()==2 && rt::Audio().Cancelled()==8);
    assert(rt::Audio().Bind(nullptr));
    puts("Audio APIs PASS (4 normal x86 sessions,no-device/capture/submit-error/restart,actual resource+memory PCM snapshots,stereo,replace/NOSTOP/release/stop,real caps/query and unsupported callback-mode refusal)");
}
