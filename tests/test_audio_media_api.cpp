#include "source/runtime/guest_session.h"
#include "source/runtime/guest_execution.h"
#include "source/runtime/clock.h"
#include "source/runtime/files.h"
#include "source/cpu/interpreter.h"
#include "source/win32/host_functions.h"
#include "source/win32/user32.h"
#include "tests/audio_capture_device.h"
#include <cassert>
#include <filesystem>
#include <cstdio>
namespace fs=std::filesystem;
int main(int argc,char**argv){
    assert(argc==3);char pattern[]="/tmp/sr-mci-XXXXXX";assert(mkdtemp(pattern));const fs::path root=pattern;
    struct Cleanup{fs::path p;~Cleanup(){fs::remove_all(p);}} cleanup{root};fs::copy_file(argv[2],root/"tone.mp3");
    AudioCaptureDevice device;assert(rt::Audio().Bind(&device));uint32_t stale=0;
    rt::Clock::SetFastForward(true);
    for(unsigned mode=0;mode<8;++mode){device.ready=mode!=7;device.submitted.clear();
        {
            rt::GuestSession session;auto& process=session.ProcessObject();auto& memory=session.Memory();std::string error;win32::user::EnableUi(800,600);
            assert(process.LoadMainExe(argv[1],error) && process.PrepareMainThread(process.MainThread().cpu,error));rt::FileSystem().SetRoot(root.string());
            const auto probe=process.GetProcAddress(process.MainModule(),"MediaProbe");assert(probe);memory.Write32(probe,mode);memory.Write32(probe+56,stale);
            const auto base=device.playedFrames;unsigned stage=0;bool stopped=false,parked=false;
            {
                rt::GuestExecution execution(process,[&](rt::GuestThread& thread){
                    win32::HostCallOutcome out;if(win32::TryHandleHostCall(thread.cpu,memory,out))return !out.requestExit;
                    cpu::Interpreter cpu(memory,thread.teb,thread.teb);assert(cpu.Step(thread.cpu)==cpu::StepResult::Continue);return true;
                },rt::GuestExecution::Startup::InitializeModules);
                for(unsigned n=0;n<200000;++n){
                    const auto result=execution.RunOne();assert(result!=rt::GuestExecution::Result::AllocationFailed);
                    const auto next=memory.Read32(probe+12);
                    if(mode==6 && next==9 && device.active.size()==4){assert(process.MainThread().status==rt::GuestThreadStatus::Waiting);parked=true;break;}
                    if(next && next!=stage){stage=next;
                        if(stage==1){assert(device.active.size()==(mode<4?2:4));device.playedFrames=base+(mode<4?240:2400);
                            const auto* pcm=static_cast<const int16_t*>(device.active.begin()->second->Data());if(mode==0 || mode==3)assert(pcm[0]==1000 && pcm[1]==-2000);
                        }else if(stage==2){assert(device.paused && device.active.size()==(mode<4?2:4));}
                        else if(stage==3 && mode<4){assert(!device.paused && device.active.size()==2);const auto first=device.active.begin();const auto frames=first->second->Bytes()/4;
                            device.playedFrames=base+frames;device.released.push_back(first->first);memory.Write32(probe+48,uint32_t(frames));
                        }else assert(stage==3 && mode>=4);
                        memory.Write32(probe+16,stage);
                    }
                    if(mode>=4 && mode!=6 && stage==3 && !device.paused && !device.active.empty()){
                        auto first=device.active.begin();device.playedFrames+=first->second->Bytes()/4;device.released.push_back(first->first);
                    }
                    if(result==rt::GuestExecution::Result::Stopped){stopped=true;break;}
                    if(result==rt::GuestExecution::Result::Idle)rt::Clock::Sleep(1);
                }
            }
            if(mode==6)assert(parked && !stopped && !memory.Read32(probe+24));
            else{if(!stopped || process.MainThread().exitCode)fprintf(stderr,"Media API mode%u stopped=%d code=%u stage=%u\n",mode,stopped,process.MainThread().exitCode,stage);
                assert(stopped && !process.MainThread().exitCode && memory.Read32(probe+24)==1);assert(device.active.empty() && !rt::Audio().Client());}
            if(mode==4 || mode==5 || mode==6)stale=memory.Read32(probe+4);
        }
        assert(device.active.empty() && !rt::Audio().Pending() && !rt::Audio().Client());
    }
    rt::Clock::SetFastForward(false);assert(rt::Audio().Bind(nullptr));
    puts("Audio media APIs PASS (7 normal x86 sessions + parked MCI_WAIT abort,real 22/44/48/96k mono/stereo PCM8/16,hardware-counter position/pause/restart/master volume,MCI A/W MP3 open/alias/caps/status/play/wait/notify/abort/seek/close,unsupported MIDI/no-device/stale IDs;hardware sound pending)");
}
