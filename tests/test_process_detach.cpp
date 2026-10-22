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
static void Scenario(const char* path,uint32_t mode,bool lastThread=false) {
    cpu::PagedGuestMemory memory;
    rt::Process process(memory); rt::g_process=&process;
    std::string error;
    assert(process.LoadMainExe(path,error));
    assert(process.PrepareMainThread(process.MainThread().cpu,error));
    assert(process.InitializeModules(process.MainThread().cpu,error));
    auto* dep=process.FindByName("lifecycle_dep.dll"); assert(dep);
    uint32_t state=process.GetProcAddress(dep,"state"); assert(state);
    memory.Write32(state+offsetof(LifeState32,exitMode),mode);
    fprintf(stderr,"INSPECT begin last=%d mode=%u\n",lastThread,mode);
    bool completed=false,resumed=false,loaderParked=false;
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
            if(lastThread && mode==4) {
                auto* worker=process.FindThread(memory.Read32(state+offsetof(LifeState32,exitOwner)));
                if(worker && worker->status==rt::GuestThreadStatus::Waiting && worker->waitObject==0xfffffff1u)
                    loaderParked=true;
            }
            if(lastThread && mode==5 && !resumed &&
               process.MainThread().status==rt::GuestThreadStatus::Exited) {
                // A suspended peer keeps the process alive. Resume it only
                // after proving no premature process detach occurred.
                assert(!win32::g_guestExitRequested && process.MainThread().exitCode==73);
                assert(process.MainThread().memoryReleased);
                for(uint32_t i=0;i<memory.Read32(state);++i) {
                    uint32_t event=state+offsetof(LifeState32,events)+i*24;
                    if(memory.Read32(event+8)==0)
                        assert(memory.Read32(event)==6 && memory.Read32(event+4)==2 && !memory.Read32(event+20));
                }
                auto* worker=process.FindThread(memory.Read32(state+offsetof(LifeState32,exitOwner)));
                assert(worker && worker->status==rt::GuestThreadStatus::Suspended && !worker->memoryReleased);
                // Use the actual Win32 wrapper: scheduler Resume only changes
                // the count; ResumeThread also makes CREATE_SUSPENDED runnable.
                uint32_t handle=0;
                for(auto& object:process.Handles().Objects())
                    if(object->type==rt::ObjectKind::Thread &&
                       static_cast<rt::ThreadObject&>(*object).threadId==worker->id)
                        handle=process.Handles().Aliases(object.get()).front();
                assert(handle);
                cpu::CpuState local=worker->cpu;local.ESP()=worker->stackBase-0x100;
                memory.Write32(local.ESP(),0x1234);memory.Write32(local.ESP()+4,handle);
                local.eip=win32::IndexToHostAddress(win32::FindHostFunction("KERNEL32.dll","ResumeThread"));
                auto* saved=process.Scheduler().Current();process.Scheduler().SetCurrent(worker);
                win32::HostCallOutcome outcome;
                assert(win32::TryHandleHostCall(local,memory,outcome) && outcome.returnValue==1);
                process.Scheduler().SetCurrent(saved);
                assert(worker->IsRunnable());resumed=true;
            }
            if(!lastThread && mode==3 && memory.Read32(state+offsetof(LifeState32,exitTest))) {
                completed=true; break; // abort without any guest process detach
            }
            if(result==rt::GuestExecution::Result::Stopped) {
                assert(win32::g_guestExitRequested && win32::g_guestExitCode==(lastThread?55:0));
                completed=true; break;
            }
            if(result==rt::GuestExecution::Result::Idle) {
                assert(process.Scheduler().HasPendingTimers()); rt::Clock::Sleep(1);
            }
        }
        if(!completed) {
            fprintf(stderr,"INSPECT incomplete last=%d mode=%u owner=%x flags=%x events=%u loaderParked=%d\n",
                lastThread,mode,memory.Read32(state+offsetof(LifeState32,exitOwner)),
                memory.Read32(state+offsetof(LifeState32,failure)),memory.Read32(state),loaderParked);
            for(uint32_t id=0x1004;id<0x1040;id+=4) if(auto* thread=process.FindThread(id))
                fprintf(stderr,"thread=%x status=%u eip=%x wait=%x sleep=%d exitReq=%d detaching=%d detachAborted=%d\n",
                    id,unsigned(thread->status),thread->cpu.eip,thread->waitObject,thread->sleeping,
                    thread->exitRequested,thread->detaching,thread->detachAborted);
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
    unsigned detached=0,mainDetached=0,failedDetached=0;
    uint32_t count=memory.Read32(state); assert(count<=256);
    for(uint32_t i=0;i<count;++i) {
        uint32_t event=state+offsetof(LifeState32,events)+i*24;
        uint32_t module=memory.Read32(event),kind=memory.Read32(event+4);
        uint32_t reason=memory.Read32(event+8),tid=memory.Read32(event+12);
        if(reason==3) {
            if(lastThread) assert(tid!=owner);
            else assert(tid!=blockedId && tid!=suspendedId);
            if(tid==process.MainThread().id) { assert(!detached);++mainDetached; }
        }
        if(reason!=0) continue;
        if(module==6) {
            // A rejected dynamic PROCESS_ATTACH now gets its own immediate
            // failed-load detach (reserved=0), outside final process shutdown.
            assert(kind==2 && !memory.Read32(event+20) && !detached);++failedDetached;continue;
        }
        assert((lastThread || mode!=3) && detached<5 && module*10+kind==expected[detached++]);
        assert(tid==owner && memory.Read32(event+20));
        if(module==1) assert(memory.Read32(event+16)==(owner^1));
        if(module==5) assert(memory.Read32(event+16)==((lastThread?mode!=0:mode==1)?84u:42u));
    }
    if(detached==0 && (lastThread || mode!=3)) fprintf(stderr,"process detach last=%d mode=%u notifications=%u events=%u\n",lastThread,mode,detached,count);
    assert(detached==(!lastThread && mode==3?0u:5u));
    assert(failedDetached==1);
    assert(!(lastThread?mode!=0:mode==1) || owner!=process.MainThread().id);
    assert(!lastThread || mode!=5 || resumed);
    assert(!lastThread || mode!=4 || loaderParked);
    assert(!lastThread || mainDetached==(mode?4u:0u));
    for(auto& object:process.Handles().Objects()) if(object->type==rt::ObjectKind::Thread) {
        auto* thread=process.FindThread(static_cast<rt::ThreadObject&>(*object).threadId);
        assert(thread && thread->memoryReleased && thread->pendingApcs.empty());
        assert(thread->waitHandles.empty() && !thread->waitObject);
        if(lastThread) assert(thread->exitCode==(thread->id==owner?55u:73u));
    }
    assert(process.MainThread().memoryReleased && memory.IsMapped(process.PebAddress()));
    assert(!process.Scheduler().HasPendingTimers());
    if(lastThread) {
        unsigned mutexes=0;
        for(auto& object:process.Handles().Objects()) if(object->type==rt::ObjectKind::Mutex) {
            auto& mutex=static_cast<rt::SyncObject&>(*object);
            assert(mutex.abandoned && !mutex.ownerThread && !mutex.recursion);++mutexes;
        }
        assert(mutexes);
    }
    win32::g_guestExitRequested=false; win32::g_guestExitCode=0; rt::g_process=nullptr;
}
int main(int argc,char** argv) {
    assert(argc==3);rt::Clock::SetFastForward(true);
    for(uint32_t mode=0;mode<4;++mode) Scenario(argv[1],mode);
    for(uint32_t mode=0;mode<6;++mode) Scenario(argv[2],mode,true);
    puts("PASSED: process detach (4 exit/abort + 6 last-thread cases), loader-lock recheck, suspended-peer lifetime, TLS/order/codes/APC/timer/mutex cleanup");
}
