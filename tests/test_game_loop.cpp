#include "source/runtime/guest_session.h"
#include "source/runtime/guest_execution.h"
#include "source/runtime/clock.h"
#include "source/cpu/interpreter.h"
#include "source/win32/user32.h"
#include "source/ui/presentation.h"
#include <cassert>
#include <algorithm>
#include <cstdio>
namespace u=win32::user;
namespace {
cpu::PagedGuestMemory* memory=nullptr;
uint32_t probe=0,seen=0,injected=0,frames=0,stages=0,minX=999,maxX=0;
bool abortRun=false;
ui::SoftwarePresentation renderer;
ui::Presentation output(&renderer);
std::vector<uint32_t> pixels(1280*720),lastPixels;
uint32_t Read(unsigned index){return memory->Read32(probe+index*4);}
void Key(std::vector<u::InputEvent>& events,uint32_t vk,bool down){u::InputEvent e;e.vk=vk;e.type=down?u::InputEvent::KeyDown:u::InputEvent::KeyUp;events.push_back(e);}
bool Input(int,std::vector<u::InputEvent>& events) {
    // Test clock progression is explicit, separate from guest timeGetTime calls.
    rt::Clock::Sleep(rt::Clock::IsFastForward()?4:1);
    const auto stage=Read(2);
    if(stage==8 && seen==8){memory->Write32(probe+32,1);return true;}
    if(stage<1 || stage>6 || stage==injected || seen<stage || (abortRun && stage>=3))return true;
    injected=stage;
    switch(stage) {
    case 1:Key(events,0x71,true);Key(events,0x71,false);break;
    case 2:Key(events,0x27,true);break;
    case 3:Key(events,0x27,false);break;
    case 4:Key(events,'Z',true);break;
    case 5:Key(events,'Z',false);break;
    case 6:Key(events,0x20,true);Key(events,0x20,false);break;
    }
    return true;
}
void Present(const ui::Surface& s) {
    const auto stage=Read(14);if(!stage)return;
    auto* w=u::Win(Read(1));if(!w)return;
    const auto x=Read(15),z=Read(16),timers=Read(17);
    assert(stage<=8 && x>=4 && x<=64 && z==(stage==5?1u:0u));
    if(stage==1)assert(x==4);
    if(stage==2)assert(x>=8 && x<12);
    if(stage>=3 && stage<=6)assert(x>=12);
    if(stage>=7)assert(x==8);
    if(stage==8)assert(timers>=8 && Read(7)==2 && Read(10)==4 && Read(11)==4);
    const int ox=u::ClientOriginX(w),oy=u::ClientOriginY(w);
    assert(output.Submit(s) && output.Render({pixels.data(),1280,720,1280,pixels.size(),true}));
    const auto viewport=ui::FitViewport(s.width,s.height,1280,720);
    // Independent complete reference of the authored 96x64 scene, not a hash
    // supplied by the guest or a comparison of a surface with its own copy.
    for(int cy=0;cy<64;++cy)for(int cx=0;cx<96;++cx) {
        uint32_t expected=0x182838;
        if(cy>=16 && cy<19 && cx>=int(x) && cx<int(x)+3)expected=stage==5?0x00ff00:0xff8000;
        if(cx==2 && cy==2)expected=timers&255;
        assert(s.Get(ox+cx,oy+cy)==expected);
        const int px=viewport.x+((ox+cx)*viewport.width+s.width-1)/s.width;
        const int py=viewport.y+((oy+cy)*viewport.height+s.height-1)/s.height;
        const uint32_t rgba=0xff000000u|((expected>>16)&255)|(expected&0xff00)|((expected&255)<<16);
        assert(pixels[size_t(py)*1280+px]==rgba);
    }
    minX=std::min(minX,x);maxX=std::max(maxX,x);stages|=1u<<stage;seen=stage;++frames;lastPixels=pixels;
}
}
int main(int argc,char** argv) {
    assert(argc==2);unsigned normal=0,aborts=0;
    for(unsigned mode:{0u,1u,2u,0u,3u,1u}) {
        const bool fast=mode==0 || mode==2;abortRun=mode>=2;
        seen=injected=frames=stages=maxX=0;minX=999;output.Attach(&renderer);
        rt::Clock::SetFastForward(fast);const auto saved=u::g_platform;
        {
            rt::GuestSession session;auto& p=session.ProcessObject();auto& m=session.Memory();std::string error;
            u::EnableUi(fast?800:1280,fast?600:720);
            assert(p.LoadMainExe(argv[1],error) && p.PrepareMainThread(p.MainThread().cpu,error));
            probe=p.GetProcAddress(p.MainModule(),"LoopProbe");assert(probe);memory=&m;m.Write32(probe,1);
            u::g_platform.wait=Input;u::g_platform.present=Present;
            bool stopped=false,parked=false;
            {
                rt::GuestExecution execution(p,[&](rt::GuestThread& thread){win32::HostCallOutcome out;
                    if(win32::TryHandleHostCall(thread.cpu,m,out))return !out.requestExit;
                    cpu::Interpreter cpu(m,thread.teb,thread.teb);const auto step=cpu.Step(thread.cpu);
                    assert(step==cpu::StepResult::Continue || step==cpu::StepResult::Halted);return true;
                },rt::GuestExecution::Startup::InitializeModules);
                for(unsigned n=0;n<500000;++n) {
                    const auto result=execution.RunOne();assert(result!=rt::GuestExecution::Result::AllocationFailed);
                    if(abortRun && seen==3){assert(u::S().asyncKeys[0x27]&0x80);parked=true;break;}
                    if(result==rt::GuestExecution::Result::Stopped){stopped=true;break;}
                    if(result==rt::GuestExecution::Result::Idle)rt::Clock::Sleep(1);
                }
            }
            if(abortRun){assert(parked && !stopped && !Read(9));++aborts;}
            else {
                if(!stopped || p.MainThread().exitCode || injected!=6 || (stages&0x17e)!=0x17e)fprintf(stderr,"Game loop mode%u stopped=%d stage=%u seen=%u timers=%u chars=%u injected=%u stages=%x exit/source-line=%u\n",mode,stopped,Read(2),seen,Read(5),Read(7),injected,stages,p.MainThread().exitCode);
                assert(stopped && !p.MainThread().exitCode && Read(9)==1 && injected==6 && (stages&0x17e)==0x17e);
                assert(Read(13)>=160 && Read(6)>=8 && maxX>=12 && minX==4);++normal;
            }
            assert(frames>=3 && !u::S().foreground && !u::S().capture);
            for(const auto& item:u::S().windows)assert(item.first==u::S().desktop);
            for(const auto& item:u::S().queues)assert(item.second.timers.empty());
            u::g_platform=saved;memory=nullptr;probe=0;
        }
        // The production presentation owns its pixels after guest/window/memory
        // destruction; force a full redraw into a different output allocation.
        std::vector<uint32_t> replay(1280*720);
        assert(output.Render({replay.data(),1280,720,1280,replay.size(),false}) && replay==lastPixels);
        output.Detach();
    }
    rt::Clock::SetFastForward(false);assert(normal==4 && aborts==2);
    puts("Game loop PASS (4 normal real-x86 sessions + 2 held-key loop aborts;real/fast-forward clocks,PeekMessage/timeGetTime/WM_TIMER,4 key pairs/2 chars,7 required visible stages (transient stage7 may coalesce),moving/held/released pixels,96x64 independent CPU+production output oracle at800/1280,owned redraw after teardown;no game/FPS/Switch acceptance)");
}
