// Exercise the real native-continuation wait without requiring a MinGW install.
#include "source/runtime/guest_execution.h"
#include "source/runtime/clock.h"
#include "source/win32/host_functions.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>

int main() {
    cpu::PagedGuestMemory memory;
    assert(memory.Map(0x10000,0x20000,cpu::kProtRWX));
    rt::Process process(memory);rt::g_process=&process;
    auto& main=process.MainThread();rt::GuestThread peer;
    main.id=0x1004;main.teb=0x10000;main.cpu.ESP()=0x18000;
    peer.id=0x1008;peer.teb=0x11000;peer.cpu.ESP()=0x1f000;
    main.status=peer.status=rt::GuestThreadStatus::Runnable;
    assert(process.Scheduler().Add(main) && process.Scheduler().Add(peer));
    process.Scheduler().SetCurrent(&main);
    auto call=[&](const char* name,std::initializer_list<uint32_t> args) {
        auto& c=process.Scheduler().Current()->cpu;
        uint32_t esp=c.ESP();
        memory.Write32(esp,0x123456);
        unsigned i=0;for(auto arg:args)memory.Write32(esp+4+4*i++,arg);
        int index=win32::FindHostFunction("KERNEL32.dll",name);assert(index>=0);
        c.eip=win32::IndexToHostAddress(index);
        win32::HostCallOutcome result;
        assert(win32::TryHandleHostCall(c,memory,result) && !result.customReturn);
        assert(c.eip==0x123456 && c.ESP()==esp+4+4*args.size());
        c.ESP()=esp;
        return result.returnValue;
    };
    uint32_t request=call("CreateSemaphoreA",{0,0,2,0});
    uint32_t reply=call("CreateEventA",{0,0,0,0});
    uint32_t expired=call("CreateSemaphoreA",{0,0,2,0});
    process.Handles().BindName(u"TemporarySignal",process.Handles().Get(expired));
    std::weak_ptr<rt::KernelObject> weak=process.Handles().Get(expired);
    unsigned completed=0;
    {
        rt::GuestExecution execution(process,[&](rt::GuestThread& t) {
            if(t.id==main.id) {
                assert(call("SignalObjectAndWait",{request,reply,2000,0})==0);
                assert(call("WaitForSingleObject",{request,0})==0x102);
                // Timeout must not execute the signal a second time.
                assert(call("SignalObjectAndWait",{request,reply,2,0})==0x102);
                assert(call("WaitForSingleObject",{request,0})==0);
                assert(call("WaitForSingleObject",{request,0})==0x102);
                assert(call("SignalObjectAndWait",{expired,reply,2000,0})==0);
            } else {
                assert(main.status==rt::GuestThreadStatus::Waiting);
                assert(call("WaitForSingleObject",{request,0})==0);
                assert(call("WaitForSingleObject",{request,0})==0x102);
                assert(call("SetEvent",{reply}));
                // Let main finish the first wait and its finite timeout.
                while(main.waitObject!=reply || !process.Handles().Get(expired) ||
                      std::static_pointer_cast<rt::SyncObject>(process.Handles().Get(expired))->count==0) {
                    t.yieldRequested=true;assert(rt::GuestExecution::Checkpoint());
                }
                assert(call("CloseHandle",{expired}));
                assert(weak.expired() && !process.Handles().FindNamed(u"TemporarySignal"));
                assert(call("SetEvent",{reply}));
            }
            ++completed;process.Scheduler().Exit(t,0);return true;
        });
        for(unsigned i=0;i<10000 && process.Scheduler().HasLiveThreads();++i) {
            auto result=execution.RunOne();
            assert(result==rt::GuestExecution::Result::Ran || result==rt::GuestExecution::Result::Idle);
            if(result==rt::GuestExecution::Result::Idle)rt::Clock::Sleep(1);
        }
    }
    assert(completed==2);
    rt::g_process=nullptr;
    puts("PASSED: combined wait continuation, exactly-once signaling, timeout and named-object expiration");
}
