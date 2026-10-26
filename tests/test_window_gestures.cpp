#include "source/runtime/guest_session.h"
#include "source/runtime/guest_execution.h"
#include "source/runtime/clock.h"
#include "source/cpu/interpreter.h"
#include "source/win32/user32.h"
#include <cassert>
#include <cstdio>
#include <functional>
static std::function<bool(int,std::vector<win32::user::InputEvent>&)> input;
namespace user=win32::user;
using Event=user::InputEvent;
static void Run(const char* path,bool batched,bool abort=false){
    rt::GuestSession session;auto& process=session.ProcessObject();auto& memory=session.Memory();
    user::EnableUi(1280,720);user::S().workBottom=680;
    std::string error;assert(process.LoadMainExe(path,error));assert(process.PrepareMainThread(process.MainThread().cpu,error));
    const auto probe=process.GetProcAddress(process.MainModule(),"GestureProbe");assert(probe);unsigned checked=0;bool parked=false;
    user::g_platform.wait=[](int timeout,std::vector<Event>& events){return input(timeout,events);};
    std::vector<Event> sequence;size_t next=0;
    input=[&](int,std::vector<Event>& events){
        if(abort && checked==1 && user::S().capture){parked=true;rt::GuestExecution::Yield();return true;}
        if(next<sequence.size()){events.push_back(sequence[next++]);return true;}
        sequence.clear();next=0;
        const auto stage=memory.Read32(probe);if(stage<=checked || stage>22)return true;
        assert(stage==checked+1);const auto hwnd=memory.Read32(probe+4);auto* w=user::Win(hwnd);assert(w);
        const auto r=user::ScreenRect(w);int x=r.right-2,y=r.bottom-2,dx=30,dy=20;
        if(stage==1){dx=70;dy=50;}
        if(stage==2 || stage==5 || stage==13){x=r.left+70;y=r.top+12;dx=40;dy=30;}
        if(stage==3){dx=-700;dy=-500;}
        if(stage==4){dx=900;dy=600;}
        if(stage>=14 && stage<=20){
            switch(stage-14){case 0:x=r.left+1;y=r.top+60;break;case 1:x=r.right-2;y=r.top+60;break;
            case 2:x=r.left+60;y=r.top+1;break;case 3:x=r.left+1;y=r.top+1;break;
            case 4:x=r.right-2;y=r.top+1;break;case 5:x=r.left+60;y=r.bottom-2;break;
            case 6:x=r.left+1;y=r.bottom-2;break;}
        }
        if(stage>=9 && stage<=12){
            const uint32_t hit=stage<=10?user::HTCLOSE:stage==11?user::HTMAXBUTTON:user::HTMINBUTTON;
            bool found=false;
            for(int yy=std::max(0,r.top);!found && yy<std::min(720,r.bottom);++yy)
                for(int xx=std::max(0,r.left);xx<std::min(1280,r.right);++xx)
                    if(user::DefWindowProc(hwnd,user::WM_NCHITTEST,0,(uint32_t(xx)&0xffff)|(uint32_t(yy)<<16),false)==hit){x=xx+1;y=yy+1;found=true;break;}
            assert(found);dx=stage==9?-100:0;dy=stage==9?80:0;
        }
        auto push=[&](Event::Type type,int px,int py,uint32_t vk=0){Event e;e.type=type;e.x=px;e.y=py;e.vk=vk;events.push_back(e);};
        push(Event::MouseDown,x,y);
        if(abort){}
        else if(stage==13){push(Event::MouseUp,x,y);push(Event::MouseDown,x,y);push(Event::MouseUp,x,y);}
        else {
            push(Event::MouseMove,std::max(0,std::min(x+dx,1279)),std::max(0,std::min(y+dy,719)));
            if(stage==5)push(Event::KeyDown,x,y,0x1b);
            push(Event::MouseUp,std::max(0,std::min(x+dx,1279)),std::max(0,std::min(y+dy,719)));
        }
        std::fprintf(stderr,"physical gesture stage=%u anchor=%d,%d delta=%d,%d\n",stage,x,y,dx,dy);
        if(!batched){sequence.swap(events);events.push_back(sequence[next++]);}
        memory.Write32(probe+28,stage);++checked;return true;
    };
    {rt::GuestExecution execution(process,[&](rt::GuestThread& t){
        win32::HostCallOutcome result;if(win32::TryHandleHostCall(t.cpu,memory,result))return !result.requestExit;
        cpu::Interpreter cpu(memory,t.teb,t.teb);assert(cpu.Step(t.cpu)==cpu::StepResult::Continue);return true;
    },rt::GuestExecution::Startup::InitializeModules);
    bool stopped=false;
    for(unsigned i=0;i<1000000;++i){auto result=execution.RunOne();assert(result!=rt::GuestExecution::Result::AllocationFailed);
        if(abort && parked){stopped=true;break;}
        if(result==rt::GuestExecution::Result::Stopped){std::printf("gesture stages=%u exit=%u\n",checked,process.MainThread().exitCode);assert(checked==22&&process.MainThread().exitCode==0);stopped=true;break;}
        if(result==rt::GuestExecution::Result::Idle)rt::Clock::Sleep(1);
    }
    assert(stopped);}
    assert(!user::S().capture);user::g_platform.wait={};input={};
}
int main(int argc,char** argv){assert(argc==2);rt::Clock::SetFastForward(true);Run(argv[1],false);Run(argv[1],true);Run(argv[1],false,true);
    puts("Window gestures: PASS (44 real-x86 stages + parked modal host abort, sequenced/batched physical caption/buttons/all eight edges, minmax/callback/escape/capture/destruction, fresh sessions)");}
