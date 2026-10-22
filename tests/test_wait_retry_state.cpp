// Failed resumed wait-all must not leak its old deadline into a new call.
#include "source/runtime/process.h"
#include "source/runtime/clock.h"
#include "source/win32/host_functions.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
int main() {
    cpu::PagedGuestMemory memory;assert(memory.Map(0x10000,0x20000,cpu::kProtRW));
    rt::Process process(memory);rt::g_process=&process;
    auto& main=process.MainThread();rt::GuestThread peer;
    main.id=0x1004;main.teb=0x10000;main.cpu.ESP()=0x18000;
    peer.id=0x1008;peer.teb=0x11000;peer.cpu.ESP()=0x1f000;
    main.status=peer.status=rt::GuestThreadStatus::Runnable;
    assert(process.Scheduler().Add(main) && process.Scheduler().Add(peer));
    process.Scheduler().SetCurrent(&main);rt::Clock::SetFastForward(true);
    auto call=[&](const char* name,std::initializer_list<uint32_t> args) {
        auto& c=process.Scheduler().Current()->cpu;
        uint32_t esp=c.ESP();memory.Write32(esp,0x123456);
        unsigned i=0;for(auto v:args)memory.Write32(esp+4+4*i++,v);
        int index=win32::FindHostFunction("KERNEL32.dll",name);assert(index>=0);
        c.eip=win32::IndexToHostAddress(index);
        win32::HostCallOutcome out;assert(win32::TryHandleHostCall(c,memory,out));
        if(!out.customReturn) assert(c.eip==0x123456 && c.ESP()==esp+4+4*args.size());
        c.ESP()=esp;return out;
    };
    for(unsigned mode=0;mode<3;++mode) {
        const uint32_t first=call("CreateEventA",{0,0,0,0}).returnValue;
        const uint32_t second=call("CreateEventA",{0,0,0,0}).returnValue;
        memory.Write32(0x13000,first);memory.Write32(0x13004,second);
        auto wait=[&]() { return mode==0 ? call("WaitForMultipleObjects",{2,0x13000,1,5}) :
            call("WaitForMultipleObjectsEx",{2,0x13000,1,5,0}); };
        assert(wait().customReturn && main.status==rt::GuestThreadStatus::Waiting);
        process.Scheduler().SetCurrent(&peer);
        assert(call("CloseHandle",{second}).returnValue);
        process.Scheduler().SetCurrent(&main);
        assert(main.retryWaitToken==first && main.retryWaitDeadlineUs);
        if(mode==2) memory.Write32(0x13004,first); // alias validation failure on retry
        assert(wait().returnValue==0xffffffff);
        assert(memory.Read32(main.teb+0x34)==(mode==2 ? 87u:6u));
        rt::Clock::Sleep(10);
        assert(call("SetEvent",{first}).returnValue);
        const auto result=call("WaitForSingleObject",{first,0xffffffff});
        if(result.returnValue!=0) {
            std::fprintf(stderr,"STALE WAIT DEADLINE: fresh infinite wait returned 0x%x (mode %u)\n",result.returnValue,mode);
            return 1;
        }
        assert(!main.retryWaitToken && !main.retryWaitDeadlineUs && !main.timedOutHandle);
        assert(call("WaitForSingleObject",{first,0}).returnValue==0x102);
        assert(call("CloseHandle",{first}).returnValue);
    }
    const uint32_t expired=call("CreateEventA",{0,0,0,0}).returnValue;
    assert(call("WaitForSingleObject",{expired,2}).customReturn);
    rt::Clock::Sleep(3);assert(process.Scheduler().WakeExpiredWaits()==1);
    assert(main.timedOutHandle==expired);
    assert(call("CloseHandle",{expired}).returnValue);
    assert(call("WaitForSingleObject",{expired,2}).returnValue==0xffffffff);
    assert(!main.retryWaitToken && !main.retryWaitDeadlineUs && !main.timedOutHandle);
    rt::Clock::SetFastForward(false);rt::g_process=nullptr;
    puts("PASSED: failed wait-all/Ex/alias retry cleanup, independent next deadline and failed timed-out single wait cleanup");
}
