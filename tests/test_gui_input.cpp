#include "source/runtime/guest_execution.h"
#include "source/runtime/clock.h"
#include "source/cpu/interpreter.h"
#include "source/win32/user32.h"
#include "source/ui/presentation.h"
#include <cassert>
#include <cstdio>
#include <cstring>

namespace user=win32::user;
static cpu::PagedGuestMemory* probeMemory;
static rt::Process* probeProcess;
static uint32_t probeStage,previousStage;
static unsigned injected,presented;
static ui::SoftwarePresentation renderer;
static ui::Presentation presentation(&renderer);
static std::vector<uint32_t> display(1280*720);
static bool pacedOutput;
static void AbortInput(bool guestExit) {
    cpu::PagedGuestMemory memory;rt::Process process(memory);rt::g_process=&process;
    assert(memory.Map(0x10000,0x10000,cpu::kProtRWX));
    auto& main=process.MainThread();rt::GuestThread peer;
    main.id=0x1004;peer.id=0x1008;main.teb=0x10000;peer.teb=0x11000;
    main.cpu.ESP()=0x18000;peer.cpu.ESP()=0x19000;
    main.status=peer.status=rt::GuestThreadStatus::Runnable;
    main.memoryRegions.emplace_back(main.teb,0x1000);peer.memoryRegions.emplace_back(peer.teb,0x1000);
    assert(process.Scheduler().Add(main)&&process.Scheduler().Add(peer));
    user::EnableUi(800,600);
    for(auto id:{main.id,peer.id}) {
        auto w=std::make_unique<user::Window>();w->hwnd=0x20000+id;w->threadId=id;
        auto& q=user::QueueFor(id);q.active=q.focus=w->hwnd;
        user::S().windows.emplace(w->hwnd,std::move(w));
    }
    user::S().foreground=0x20000+peer.id;user::S().capture=0x20000+main.id;
    user::S().mouseDownOwners[0]=main.id;user::S().mouseDownOwners[1]=peer.id;
    unsigned resumed=0;
    {
        rt::GuestExecution execution(process,[&](rt::GuestThread& thread) {
            process.Scheduler().Wait(thread,0xabcdef);rt::GuestExecution::Yield();
            assert(!rt::GuestExecution::Checkpoint());++resumed;return true;
        });
        assert(execution.RunOne()==rt::GuestExecution::Result::Ran);
        assert(execution.RunOne()==rt::GuestExecution::Result::Ran);
        if(guestExit){win32::g_guestExitRequested=true;win32::g_guestExitCode=49;}
    }
    assert(resumed==2 && main.memoryReleased && peer.memoryReleased);
    for(auto id:{main.id,peer.id}) {
        auto& q=user::QueueFor(id);assert(q.closed && !q.focus && !q.active);
        assert(!user::Win(0x20000+id));
    }
    assert(!user::S().foreground && !user::S().capture);
    for(auto owner:user::S().mouseDownOwners)assert(!owner);
    rt::g_process=nullptr;win32::g_guestExitRequested=false;win32::g_guestExitCode=0;
    user::S()=user::State{};
}
int main(int argc,char** argv) {
    assert(argc==2 || (argc==3 && std::strcmp(argv[2],"--paced")==0));
    const bool paced=argc==3;rt::Clock::SetFastForward(!paced);
    AbortInput(false);AbortInput(true);
    user::EnableUi(paced?1280:800,paced?720:600);
    user::S().inputFrames.Reset(true);
    cpu::PagedGuestMemory memory;rt::Process process(memory);rt::g_process=&process;
    std::string error;assert(process.LoadMainExe(argv[1],error));
    assert(process.PrepareMainThread(process.MainThread().cpu,error));
    uint32_t enabled=process.GetProcAddress(process.MainModule(),"InputTestProbe");
    uint32_t stage=process.GetProcAddress(process.MainModule(),"InputTestStage");assert(enabled&&stage);
    memory.Write32(enabled,1);probeMemory=&memory;probeProcess=&process;probeStage=stage;
    pacedOutput=paced;
    user::g_platform.present=[](const ui::Surface& surface) {
        assert(presentation.Submit(surface));
        assert(presentation.Render({display.data(),1280,720,1280,display.size(),true}));
        user::S().inputFrames.OutputQueued(ui::PerformanceNowNs(),ui::g_gdi.ScreenChangeCounter(),presentation.ContentSerial());
        ++presented;if(pacedOutput)rt::Clock::Sleep(16);
    };
    user::g_platform.wait=[](int,std::vector<user::InputEvent>& events) {
        uint32_t current=probeMemory->Read32(probeStage);
        if(!current || current==previousStage)return true;
        previousStage=current;++injected;
        assert(probeProcess->Scheduler().Current() && probeProcess->CurrentThreadId()!=probeProcess->MainThread().id);
        user::InputEvent event{};
        if(current<=2) {
            event.type=user::InputEvent::KeyDown;event.vk=current==1?'A':'B';events.push_back(event);
            event.type=user::InputEvent::KeyUp;events.push_back(event);
            event.type=user::InputEvent::Char;event.ch=current==1?'a':'b';events.push_back(event);
        } else if(current==3) {
            event.type=user::InputEvent::MouseMove;event.x=550;event.y=150;events.push_back(event);
            events.push_back(event); // one coalesced move, without duplicate delivery
        } else if(current==4) {
            // Begin in the capture owner's child, then drag across the peer.
            event.type=user::InputEvent::MouseDown;event.x=440;event.y=80;events.push_back(event);
            event.type=user::InputEvent::MouseMove;event.x=60;event.y=80;events.push_back(event);
            event.type=user::InputEvent::MouseUp;events.push_back(event);
        } else {
            assert(current==5);
            event.type=user::InputEvent::MouseDown;event.x=130;event.y=350;events.push_back(event);
            event.type=user::InputEvent::MouseUp;events.push_back(event);
        }
        return true;
    };
    bool stopped=false;
    {
        rt::GuestExecution execution(process,[&](rt::GuestThread& thread) {
            win32::HostCallOutcome result;
            if(win32::TryHandleHostCall(thread.cpu,memory,result))return !result.requestExit;
            cpu::Interpreter interpreter(memory,thread.teb,thread.teb);
            auto resultStep=interpreter.Step(thread.cpu);
            assert(resultStep==cpu::StepResult::Continue || resultStep==cpu::StepResult::Halted);return true;
        },rt::GuestExecution::Startup::InitializeModules);
        for(unsigned n=0;n<100000;++n) {
            auto result=execution.RunOne();assert(result!=rt::GuestExecution::Result::AllocationFailed);
            if(result==rt::GuestExecution::Result::Stopped){assert(win32::g_guestExitCode==0);stopped=true;break;}
            if(result==rt::GuestExecution::Result::Idle){assert(process.Scheduler().HasPendingTimers());rt::Clock::Sleep(1);}
        }
    }
    assert(stopped && injected==5 && !user::S().foreground && !user::S().capture);
    for(const auto& item:user::S().queues)assert(!item.second.active && !item.second.focus);
    for(const auto& item:user::S().windows)assert(item.first==user::S().desktop);
    assert(presented>0 && presentation.SourceWidth()==(paced?1280:800));
    assert(user::S().inputFrames.Stats().queueWait.count>0 && user::S().inputFrames.Stats().coalesced>0);
    // Redraw remains usable after guest shutdown; source is an owned snapshot.
    assert(presentation.Render({display.data(),1280,720,1280,display.size(),true},{0,0,true}));
    presentation.Detach();assert(!presentation.FrameBytes());
    user::S().inputFrames.Reset();
    user::g_platform.wait={};user::g_platform.present=nullptr;rt::g_process=nullptr;
    if(paced)puts("PASSED: 1280x720 real-clock GUI callbacks through software backend with paced presentation");
    puts("PASSED: foreground input routing from peer poll, capture drag/hover ownership, nested callbacks and host-abort/ExitProcess GUI cleanup");
}
