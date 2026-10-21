// Exercise real Process allocations with a redistributable MinGW PE fixture.
#include "source/runtime/guest_execution.h"
#include "source/win32/host_functions.h"
#include <cassert>
#include <cstdio>

struct MappingGuard {
    cpu::PagedGuestMemory& memory;
    rt::GuestThread& thread;
    unsigned& cleaned;
    ~MappingGuard() {
        // Native HLE/SEH destructors may still need the dying thread's memory.
        for (const auto& region:thread.memoryRegions)
            assert(memory.IsRangeMapped(region.first,region.second));
        ++cleaned;
    }
};

int main(int argc,char** argv) {
    assert(argc==2);
    cpu::PagedGuestMemory memory;
    rt::Process process(memory);
    rt::g_process=&process;
    std::string error;
    assert(process.LoadMainExe(argv[1],error));
    auto& main=process.MainThread();
    assert(process.PrepareMainThread(main.cpu,error));
    const uint32_t start=main.cpu.eip;
    main.suspendCount=1;
    unsigned cleaned=0;
    bool park=false;
    rt::GuestExecution execution(process,[&](rt::GuestThread& thread) {
        MappingGuard guard{memory,thread,cleaned};
        if (park) {
            process.Scheduler().Wait(thread,0x1234);
            rt::GuestExecution::Yield();
            assert(!rt::GuestExecution::Checkpoint());
        } else process.Scheduler().Exit(thread,73);
        return true;
    });
    auto unmapped=[&](const std::vector<std::pair<uint32_t,uint32_t>>& regions) {
        for (const auto& region:regions)
            for (uint32_t offset=0;offset<region.second;offset+=0x1000)
                assert(!memory.IsMapped(region.first+offset));
    };
    uint32_t previousStack=0, previousTeb=0;
    // Repeated creation must reuse reclaimed address space rather than grow.
    for (unsigned n=0;n<256;++n) {
        auto* worker=process.CreateGuestThread(start,n,0x10000,false,error);
        assert(worker && !worker->memoryReleased && !worker->memoryRegions.empty());
        if (n) assert(worker->stackLimit==previousStack && worker->teb==previousTeb);
        previousStack=worker->stackLimit; previousTeb=worker->teb;
        const auto regions=worker->memoryRegions;
        // Include static TLS blocks, not just the vector and TEB slots.
        bool hasStaticTls=false;
        for (const auto& module:process.Modules()) {
            if (module->tlsIndex==0xffffffffu || module->tlsIndex>=rt::Process::kTlsSlots) continue;
            uint32_t block=memory.Read32(worker->tlsArray+module->tlsIndex*4);
            assert(block && memory.IsMapped(block));
            hasStaticTls=true;
        }
        assert(hasStaticTls);
        // Ownership must not be reconstructed from guest-writable TLS slots.
        if (!n) for (const auto& module:process.Modules()) {
            if (module->tlsIndex<rt::Process::kTlsSlots)
                memory.Write32(worker->tlsArray+module->tlsIndex*4,main.teb);
        }
        uint32_t handle=process.Handles().Add(std::make_shared<rt::ThreadObject>(worker->id));
        auto alias=process.Handles().Get(handle);
        uint32_t duplicate=process.Handles().Add(alias);
        assert(execution.RunOne()==rt::GuestExecution::Result::Ran);
        assert(worker->memoryReleased && worker->memoryRegions.empty());
        assert(cleaned==n+1 && worker->exitCode==73);
        unmapped(regions);
        assert(process.FindThread(worker->id)==worker);
        assert(process.Handles().Get(handle)==alias && process.Handles().Get(duplicate)==alias);
        assert(process.Handles().Close(handle) && process.Handles().Close(duplicate));
        assert(memory.IsMapped(main.teb) && memory.IsMapped(process.PebAddress()));
    }
    // A peer exits a worker parked in native code: drain before unmapping.
    park=true;
    auto* parked=process.CreateGuestThread(start,0,0x10000,false,error);
    assert(parked);
    const auto parkedRegions=parked->memoryRegions;
    assert(execution.RunOne()==rt::GuestExecution::Result::Ran);
    assert(cleaned==256 && !parked->memoryReleased);
    process.Scheduler().Exit(*parked,91);
    assert(execution.RunOne()==rt::GuestExecution::Result::Idle);
    assert(cleaned==257 && parked->memoryReleased && parked->exitCode==91);
    unmapped(parkedRegions);
    // Never started / CREATE_SUSPENDED has no native continuation to drain.
    auto* unstarted=process.CreateGuestThread(start,0,0x10000,true,error);
    assert(unstarted);
    const auto unstartedRegions=unstarted->memoryRegions;
    process.Scheduler().Exit(*unstarted,92);
    assert(execution.RunOne()==rt::GuestExecution::Result::Idle);
    assert(unstarted->memoryReleased && cleaned==257);
    unmapped(unstartedRegions);
    // Session destruction must drain an exited parked continuation too.
    auto* closing=process.CreateGuestThread(start,0,0x10000,false,error);
    assert(closing);
    const auto closingRegions=closing->memoryRegions;
    {
        rt::GuestExecution closingExecution(process,[&](rt::GuestThread& thread) {
            MappingGuard guard{memory,thread,cleaned};
            process.Scheduler().Wait(thread,0x1234);
            rt::GuestExecution::Yield();
            assert(!rt::GuestExecution::Checkpoint());
            return true;
        });
        assert(closingExecution.RunOne()==rt::GuestExecution::Result::Ran);
        process.Scheduler().Exit(*closing,94);
    }
    assert(closing->memoryReleased && cleaned==258 && !win32::g_guestExitRequested);
    unmapped(closingRegions);
    // Initial thread's TEB shares an allocation with PEB. Keep PEB alive.
    const auto mainRegions=main.memoryRegions;
    process.Scheduler().Exit(main,93);
    assert(execution.RunOne()==rt::GuestExecution::Result::Idle);
    assert(main.memoryReleased && main.exitCode==93);
    unmapped(mainRegions);
    assert(memory.IsMapped(process.PebAddress()));
    assert(memory.Read32(process.PebAddress()+8)==process.MainModule()->base);
    rt::g_process=nullptr;
    puts("PASSED: 256 thread reclamations, TLS blocks, native cleanup, HANDLE tombstones and PEB lifetime");
}
