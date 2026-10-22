#include "source/runtime/guest_execution.h"
#include "source/runtime/waitable_timer.h"
#include "source/win32/host_functions.h"
#include "source/ui/presentation.h"
#include "source/win32/presentation_session.h"
#include <cassert>
#include <cstdio>

static ui::Presentation* livePresentation=nullptr;
static unsigned presentationCleanup=0;
static void FinishPresentation() {
    assert(livePresentation && livePresentation->FrameBytes());
    livePresentation->Detach();++presentationCleanup;
}

struct StopGuard {
    cpu::PagedGuestMemory& memory;
    rt::GuestThread& thread;
    unsigned& cleaned;
    ~StopGuard() {
        if(livePresentation)assert(livePresentation->FrameBytes());
        assert(memory.IsMapped(thread.teb));
        assert(!thread.memoryReleased);
        ++cleaned;
    }
};

static void Scenario(bool guestExit) {
    cpu::PagedGuestMemory memory;
    assert(memory.Map(0x10000,0x10000,cpu::kProtRWX));
    rt::Process process(memory); rt::g_process=&process;
    auto& main=process.MainThread(); rt::GuestThread suspended,exited;
    main.id=0x1004; main.teb=0x10000; main.cpu.ESP()=0x18000;
    main.memoryRegions.emplace_back(0x10000,0x1000);
    main.status=rt::GuestThreadStatus::Runnable;
    suspended.id=0x1008; suspended.status=rt::GuestThreadStatus::Suspended; suspended.suspendCount=1;
    exited.id=0x100c; exited.status=rt::GuestThreadStatus::Runnable;
    assert(process.Scheduler().Add(main) && process.Scheduler().Add(suspended) && process.Scheduler().Add(exited));
    process.Scheduler().Exit(exited,73);
    auto threadObject=std::make_shared<rt::ThreadObject>(main.id);
    uint32_t handle=process.Handles().Add(threadObject);
    auto mutex=std::make_shared<rt::SyncObject>(rt::ObjectKind::Mutex);
    mutex->ownerThread=main.id; mutex->recursion=2; process.Handles().Add(mutex);
    auto plain=std::make_shared<rt::TimerObject>();
    auto callback=std::make_shared<rt::TimerObject>();
    process.Handles().Add(plain); process.Handles().Add(callback);
    unsigned cleaned=0;
    ui::SoftwarePresentation software;
    ui::Presentation presentation(&software);
    ui::OwnedSurface source;source.Allocate(64,64);
    std::vector<uint32_t> output(64*64);
    assert(presentation.Submit(source.s));
    assert(presentation.Render({output.data(),64,64,64,output.size(),true,1}));
    livePresentation=&presentation;const auto cleanupBefore=presentationCleanup;
    const auto oldPresent=win32::user::g_platform.present;
    {
        win32::user::PresentationSession session(true,&FinishPresentation);
        win32::user::g_platform.present=nullptr;
        rt::GuestExecution execution(process,[&](rt::GuestThread& thread) {
            StopGuard guard{memory,thread,cleaned};
            process.Scheduler().Wait(thread,0xabcdef);
            rt::GuestExecution::Yield();
            assert(!rt::GuestExecution::Checkpoint());
            return true;
        });
        assert(execution.RunOne()==rt::GuestExecution::Result::Ran);
        assert(main.status==rt::GuestThreadStatus::Waiting && !cleaned);
        main.retryWaitToken=0xabcdef; main.retryWaitDeadlineUs=12345;
        main.pendingApcs.push_back({0x123456,1});
        plain->active=callback->active=true;
        plain->signaled=callback->signaled=true;
        plain->dueUs=callback->dueUs=~uint64_t(0);
        callback->routine=0x123456; callback->callbackThread=main.id; callback->apcQueued=true;
        main.alertableWait=true;
        if (guestExit) {
            // Exercise the actual HLE process-exit request, with a parked peer.
            cpu::CpuState cpu=main.cpu;
            memory.Write32(cpu.ESP(),0x1234); memory.Write32(cpu.ESP()+4,42);
            cpu.eip=win32::IndexToHostAddress(win32::FindHostFunction("KERNEL32.dll","ExitProcess"));
            win32::HostCallOutcome result;
            assert(win32::TryHandleHostCall(cpu,memory,result));
            assert(win32::g_guestExitRequested && win32::g_guestExitCode==42);
        }
    }
    assert(presentationCleanup==cleanupBefore+1);
    assert(!presentation.FrameBytes() && !presentation.TrackingBytes() && !software.CacheBytes());
    assert(win32::user::g_platform.present==oldPresent);
    livePresentation=nullptr;
    assert(cleaned==1);
    assert(!process.Scheduler().HasLiveThreads());
    assert(main.exitCode==(guestExit?42u:0xc0000001u));
    assert(suspended.status==rt::GuestThreadStatus::Exited && suspended.exitCode==main.exitCode);
    assert(exited.exitCode==73);
    assert(!main.waitObject && main.waitHandles.empty() && !main.retryWaitToken && !main.retryWaitDeadlineUs);
    assert(main.pendingApcs.empty() && !main.alertableWait && !suspended.suspendCount);
    assert(main.memoryReleased && !memory.IsMapped(main.teb));
    assert(!plain->active && !callback->active && !callback->apcQueued && !callback->routine);
    assert(plain->signaled && callback->signaled);
    assert(!process.Scheduler().HasPendingTimers());
    assert(mutex->abandoned && mutex->signaled && !mutex->ownerThread && !mutex->recursion);
    assert(process.Handles().Get(handle)==threadObject);
    assert(win32::g_guestExitRequested==guestExit && (!guestExit || win32::g_guestExitCode==42));
    win32::g_guestExitRequested=false; win32::g_guestExitCode=0; rt::g_process=nullptr;
}
static void ScheduledScenario(bool nested,bool viaReturn) {
    // Request exit on a worker while another native continuation is parked.
    // RunOne must finish cancellation before publishing process completion.
    cpu::PagedGuestMemory memory;
    assert(memory.Map(0x10000,0x10000,cpu::kProtRWX));
    rt::Process process(memory); rt::g_process=&process;
    auto& main=process.MainThread(); rt::GuestThread worker;
    main.id=0x1004; main.teb=0x10000; main.cpu.ESP()=0x18000;
    worker.id=0x1008; worker.teb=0x11000; worker.cpu.ESP()=0x19000;
    main.status=worker.status=rt::GuestThreadStatus::Runnable;
    main.memoryRegions.emplace_back(main.teb,0x1000);
    worker.memoryRegions.emplace_back(worker.teb,0x1000);
    assert(process.Scheduler().Add(main) && process.Scheduler().Add(worker));
    unsigned cleaned=0;
    const auto requestExit=[&](rt::GuestThread& thread) {
        memory.Write32(thread.cpu.ESP(),0x1234); memory.Write32(thread.cpu.ESP()+4,47);
        thread.cpu.Get(cpu::Reg32::EAX)=47;
        thread.cpu.eip=viaReturn ? rt::EntryReturnAddress() :
            win32::IndexToHostAddress(win32::FindHostFunction("KERNEL32.dll","ExitProcess"));
        win32::HostCallOutcome result;
        assert(win32::TryHandleHostCall(thread.cpu,memory,result));
    };
    {
        rt::GuestExecution execution(process,[&](rt::GuestThread& thread) {
            StopGuard guard{memory,thread,cleaned};
            if (&thread==&main) {
                process.Scheduler().Wait(thread,0xabcdef);
                if (nested) {
                    {
                        rt::GuestExecution inner(process,[&](rt::GuestThread& peer) {
                            StopGuard guard{memory,peer,cleaned};
                            assert(&peer==&worker); requestExit(peer); return false;
                        });
                        assert(inner.RunOne()==rt::GuestExecution::Result::Stopped);
                    }
                    assert(memory.IsMapped(main.teb) && !main.memoryReleased);
                    return false;
                }
                rt::GuestExecution::Yield();
                assert(!rt::GuestExecution::Checkpoint());
                return true;
            }
            requestExit(thread);
            return false;
        });
        auto first=execution.RunOne();
        assert(first==(nested?rt::GuestExecution::Result::Stopped:rt::GuestExecution::Result::Ran));
        for (unsigned n=0;n<8;++n)
            if(execution.RunOne()==rt::GuestExecution::Result::Stopped) break;
        assert(!process.Scheduler().HasLiveThreads() && cleaned==2);
        assert(main.memoryReleased && worker.memoryReleased);
        assert(main.exitCode==47 && worker.exitCode==47);
        assert(win32::g_guestExitRequested && win32::g_guestExitCode==47);
    }
    win32::g_guestExitRequested=false; win32::g_guestExitCode=0; rt::g_process=nullptr;
}
static void LastThreadScenario(bool workerLast,bool suspendedPeer) {
    cpu::PagedGuestMemory memory;
    assert(memory.Map(0x10000,0x10000,cpu::kProtRWX));
    rt::Process process(memory);rt::g_process=&process;
    auto& main=process.MainThread();rt::GuestThread worker;
    main.id=0x1004;main.teb=0x10000;main.cpu.ESP()=0x18000;
    main.status=rt::GuestThreadStatus::Runnable;
    main.memoryRegions.emplace_back(main.teb,0x1000);
    assert(process.Scheduler().Add(main));
    worker.id=0x1008;worker.teb=0x11000;worker.cpu.ESP()=0x19000;
    if(workerLast || suspendedPeer) {
        worker.status=suspendedPeer?rt::GuestThreadStatus::Suspended:rt::GuestThreadStatus::Runnable;
        worker.suspendCount=suspendedPeer?1:0;
        assert(process.Scheduler().Add(worker));
    }
    if(workerLast) {
        process.Scheduler().Exit(main,73);
        worker.memoryRegions.emplace_back(worker.teb,0x1000);
    }
    unsigned cleaned=0;
    {
        rt::GuestExecution execution(process,[&](rt::GuestThread& thread) {
            StopGuard guard{memory,thread,cleaned};
            process.RequestThreadExit(thread,55);
            return true;
        });
        auto result=execution.RunOne();
        assert(cleaned==1);
        if(suspendedPeer) {
            assert(result==rt::GuestExecution::Result::Ran && !win32::g_guestExitRequested);
            assert(main.memoryReleased && worker.status==rt::GuestThreadStatus::Suspended);
        } else {
            assert(result==rt::GuestExecution::Result::Stopped);
            assert(win32::g_guestExitRequested && win32::g_guestExitCode==55);
            assert(!process.Scheduler().HasLiveThreads());
            assert((workerLast?worker:main).memoryReleased);
            assert(!workerLast || main.exitCode==73);
        }
    }
    win32::g_guestExitRequested=false;win32::g_guestExitCode=0;rt::g_process=nullptr;
}
static void StartupFailureScenario() {
    cpu::PagedGuestMemory memory;
    assert(memory.Map(0x10000,0x10000,cpu::kProtRWX));
    rt::Process process(memory);rt::g_process=&process;
    auto& main=process.MainThread();main.id=0x1004;main.teb=0x10000;main.cpu.ESP()=0x18000;
    main.status=rt::GuestThreadStatus::Runnable;main.memoryRegions.emplace_back(main.teb,0x1000);
    assert(process.Scheduler().Add(main));
    {
        rt::GuestExecution execution(process,[&](rt::GuestThread&) {
            assert(false && "entry must not run after startup failure");return false;
        },rt::GuestExecution::Startup::InitializeModules);
        assert(execution.RunOne()==rt::GuestExecution::Result::Stopped);
        assert(execution.InitializationFailed() && !execution.InitializationError().empty());
        assert(win32::g_guestExitRequested && uint32_t(win32::g_guestExitCode)==0xc0000142u);
        assert(main.memoryReleased && main.exitCode==0xc0000142u);
    }
    win32::g_guestExitRequested=false;win32::g_guestExitCode=0;rt::g_process=nullptr;
}
int main() {
    Scenario(false); Scenario(true);
    for (bool nested:{false,true}) for (bool viaReturn:{false,true}) ScheduledScenario(nested,viaReturn);
    LastThreadScenario(false,false);LastThreadScenario(true,false);LastThreadScenario(false,true);
    StartupFailureScenario();
    puts("PASSED: execution stop/ExitProcess drain, thread tombstones, APC/timer cancellation, mutex abandonment and mapping order");
}
