#include "source/runtime/guest_execution.h"
#include "source/runtime/clock.h"
#include "source/cpu/interpreter.h"
#include "source/win32/host_functions.h"
#include <cassert>
#include <cstddef>
#include <cstdio>

// PE32 layout shared with source-only lifecycle fixtures; HANDLE is 32 bits.
struct State32 {
    uint32_t count,failure,busy,late,events[256][6];
    uint32_t exitTest,exitMode,owner,blocked,suspended,finished,gate;
    uint32_t mode,entry,worker,afterExit,wait,ready,thread,suspendedThread,timer,mutex;
};
static void Scenario(const char* path,uint32_t mode) {
    cpu::PagedGuestMemory memory;rt::Process process(memory);rt::g_process=&process;
    std::string error;
    assert(process.LoadMainExe(path,error));
    assert(process.PrepareMainThread(process.MainThread().cpu,error));
    auto* dep=process.FindByName("lifecycle_dep.dll");assert(dep);
    auto* startup=process.FindByName("lifecycle_startup.dll");assert(startup);
    uint32_t state=process.GetProcAddress(dep,"state");assert(state);
    memory.Write32(state+offsetof(State32,mode),mode);
    const auto read=[&](size_t offset) { return memory.Read32(state+uint32_t(offset)); };
    auto findHandleThread=[&](uint32_t handle) {
        auto object=process.Handles().Get(handle);assert(object && object->type==rt::ObjectKind::Thread);
        return process.FindThread(static_cast<rt::ThreadObject&>(*object).threadId);
    };
    const uint32_t codes[]={0,0xc0000142u,37,38,0xc0000001u,39};
    bool complete=false,loaderParked=false;
    {
        rt::GuestExecution execution(process,[&](rt::GuestThread& thread) {
            win32::HostCallOutcome result;
            if(win32::TryHandleHostCall(thread.cpu,memory,result))return !result.requestExit;
            cpu::Interpreter interpreter(memory,thread.teb,thread.teb);
            auto step=interpreter.Step(thread.cpu);
            assert(step==cpu::StepResult::Continue || step==cpu::StepResult::Halted);
            return true;
        },rt::GuestExecution::Startup::InitializeModules);
        for(unsigned n=0;n<30000;++n) {
            auto result=execution.RunOne();assert(result!=rt::GuestExecution::Result::AllocationFailed);
            if(read(offsetof(State32,thread))) {
                auto* worker=findHandleThread(read(offsetof(State32,thread)));assert(worker);
                if(worker->status==rt::GuestThreadStatus::Waiting && worker->waitObject==0xfffffff1u)
                    loaderParked=true;
            }
            if(mode==4 && startup->initializing && read(offsetof(State32,wait))==0x7fffffff &&
               process.MainThread().status==rt::GuestThreadStatus::Waiting) {
                assert(!execution.InitializationFailed() && !win32::g_guestExitRequested);
                complete=true;break;
            }
            if(result==rt::GuestExecution::Result::Stopped) {
                assert(win32::g_guestExitRequested && uint32_t(win32::g_guestExitCode)==codes[mode]);
                assert(execution.InitializationFailed()==(mode==1));
                if(mode==1)assert(execution.InitializationError().find("lifecycle_startup.dll")!=std::string::npos);
                complete=true;break;
            }
            if(result==rt::GuestExecution::Result::Idle) {
                assert(process.Scheduler().HasPendingTimers());rt::Clock::Sleep(1);
            }
        }
        if(!complete)fprintf(stderr,"startup incomplete mode=%u error=%s count=%u flags=%x\n",
            mode,execution.InitializationError().c_str(),read(0),read(offsetof(State32,failure)));
    }
    assert(complete && !process.Scheduler().HasLiveThreads());
    assert(read(offsetof(State32,failure))==0 && !read(offsetof(State32,afterExit)));
    assert(read(offsetof(State32,entry))==(mode==0?1u:0u));
    assert(read(offsetof(State32,worker))==(mode==0?1u:0u));
    assert(read(offsetof(State32,wait))==(mode==4?0x7fffffffu:0x102u));
    assert(mode==4 || loaderParked);
    const uint32_t normal[]={91,81,82,11,12},failed[]={81,82,11,12},partial[]={11,12};
    const uint32_t* expected=mode==0?normal:((mode==1 || mode==5)?failed:partial);
    const unsigned expectedCount=mode==4?0:(mode==0?5:((mode==1 || mode==5)?4:2));
    unsigned detached=0;
    assert(read(0)<=256);
    for(uint32_t i=0;i<read(0);++i) {
        uint32_t event=state+offsetof(State32,events)+i*24;
        uint32_t module=memory.Read32(event),kind=memory.Read32(event+4),reason=memory.Read32(event+8);
        if(reason==3)assert(mode==0 && memory.Read32(event+12)!=process.MainThread().id);
        if(reason!=0)continue;
        assert(detached<expectedCount && module*10+kind==expected[detached++]);
        assert(memory.Read32(event+12)==process.MainThread().id);
        bool failedLoad=(module==8 && (mode==1 || mode==5));
        assert((memory.Read32(event+20)!=0)==!failedLoad);
        assert(memory.Read32(event+16)==(process.MainThread().id^module));
    }
    assert(detached==expectedCount && !startup->initializing);
    assert(startup->initialized==(mode==0));
    assert(startup->initializationFailed==(mode==1 || mode==5));
    assert(startup->processDetached==(mode==0 || mode==1 || mode==5));
    assert(process.MainModule()->initialized==(mode==0));
    assert(dep->processDetached==(mode!=4));
    assert(process.MainThread().memoryReleased && process.MainThread().exitCode==codes[mode]);
    auto* worker=findHandleThread(read(offsetof(State32,thread)));
    auto* suspended=findHandleThread(read(offsetof(State32,suspendedThread)));
    assert(worker && suspended && worker->memoryReleased && suspended->memoryReleased);
    assert(worker->pendingApcs.empty() && !worker->waitObject && suspended->pendingApcs.empty());
    assert(worker->exitCode==(mode==0?17u:codes[mode]) && suspended->exitCode==codes[mode]);
    assert(!process.Scheduler().HasPendingTimers() && memory.IsMapped(process.PebAddress()));
    auto mutex=process.Handles().Get(read(offsetof(State32,mutex)));assert(mutex && mutex->type==rt::ObjectKind::Mutex);
    auto& sync=static_cast<rt::SyncObject&>(*mutex);assert(sync.abandoned && !sync.ownerThread && !sync.recursion);
    win32::g_guestExitRequested=false;win32::g_guestExitCode=0;rt::g_process=nullptr;
}
int main(int argc,char** argv) {
    assert(argc==2);rt::Clock::SetFastForward(true);
    for(uint32_t mode=0;mode<6;++mode)Scenario(argv[1],mode);
    puts("PASSED: 6 cooperative startup modes, serialized child, failed-attach detach, partial-module exit/abort, status/TLS/order/resource cleanup");
}
