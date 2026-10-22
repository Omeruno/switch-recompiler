// Host inspection is necessary: a guest cannot check its DLL notifications
// after ExitProcess. The fixture contains only redistributable MinGW sources.
#include "source/runtime/guest_execution.h"
#include "source/runtime/clock.h"
#include "source/cpu/interpreter.h"
#include "source/win32/host_functions.h"
#include <cassert>
#include <cstddef>
#include <cstdio>

// PE32 layout of tests/guest/lifecycle_shared.h (HANDLE is 32 bits).
struct LifeState32 {
    uint32_t count,failure,busyTid,lateAttaches;
    uint32_t events[256][6];
    uint32_t exitTest,exitMode,exitOwner,exitBlocked,exitSuspended,exitFinished;
};
static void Scenario(const char* path,uint32_t mode) {
    cpu::PagedGuestMemory memory;
    rt::Process process(memory); rt::g_process=&process;
    std::string error;
    assert(process.LoadMainExe(path,error));
    assert(process.PrepareMainThread(process.MainThread().cpu,error));
    assert(process.InitializeModules(process.MainThread().cpu,error));
    auto* dep=process.FindByName("lifecycle_dep.dll"); assert(dep);
    uint32_t state=process.GetProcAddress(dep,"state"); assert(state);
    memory.Write32(state+offsetof(LifeState32,exitMode),mode);
    bool completed=false;
    {
        rt::GuestExecution execution(process,[&](rt::GuestThread& thread) {
            win32::HostCallOutcome result;
            if(win32::TryHandleHostCall(thread.cpu,memory,result)) return !result.requestExit;
            cpu::Interpreter interpreter(memory,thread.teb,thread.teb);
            auto step=interpreter.Step(thread.cpu);
            assert(step==cpu::StepResult::Continue || step==cpu::StepResult::Halted);
            return true;
        });
        for(unsigned n=0;n<30000;++n) {
            auto result=execution.RunOne();
            assert(result!=rt::GuestExecution::Result::AllocationFailed);
            if(mode==3 && memory.Read32(state+offsetof(LifeState32,exitTest))) {
                completed=true; break; // abort without any guest process detach
            }
            if(result==rt::GuestExecution::Result::Stopped) {
                assert(win32::g_guestExitRequested && win32::g_guestExitCode==0);
                completed=true; break;
            }
            if(result==rt::GuestExecution::Result::Idle) {
                assert(process.Scheduler().HasPendingTimers()); rt::Clock::Sleep(1);
            }
        }
    }
    assert(completed && !process.Scheduler().HasLiveThreads());
    uint32_t failure=memory.Read32(state+offsetof(LifeState32,failure));
    if(failure) fprintf(stderr,"process detach mode=%u flags=%x events=%u\n",mode,failure,memory.Read32(state));
    assert(!failure);
    uint32_t owner=memory.Read32(state+offsetof(LifeState32,exitOwner));
    uint32_t blocked=memory.Read32(state+offsetof(LifeState32,exitBlocked));
    uint32_t suspended=memory.Read32(state+offsetof(LifeState32,exitSuspended));
    auto peerId=[&](uint32_t handle) {
        auto object=process.Handles().Get(handle); assert(object && object->type==rt::ObjectKind::Thread);
        return static_cast<rt::ThreadObject&>(*object).threadId;
    };
    const uint32_t blockedId=peerId(blocked),suspendedId=peerId(suspended);
    const uint32_t expected[]={32,51,22,11,12};
    unsigned detached=0;
    uint32_t count=memory.Read32(state); assert(count<=256);
    for(uint32_t i=0;i<count;++i) {
        uint32_t event=state+offsetof(LifeState32,events)+i*24;
        uint32_t module=memory.Read32(event),kind=memory.Read32(event+4);
        uint32_t reason=memory.Read32(event+8),tid=memory.Read32(event+12);
        if(reason==3) assert(tid!=blockedId && tid!=suspendedId);
        if(reason!=0) continue;
        assert(mode!=3 && detached<5 && module*10+kind==expected[detached++]);
        assert(tid==owner && memory.Read32(event+20));
        if(module==1) assert(memory.Read32(event+16)==(owner^1));
        if(module==5) assert(memory.Read32(event+16)==(mode==1?84u:42u));
    }
    if(detached!=(mode==3?0u:5u)) fprintf(stderr,"process detach mode=%u notifications=%u events=%u\n",mode,detached,count);
    assert(detached==(mode==3?0u:5u));
    assert(mode!=1 || owner!=process.MainThread().id);
    for(auto& object:process.Handles().Objects()) if(object->type==rt::ObjectKind::Thread) {
        auto* thread=process.FindThread(static_cast<rt::ThreadObject&>(*object).threadId);
        assert(thread && thread->memoryReleased && thread->pendingApcs.empty());
        assert(thread->waitHandles.empty() && !thread->waitObject);
    }
    assert(process.MainThread().memoryReleased && memory.IsMapped(process.PebAddress()));
    assert(!process.Scheduler().HasPendingTimers());
    win32::g_guestExitRequested=false; win32::g_guestExitCode=0; rt::g_process=nullptr;
}
int main(int argc,char** argv) {
    assert(argc==2);rt::Clock::SetFastForward(true);
    for(uint32_t mode=0;mode<4;++mode) Scenario(argv[1],mode);
    puts("PASSED: main/worker APC and CRT process detach order, reserved/TLS/loader, terminated peers, disabled/failed DLLs and host-abort isolation");
}
