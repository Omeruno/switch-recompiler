// Deterministic HLE ABI/lifecycle checks, also available without MinGW.
#include "source/runtime/process.h"
#include "source/runtime/files.h"
#include "source/win32/host_functions.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>

int main() {
    cpu::PagedGuestMemory memory;
    assert(memory.Map(0x10000,0x10000,cpu::kProtRW));
    rt::Process process(memory);
    rt::g_process=&process;
    auto& main=process.MainThread();
    main.id=0x1004; main.teb=0x10000;
    main.status=rt::GuestThreadStatus::Runnable;
    rt::GuestThread peer;
    peer.id=0x1008; peer.teb=0x11000;
    peer.status=rt::GuestThreadStatus::Runnable;
    assert(process.Scheduler().Add(main) && process.Scheduler().Add(peer));
    process.Scheduler().SetCurrent(&main);
    auto call=[&](const char* name,std::initializer_list<uint32_t> args) {
        auto* thread=process.Scheduler().Current();
        assert(thread);
        auto& cpu=thread->cpu;
        cpu.ESP()=0x1f000;
        memory.Write32(cpu.ESP(),0x123456);
        unsigned index=0;
        for (uint32_t arg:args) memory.Write32(cpu.ESP()+4+4*index++,arg);
        int function=win32::FindHostFunction("KERNEL32.dll",name);
        assert(function>=0);
        cpu.eip=win32::IndexToHostAddress(function);
        const uint32_t eip=cpu.eip, esp=cpu.ESP();
        win32::HostCallOutcome result;
        assert(win32::TryHandleHostCall(cpu,memory,result));
        if (result.customReturn) {
            assert(cpu.eip==eip && cpu.ESP()==esp);
        } else {
            assert(cpu.eip==0x123456 && cpu.ESP()==esp+4+4*args.size());
        }
        return result;
    };
    auto copy=[&](uint32_t handle) {
        assert(call("DuplicateHandle",{0xffffffff,handle,0xffffffff,0x12000,0,0,2}).returnValue);
        return memory.Read32(0x12000);
    };
    uint32_t event=call("CreateEventA",{0,0,0,0}).returnValue;
    uint32_t alias=copy(event);
    assert(alias!=event && rt::Handles().Get(alias)==rt::Handles().Get(event));
    assert(call("WaitForSingleObject",{event,0xffffffff}).customReturn);
    assert(main.status==rt::GuestThreadStatus::Waiting);
    process.Scheduler().SetCurrent(&peer);
    assert(call("SetEvent",{alias}).returnValue==1);
    assert(main.IsRunnable());
    process.Scheduler().SetCurrent(&main);
    assert(call("WaitForSingleObject",{event,0xffffffff}).returnValue==0);
    assert(call("WaitForSingleObject",{alias,0}).returnValue==0x102);
    assert(call("CloseHandle",{event}).returnValue==1);
    assert(call("SetEvent",{alias}).returnValue==1);
    assert(call("WaitForSingleObject",{event,0}).returnValue==0xffffffff);
    assert(call("WaitForSingleObject",{alias,0}).returnValue==0);
    std::weak_ptr<rt::KernelObject> weak=rt::Handles().Get(alias);
    assert(call("CloseHandle",{alias}).returnValue==1 && weak.expired());
    assert(call("CloseHandle",{alias}).returnValue==0);
    uint32_t semaphore=call("CreateSemaphoreA",{0,1,2,0}).returnValue;
    alias=copy(semaphore);
    memory.Write32(0x12100,semaphore); memory.Write32(0x12104,alias);
    assert(call("WaitForMultipleObjects",{2,0x12100,1,0}).returnValue==0xffffffff);
    assert(memory.Read32(main.teb+0x34)==87);
    assert(call("WaitForSingleObject",{alias,0}).returnValue==0);
    assert(call("WaitForSingleObject",{semaphore,0}).returnValue==0x102);
    assert(call("SetEvent",{alias}).returnValue==0);
    assert(memory.Read32(main.teb+0x34)==6);
    assert(call("CloseHandle",{alias}).returnValue==1);
    assert(call("CloseHandle",{semaphore}).returnValue==1);

    uint32_t mutex=call("CreateMutexA",{0,1,0}).returnValue;
    uint32_t threadHandle=copy(0xfffffffe);
    uint32_t threadCopy=copy(threadHandle);
    assert(call("CloseHandle",{threadHandle}).returnValue==1);
    process.Scheduler().SetCurrent(&peer);
    assert(call("WaitForSingleObject",{threadCopy,0xffffffff}).customReturn);
    process.Scheduler().Exit(main,73);
    assert(peer.IsRunnable());
    assert(call("WaitForSingleObject",{threadCopy,0xffffffff}).returnValue==0);
    assert(call("GetExitCodeThread",{threadCopy,0x12200}).returnValue==1);
    assert(memory.Read32(0x12200)==73);
    event=call("CreateEventA",{0,1,0,0}).returnValue;
    memory.Write32(0x12100,event); memory.Write32(0x12104,mutex);
    assert(call("WaitForMultipleObjects",{2,0x12100,1,0}).returnValue==0x102);
    assert(call("WaitForMultipleObjects",{2,0x12100,0,0}).returnValue==0x81);
    assert(call("ReleaseMutex",{mutex}).returnValue==1);
    assert(call("ReleaseMutex",{mutex}).returnValue==0);
    assert(call("CloseHandle",{threadCopy}).returnValue==1);
    assert(call("CloseHandle",{event}).returnValue==1);
    assert(call("CloseHandle",{mutex}).returnValue==1);

    event=call("CreateEventA",{0,1,1,0}).returnValue;
    assert(!call("DuplicateHandle",{0xffffffff,event,0xffffffff,1,0,0,3}).returnValue);
    assert(!rt::Handles().Get(event));
    // Explicit last-thread exit is distinct from an all-blocked deadlock.
    assert(process.Scheduler().HasLiveThreads());
    assert(call("ExitThread",{0}).customReturn);
    assert(!process.Scheduler().HasLiveThreads() && process.Scheduler().LastExitCode()==0);

    // Different Process instances cannot share their open object tables.
    auto file=std::make_shared<rt::OpenFile>();
    uint32_t fileHandle=rt::Files().Add(file);
    cpu::PagedGuestMemory otherMemory;
    rt::Process other(otherMemory);
    rt::g_process=&other;
    assert(!rt::Files().Get(fileHandle));
    rt::g_process=&process;
    assert(rt::Files().Get(fileHandle)==file);
    assert(rt::Files().Close(fileHandle));
    rt::g_process=nullptr;
    puts("PASSED: HANDLE ABI, aliases, waits, abandonment, process ownership and exit");
}
