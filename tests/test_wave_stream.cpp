#include "source/runtime/guest_session.h"
#include "source/runtime/guest_execution.h"
#include "source/cpu/interpreter.h"
#include "source/win32/host_functions.h"
#include "source/win32/user32.h"
#include "tests/audio_capture_device.h"
#include <cassert>
#include <cstdio>
namespace {
struct TeardownProbe {
    cpu::IGuestMemory* memory=nullptr;uint32_t header=0;
    ~TeardownProbe(){if(memory){assert(memory->IsRangeMapped(header,32));const auto flags=memory->Read32(header+16);assert((flags&1) && !(flags&16));}}
};
}
int main(int argc,char** argv){
    assert(argc==2);AudioCaptureDevice device;assert(rt::Audio().Bind(&device));uint32_t stale=0;
    for(unsigned mode=0;mode<8;++mode){
        device.submitted.clear();device.resetFails=false;device.submitFails=mode==7;
        {
            rt::GuestSession session;auto& process=session.ProcessObject();auto& memory=session.Memory();std::string error;win32::user::EnableUi(800,600);
            assert(process.LoadMainExe(argv[1],error) && process.PrepareMainThread(process.MainThread().cpu,error));
            const auto probe=process.GetProcAddress(process.MainModule(),"WaveProbe");assert(probe);memory.Write32(probe,mode);memory.Write32(probe+28,stale);
            unsigned previousStage=0;bool stopped=false,parked=false;
            {
                rt::GuestExecution execution(process,[&](rt::GuestThread& t){
                    win32::HostCallOutcome call;if(win32::TryHandleHostCall(t.cpu,memory,call))return !call.requestExit;
                    cpu::Interpreter cpu(memory,t.teb,t.teb);const auto r=cpu.Step(t.cpu);assert(r==cpu::StepResult::Continue || r==cpu::StepResult::Halted);return true;
                },rt::GuestExecution::Startup::InitializeModules);
                for(unsigned n=0;n<500000;++n){
                    const auto r=execution.RunOne();assert(r!=rt::GuestExecution::Result::AllocationFailed);
                    const auto stage=memory.Read32(probe+16);
                    if(stage && stage!=previousStage){
                        previousStage=stage;
                        if(stage==1 || stage==9){
                            assert(device.active.size()==4 && device.submitted.size()==4);
                            const auto& block=device.active.begin()->second;const auto* samples=static_cast<const int16_t*>(block->Data());assert(samples[0]==1000 && samples[1]==-2000);
                            const auto header=memory.Read32(probe+8);assert(memory.Read16(memory.Read32(header))==777);
                            assert(memory.Read32(header+16)==18 && rt::Audio().Client()==memory.Read32(probe+4));
                            if(stage==9){auto& check=process.LocalState<TeardownProbe>();check.memory=&memory;check.header=header;parked=true;break;}
                            auto second=device.active.begin();++second;device.released.push_back(second->first);
                        }else if(stage==2){assert(device.active.size()==4);device.resetFails=true;}
                        else if(stage==3){assert(device.active.size()==4);device.resetFails=false;}
                        else if(stage==7){assert(device.active.size()==1 && device.submitted.empty());device.resetFails=true;}
                        else if(stage==8){assert(device.active.size()==1);device.resetFails=false;device.submitFails=false;}
                        else if(stage==6){
                            const auto header=memory.Read32(probe+8);const auto client=rt::Audio().Client();assert(device.active.size()==4 && memory.Read32(header+16)==18);
                            {rt::GuestSession nested;win32::user::EnableUi(320,240);assert(!rt::Audio().Client() && device.active.empty());rt::Audio().Poll();assert(memory.Read32(header+16)==18);}
                            assert(rt::Audio().Client()==client && memory.Read32(header+16)==18);
                        }
                        else if(stage==5){assert(device.active.size()==1);memory.Unmap(memory.Read32(probe+8),4096);assert(!memory.IsRangeMapped(memory.Read32(probe+8),32));device.released.push_back(device.active.begin()->first);}
                        else{assert(stage==4 && device.active.size()==1 && device.submitted.back()[0]==(mode==6?123:mode==7?1000:777));device.released.push_back(device.active.begin()->first);}
                        memory.Write32(probe+20,stage);
                    }
                    if(r==rt::GuestExecution::Result::Stopped){stopped=true;break;}
                }
            }
            if(mode==4){assert(parked && !stopped && device.active.size()==4 && !memory.Read32(probe+24));}
            else{
                if(process.MainThread().exitCode)fprintf(stderr,"Wave fixture mode%u failed source line %u\n",mode,process.MainThread().exitCode);
                assert(stopped && !process.MainThread().exitCode && memory.Read32(probe+24)==1);
                assert(device.active.empty() && !rt::Audio().Pending() && !rt::Audio().Client());
                assert(device.submitted.size()==((mode==5 || mode==7)?1:mode==6?5:6));
            }
            stale=memory.Read32(probe+4);
        }
        assert(device.active.empty() && !rt::Audio().Pending() && !rt::Audio().Client());
    }
    assert(rt::Audio().Accepted()==35 && rt::Audio().Completed()==11 && rt::Audio().Cancelled()==24);
    assert(rt::Audio().Bind(nullptr));
    puts("Wave streaming PASS (7 normal x86 sessions + parked abort/unmapped-header/nested-owner/ambiguous-submit cases,real handles/64-bounded preparation/PCM snapshot/4 queued buffers,release before DONE,window/thread callbacks,reset fault/confirmed cancel/reuse/stale handles/exclusive PlaySound/teardown;35 accepted,11 played,24 cancelled)");
}
