#include "source/runtime/guest_execution.h"
#include "source/win32/host_functions.h"
#include "source/win32/user32.h"
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <vector>

struct Cleanup { unsigned& count; ~Cleanup() { ++count; } };
int main() {
    cpu::PagedGuestMemory memory;
    assert(memory.Map(0x10000,0x20000,cpu::kProtRWX));
    rt::Process process(memory); rt::g_process=&process;
    auto& main=process.MainThread(); rt::GuestThread peer;
    main.id=0x1004;main.teb=0x10000;main.cpu.ESP()=0x18000;
    peer.id=0x1008;peer.teb=0x11000;peer.cpu.ESP()=0x1f000;
    main.status=peer.status=rt::GuestThreadStatus::Runnable;
    memory.Write32(main.teb+0x24,main.id); memory.Write32(peer.teb+0x24,peer.id);
    assert(process.Scheduler().Add(main) && process.Scheduler().Add(peer));
    auto eventA=std::make_shared<rt::SyncObject>(rt::ObjectKind::Event);
    auto eventB=std::make_shared<rt::SyncObject>(rt::ObjectKind::Event);
    uint32_t a=process.Handles().Add(eventA), b=process.Handles().Add(eventB);
    auto program=[&](bool first) {
        std::vector<uint8_t> bytes;
        auto dword=[&](uint32_t value) { for(unsigned n=0;n<4;++n) bytes.push_back(value>>(8*n)); };
        auto push=[&](uint32_t value) { bytes.push_back(0x68); dword(value); };
        auto call=[&](const char* name) {
            int index=win32::FindHostFunction("KERNEL32.dll",name); assert(index>=0);
            bytes.push_back(0xb8); dword(win32::IndexToHostAddress(index));
            bytes.push_back(0xff); bytes.push_back(0xd0);
        };
        if (first) { push(0xffffffff); push(b); call("WaitForSingleObject"); push(a); call("SetEvent"); }
        else { push(b); call("SetEvent"); push(0xffffffff); push(a); call("WaitForSingleObject"); }
        bytes.insert(bytes.end(),{0x64,0xa1,0x24,0,0,0,0xc3}); // mov eax,fs:[24h]; ret
        return bytes;
    };
    auto first=program(true), second=program(false);
    memory.LoadBytes(0x20000,first.data(),first.size());
    memory.LoadBytes(0x21000,second.data(),second.size());
    cpu::CpuState external; win32::user::G()={&external,&memory};
    unsigned completed=0, cleaned=0;
    {
        rt::GuestExecution execution(process,[&](rt::GuestThread& thread) {
            Cleanup guard{cleaned};
            errno=thread.id;
            uint32_t esp=thread.cpu.ESP();
            // Deliberately pass the wrong FS for peer: active callback must
            // bind the scheduled thread's TEB, including local SEH CPUs.
            cpu::CpuState local=thread.cpu;
            uint32_t result=win32::CallGuestFunctionStdcall(local,memory,
                thread.id==main.id ? 0x20000 : 0x21000,{},main.teb,main.teb);
            assert(result==thread.id && local.ESP()==esp && errno==int(thread.id));
            assert(process.CurrentThreadId()==thread.id);
            ++completed;
            process.Scheduler().Exit(thread,result);
            return true;
        });
        for(unsigned n=0;n<100 && process.Scheduler().HasLiveThreads();++n) {
            assert(execution.RunOne()==rt::GuestExecution::Result::Ran);
            assert(win32::user::G().c==&external);
        }
    }
    assert(completed==2 && cleaned==2 && !main.callbackDepth && !peer.callbackDepth);
    // Stop while suspended in a native frame: cleanup must run on its own
    // stack without executing the guest instruction following the wait.
    rt::GuestThread waiting;
    waiting.id=0x1010; waiting.teb=0x12000;waiting.cpu.ESP()=0x29000;
    waiting.status=rt::GuestThreadStatus::Runnable;
    assert(process.Scheduler().Add(waiting));
    completed=cleaned=0;
    eventB->signaled=false;
    {
        rt::GuestExecution execution(process,[&](rt::GuestThread& thread) {
            Cleanup guard{cleaned};
            win32::CallGuestFunctionStdcall(thread.cpu,memory,0x20000,{},waiting.teb,waiting.teb);
            ++completed;
            return false;
        });
        assert(execution.RunOne()==rt::GuestExecution::Result::Ran);
        assert(waiting.status==rt::GuestThreadStatus::Waiting && !cleaned);
    }
    assert(completed==1 && cleaned==1 && !waiting.callbackDepth);
    assert(!eventA->signaled && !win32::g_guestExitRequested);
    rt::g_process=nullptr; win32::user::G()={};
    puts("PASSED: nested guest continuations, local CPU/TEB, errno, RAII cleanup and stop");
}
