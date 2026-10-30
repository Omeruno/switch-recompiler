#include "source/runtime/guest_session.h"
#include "source/runtime/guest_execution.h"
#include "source/cpu/interpreter.h"
#include "source/win32/host_functions.h"
#include "source/win32/user32.h"
#include "tests/audio_capture_device.h"
#include <cassert>
#include <cstdio>
struct TeardownProbe{cpu::IGuestMemory* memory=nullptr;uint32_t headers=0;~TeardownProbe(){if(memory)for(unsigned n=0;n<2;++n)assert((memory->Read32(headers+n*32+16)&17)==1);}};
static int A(size_t n,unsigned channel){return channel?-2000-int(n%13):1000+int(n%17);}
static int B(size_t n){return int((n*22050/48000)%7)*256;}
static void Check(const AudioCaptureDevice& device,size_t a,size_t b,bool first,uint16_t left=65535,uint16_t right=65535){
    assert(!device.active.empty());const auto& block=device.active.begin()->second;const auto* pcm=static_cast<const int16_t*>(block->Data());
    for(size_t n=0;n<block->Bytes()/4;++n)for(unsigned ch=0;ch<2;++ch){int expected=B(b+n);if(first && a+n<512)expected+=A(a+n,ch)*int(ch?right:left)/65535;assert(pcm[n*2+ch]==expected);}
}
int main(int argc,char** argv){
    assert(argc==2);AudioCaptureDevice device;assert(rt::Audio().Bind(&device));uint32_t stale=0;
    unsigned normal=0;for(unsigned mode=0;mode<11;++mode){
        device.submitted.clear();device.resetFails=device.submitFails=false;device.controlFails=mode==8;device.ready=mode!=9;
        {
            rt::GuestSession session;auto& p=session.ProcessObject();auto& m=session.Memory();std::string error;win32::user::EnableUi(800,600);
            assert(p.LoadMainExe(argv[1],error) && p.PrepareMainThread(p.MainThread().cpu,error));
            const auto probe=p.GetProcAddress(p.MainModule(),"MixerProbe");assert(probe);m.Write32(probe,mode);m.Write32(probe+40,stale);
            const auto base=device.playedFrames;unsigned stage=0;bool stopped=false,parked=false;
            {
                rt::GuestExecution execution(p,[&](rt::GuestThread& thread){win32::HostCallOutcome out;if(win32::TryHandleHostCall(thread.cpu,m,out))return !out.requestExit;
                    cpu::Interpreter cpu(m,thread.teb,thread.teb);const auto step=cpu.Step(thread.cpu);assert(step==cpu::StepResult::Continue || step==cpu::StepResult::Halted);return true;
                },rt::GuestExecution::Startup::InitializeModules);
                for(unsigned n=0;n<1000000;++n){
                    const auto result=execution.RunOne();assert(result!=rt::GuestExecution::Result::AllocationFailed);const auto next=m.Read32(probe+4);
                    if(next && next!=stage){stage=next;
                        if(stage==1 || stage==9){assert(device.active.size()==4);Check(device,0,0,true);if(mode==4)device.resetFails=true;else if(mode!=5)device.playedFrames=base+100;
                            if(mode==5){auto& check=p.LocalState<TeardownProbe>();check.memory=&m;check.headers=m.Read32(probe+24);}}
                        else if(stage==2){Check(device,100,100,false);device.playedFrames=base+300;}
                        else if(stage==3){Check(device,100,300,true,32768,0);device.playedFrames=base+337;}
                        else if(stage==4){assert((m.Read32(m.Read32(probe+24)+16)&17)==1);Check(device,0,337,false);}
                        else if(stage==11){assert(device.active.size()==4 && !(m.Read32(m.Read32(probe+24)+16)&1));device.resetFails=false;
                            const auto header=m.Read32(probe+24),oldFlags=m.Read32(header+16);{rt::GuestSession nested;win32::user::EnableUi(320,240);assert(m.Read32(header+16)==oldFlags);}assert(m.Read32(header+16)==oldFlags);}
                        else if(stage==12){assert(device.active.empty());device.submitFails=true;}
                        else if(stage==13){assert(device.active.size()==1);const auto* pcm=static_cast<const int16_t*>(device.active.begin()->second->Data());assert(pcm[0]==A(0,0));device.resetFails=true;}
                        else if(stage==14){assert(device.active.size()==1);device.resetFails=device.submitFails=false;}
                        else {assert(stage==10 && mode==6 && device.active.size()==4);const auto* pcm=static_cast<const int16_t*>(device.active.begin()->second->Data());assert(pcm[0]==1652 && pcm[1]==-2400);}
                        m.Write32(probe+8,stage);
                    }
                    if((stage==4 || stage==10) && !device.active.empty()){
                        const auto first=device.active.begin();device.playedFrames+=first->second->Bytes()/4;device.released.push_back(first->first);
                    }
                    if(mode==10 && stage==4 && m.Read32(probe+44) && (m.Read32(m.Read32(probe+24)+32+16)&1)){auto* worker=p.FindThread(m.Read32(probe+36));assert(worker && worker->background && worker->status==rt::GuestThreadStatus::Waiting && worker->callbackDepth==1);parked=true;break;}
                    if(mode==5 && stage==9 && p.MainThread().status==rt::GuestThreadStatus::Waiting){parked=true;break;}
                    if(result==rt::GuestExecution::Result::Stopped){stopped=true;break;}
                }
            }
            if(mode==5 || mode==10)assert(parked && !stopped && !m.Read32(probe+12));
            else{
                if(!stopped || p.MainThread().exitCode)fprintf(stderr,"Mixed fixture mode%u stopped=%d stage=%u exit/source-line=%u\n",mode,stopped,stage,p.MainThread().exitCode);
                assert(stopped && !p.MainThread().exitCode && m.Read32(probe+12)==1);++normal;
            }
            stale=m.Read32(probe+16);
        }
        assert(device.active.empty() && !rt::Audio().Pending() && !rt::Audio().Client());
    }
    device.ready=true;device.controlFails=false;assert(normal==9 && rt::Audio().Bind(nullptr));
    puts("Mixed waveOut PASS (9 normal x86 sessions + main/function-callback parked aborts;device1 real8-source mixing,48k stereo16/22k mono8,partial positions/pause/stereo gain/reset/peer completion,5-second header chunking,no premature DONE,function/event/thread/window callbacks,reset+ambiguous-append retention/recovery,nested owner,stale handles/teardown;mapper/direct retained,hardware pending)");
}
