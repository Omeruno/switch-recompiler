#include "source/runtime/guest_session.h"
#include "source/runtime/guest_execution.h"
#include "source/cpu/interpreter.h"
#include "source/win32/host_functions.h"
#include "source/win32/wave_out.h"
#include "tests/audio_capture_device.h"
#include <cassert>
#include <cstdio>
int main(int argc,char** argv){
    assert(argc==2);AudioCaptureDevice device;assert(rt::Audio().Bind(&device));
    for(unsigned mode=0;mode<10;++mode){
        const auto accepts=rt::Audio().Accepted(),played=rt::Audio().Completed(),cancelled=rt::Audio().Cancelled();
        {
            rt::GuestSession session;auto& process=session.ProcessObject();auto& memory=session.Memory();std::string error;
            assert(process.LoadMainExe(argv[1],error) && process.PrepareMainThread(process.MainThread().cpu,error));
            const auto probe=process.GetProcAddress(process.MainModule(),"CallbackProbe");assert(probe);memory.Write32(probe,mode);
            bool stopped=false,parked=false;unsigned stage=0;
            {
                rt::GuestExecution execution(process,[&](rt::GuestThread& thread){
                    win32::HostCallOutcome call;if(win32::TryHandleHostCall(thread.cpu,memory,call))return !call.requestExit;
                    cpu::Interpreter cpu(memory,thread.teb,thread.teb);assert(cpu.Step(thread.cpu)==cpu::StepResult::Continue);return true;
                },rt::GuestExecution::Startup::InitializeModules);
                for(unsigned n=0;n<150000;++n){
                    const auto result=execution.RunOne();assert(result!=rt::GuestExecution::Result::AllocationFailed);
                    const auto next=memory.Read32(probe+12);
                    if(next && stage!=next){stage=next;
                        if(stage==1){assert(device.active.size()==4);auto second=device.active.begin();++second;device.released.push_back(second->first);}
                        else if(stage==4){assert(mode==4);}
                        else if(stage==3){assert(device.active.size()==1);device.released.push_back(device.active.begin()->first);}
                        else{
                            assert(stage==2 || stage==9);auto* worker=process.FindThread(memory.Read32(probe+28));assert(worker && worker->background && worker->teb==memory.Read32(probe+32) && worker->callbackDepth==1);
                            if(mode==8 && stage==2){
                                const auto flags=memory.Read32(memory.Read32(probe+8)+16);
                                {rt::GuestSession nested;win32::PollWaveOut();assert(memory.Read32(memory.Read32(probe+8)+16)==flags);}
                                assert(memory.Read32(memory.Read32(probe+8)+16)==flags);
                            }
                            if(stage==9){assert(mode==6 && !memory.Read32(probe+20));parked=true;break;}
                        }
                        if(stage!=4)memory.Write32(probe+16,stage);
                    }
                    if(stage==4){auto* worker=process.FindThread(memory.Read32(probe+28));
                        if(worker && worker->status==rt::GuestThreadStatus::Exited){assert(worker->exitCode==73);memory.Write32(probe+16,4);}}
                    if(result==rt::GuestExecution::Result::Stopped){stopped=true;break;}
                }
            }
            if(mode==6){assert(parked && !stopped && !memory.Read32(probe+24));}
            else{
                const auto code=uint32_t(win32::g_guestExitCode);
                if(!stopped || (code && mode!=3 && mode!=5))fprintf(stderr,"Callback fixture mode %u stopped=%d code=%u stage=%u\n",mode,stopped,code,stage);
                assert(stopped && code==(mode==3?37u:mode==5?55u:0u));
                assert(mode==3 || memory.Read32(probe+24)==1);
                if(mode!=1 && mode!=2){auto* worker=process.FindThread(memory.Read32(probe+28));assert(worker && worker->background && worker->status==rt::GuestThreadStatus::Exited && !worker->callbackDepth && worker->memoryReleased);}
            }
        }
        assert(device.active.empty() && !rt::Audio().Pending() && !rt::Audio().Client());
        const unsigned a=(mode==0 || mode==7 || mode==8)?5:(mode==1 || mode==2 || mode==6)?4:0;
        const unsigned p=(mode==0 || mode==7 || mode==8)?2:(mode==1 || mode==2 || mode==6)?1:0;
        assert(rt::Audio().Accepted()-accepts==a && rt::Audio().Completed()-played==p && rt::Audio().Cancelled()-cancelled==a-p);
    }
    assert(rt::Audio().Accepted()==27 && rt::Audio().Completed()==9 && rt::Audio().Cancelled()==18);assert(rt::Audio().Bind(nullptr));
    puts("Wave callbacks PASS (9 normal x86 process terminations + parked callback abort; real guest CPU/TEB/TLS/stdcall20/open-done-close,non-alertable worker wait,128-record backpressure,reopen,event rights/type/manual/auto/alias ownership,callback ExitProcess/ExitThread,last app ExitThread,nested session/drained native frames;27 accepted,9 played,18 cancelled)");
}
