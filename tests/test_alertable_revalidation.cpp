// Force APC dispatch to yield after wait validation; peer cancels that APC and
// closes a wait HANDLE. Returning without a callback must revalidate the set.
#include "source/runtime/guest_execution.h"
#include "source/runtime/waitable_timer.h"
#include "source/win32/host_functions.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
static bool Run(unsigned mode) {
    cpu::PagedGuestMemory memory;assert(memory.Map(0x10000,0x20000,cpu::kProtRWX));
    rt::Process process(memory);rt::g_process=&process;
    auto& main=process.MainThread();rt::GuestThread peer;
    main.id=0x1004;main.teb=0x10000;main.cpu.ESP()=0x18000;
    peer.id=0x1008;peer.teb=0x11000;peer.cpu.ESP()=0x1f000;
    main.status=peer.status=rt::GuestThreadStatus::Runnable;peer.suspendCount=1;
    assert(process.Scheduler().Add(main) && process.Scheduler().Add(peer));
    process.Scheduler().SetCurrent(&main);
    auto call=[&](const char* name,std::initializer_list<uint32_t> args) {
        auto& c=process.Scheduler().Current()->cpu;
        uint32_t esp=c.ESP();memory.Write32(esp,0x123456);
        unsigned i=0;for(auto v:args)memory.Write32(esp+4+4*i++,v);
        int index=win32::FindHostFunction("KERNEL32.dll",name);assert(index>=0);
        c.eip=win32::IndexToHostAddress(index);
        win32::HostCallOutcome out;assert(win32::TryHandleHostCall(c,memory,out));
        assert(!out.customReturn && c.eip==0x123456 && c.ESP()==esp+4+4*args.size());
        c.ESP()=esp;return out.returnValue;
    };
    const uint32_t closed=call("CreateEventA",{0,0,0,0});
    const uint32_t ready=call("CreateEventA",{0,0,1,0});
    const uint32_t source=call("CreateEventA",{0,0,0,0});
    const uint32_t timer=call("CreateWaitableTimerA",{0,0,0});
    memory.Write32(0x13000,0);memory.Write32(0x13004,0); // due immediately
    memory.Write32(0x13100,closed);memory.Write32(0x13104,ready);
    memory.Write8(0x20000,0xc2);memory.Write16(0x20001,12); // never executed
    bool peerRan=false,correct=false;unsigned completed=0;
    {
        rt::GuestExecution execution(process,[&](rt::GuestThread& t) {
            if(t.id==main.id) {
                assert(call("SetWaitableTimer",{timer,0x13000,0,0x20000,0,0}));
                rt::PollWaitableTimers();assert(main.pendingApcs.size()==1);
                peer.suspendCount=0;
                // Entry owns checkpoint 1. Warm to 254: outer wait checkpoint
                // 255 validates the set, APC checkpoint 256 yields to peer.
                for(unsigned n=0;n<253;++n)assert(rt::GuestExecution::Checkpoint());
                assert(!peerRan);
                uint32_t result= mode==0 || mode==1 ?
                    call("WaitForMultipleObjectsEx",{2,0x13100,mode,0,1}) :
                    (mode==2 ? call("WaitForSingleObjectEx",{closed,0,1}) :
                               call("SignalObjectAndWait",{source,closed,0,1}));
                const uint32_t error=memory.Read32(main.teb+0x34);
                assert(peerRan && main.pendingApcs.empty());
                correct=result==0xffffffff && error==6;
                if(!correct)std::fprintf(stderr,"STALE ALERTABLE PREFLIGHT: mode %u returned 0x%x error %u after cancel/close\n",mode,result,error);
                if(correct) {
                    assert(call("WaitForSingleObject",{ready,0})==0);
                    assert(call("WaitForSingleObject",{ready,0})==0x102);
                    if(mode==3)assert(call("WaitForSingleObject",{source,0})==0);
                }
            } else {
                assert(main.pendingApcs.size()==1 && !main.alertableWait);
                assert(call("CancelWaitableTimer",{timer}));
                assert(call("CloseHandle",{closed}));peerRan=true;
            }
            ++completed;process.Scheduler().Exit(t,0);return true;
        });
        for(unsigned n=0;n<10 && completed<2;++n)assert(execution.RunOne()==rt::GuestExecution::Result::Ran);
    }
    assert(completed==2);rt::g_process=nullptr;return correct;
}
int main() {
    for(unsigned mode=0;mode<4;++mode)if(!Run(mode))return 1;
    puts("PASSED: alertable any/all/single/combined wait revalidation after canceled-APC yield preserves ready tokens");
}
