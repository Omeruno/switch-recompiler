#include "source/runtime/guest_execution.h"
#include "source/runtime/waitable_timer.h"
#include "source/runtime/clock.h"
#include "source/win32/host_functions.h"
#include "source/win32/user_apc.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
#include <vector>

int main() {
    cpu::PagedGuestMemory memory;
    assert(memory.Map(0x10000,0x20000,cpu::kProtRWX));
    rt::Process process(memory);rt::g_process=&process;
    auto& main=process.MainThread();rt::GuestThread peer;
    main.id=0x1004;main.teb=0x10000;main.cpu.ESP()=0x18000;
    peer.id=0x1008;peer.teb=0x11000;peer.cpu.ESP()=0x1f000;
    main.status=peer.status=rt::GuestThreadStatus::Runnable;
    memory.Write32(main.teb+0x24,main.id);memory.Write32(peer.teb+0x24,peer.id);
    assert(process.Scheduler().Add(main) && process.Scheduler().Add(peer));
    process.Scheduler().SetCurrent(&main);
    rt::Clock::SetFastForward(true);
    auto call=[&](const char* name,std::initializer_list<uint32_t> args) {
        auto& c=process.Scheduler().Current()->cpu;
        uint32_t esp=c.ESP();memory.Write32(esp,0x123456);
        unsigned i=0;for(auto arg:args)memory.Write32(esp+4+4*i++,arg);
        int index=win32::FindHostFunction("KERNEL32.dll",name);assert(index>=0);
        c.eip=win32::IndexToHostAddress(index);
        win32::HostCallOutcome result;
        assert(win32::TryHandleHostCall(c,memory,result));
        if(!result.customReturn) assert(c.eip==0x123456 && c.ESP()==esp+4+4*args.size());
        c.ESP()=esp;return result;
    };
    auto due=[&](uint64_t value) { memory.Write32(0x13000,uint32_t(value));memory.Write32(0x13004,uint32_t(value>>32)); };
    auto arm=[&](uint32_t timer,int64_t ticks,uint32_t period=0,uint32_t callback=0) {
        due(static_cast<uint64_t>(ticks));return call("SetWaitableTimer",{timer,0x13000,period,callback,123,0}).returnValue;
    };
    const char name[]="TimerNative";memory.LoadBytes(0x13100,reinterpret_cast<const uint8_t*>(name),sizeof(name));
    uint32_t timer=call("CreateWaitableTimerA",{0,0,0x13100}).returnValue;
    uint32_t alias=call("CreateWaitableTimerA",{0,1,0x13100}).returnValue;
    assert(timer && alias && memory.Read32(main.teb+0x34)==183);
    assert(!std::static_pointer_cast<rt::TimerObject>(process.Handles().Get(timer))->manualReset);
    assert(!call("CreateEventA",{0,0,0,0x13100}).returnValue && memory.Read32(main.teb+0x34)==6);
    assert(arm(timer,-10000) && process.Scheduler().HasPendingTimers());
    assert(call("WaitForSingleObject",{timer,0}).returnValue==0x102);
    rt::Clock::Sleep(2);
    assert(call("WaitForSingleObject",{alias,0}).returnValue==0);
    assert(call("WaitForSingleObject",{timer,0}).returnValue==0x102);
    assert(!process.Scheduler().HasPendingTimers());
    uint32_t manual=call("CreateWaitableTimerW",{0,1,0}).returnValue;
    assert(arm(manual,0));
    assert(call("CancelWaitableTimer",{manual}).returnValue); // materialize due state before cancel
    assert(call("WaitForSingleObject",{manual,0}).returnValue==0);
    assert(call("WaitForSingleObject",{manual,0}).returnValue==0);
    assert(arm(manual,-1000000));
    assert(call("WaitForSingleObject",{manual,0}).returnValue==0x102);
    assert(call("CancelWaitableTimer",{manual}).returnValue);
    rt::Clock::Sleep(150);
    assert(call("WaitForSingleObject",{manual,0}).returnValue==0x102);
    const uint64_t absolute=rt::Clock::FileTime()+30000;
    assert(arm(timer,static_cast<int64_t>(absolute)));
    rt::Clock::Sleep(4);assert(call("WaitForSingleObject",{timer,0}).returnValue==0);
    assert(arm(timer,-1,2));rt::Clock::Sleep(12);
    assert(call("WaitForSingleObject",{timer,0}).returnValue==0);
    assert(call("WaitForSingleObject",{timer,0}).returnValue==0x102);
    assert(call("CancelWaitableTimer",{timer}).returnValue);
    // Permissions, invalid due/period, wrong object type and signal-side rejection.
    uint32_t restricted=call("OpenWaitableTimerA",{rt::kSynchronize,1,0x13100}).returnValue;
    assert(restricted && !call("CancelWaitableTimer",{restricted}).returnValue && memory.Read32(main.teb+0x34)==5);
    assert(!call("SetWaitableTimer",{timer,1,0,0,0,0}).returnValue && memory.Read32(main.teb+0x34)==87);
    assert(!arm(timer,-1,0xffffffff) && memory.Read32(main.teb+0x34)==87);
    assert(call("SignalObjectAndWait",{timer,manual,0,0}).returnValue==0xffffffff && memory.Read32(main.teb+0x34)==6);
    assert(!call("SetEvent",{timer}).returnValue && memory.Read32(main.teb+0x34)==6);
    assert(arm(timer,INT64_MIN));
    assert(std::static_pointer_cast<rt::TimerObject>(process.Handles().Get(timer))->dueUs>rt::Clock::NowUs());
    assert(call("CancelWaitableTimer",{timer}).returnValue);
    due(0);
    assert(call("SetWaitableTimer",{timer,0x13000,0,0,0,1}).returnValue && memory.Read32(main.teb+0x34)==50);
    assert(call("WaitForSingleObject",{timer,0}).returnValue==0);
    // Real x86 timer callback checks the 12-byte stdcall frame and receiving TEB.
    std::vector<uint8_t> code;
    auto dword=[&](uint32_t v){ for(unsigned i=0;i<4;++i)code.push_back(v>>(8*i)); };
    for(unsigned i=0;i<3;++i) {
        code.insert(code.end(),{0x8b,0x44,0x24,uint8_t(4+4*i),0xa3});dword(0x14000+4*i);
    }
    code.insert(code.end(),{0x64,0xa1,0x24,0,0,0,0xa3});dword(0x14010);
    code.insert(code.end(),{0xff,0x05});dword(0x1400c);
    code.insert(code.end(),{0xc2,0x0c,0});memory.LoadBytes(0x20000,code.data(),code.size());
    const uint64_t before=rt::Clock::FileTime();
    assert(arm(timer,-10000,20,0x20000));rt::Clock::Sleep(100);
    rt::PollWaitableTimers();rt::Clock::Sleep(100);rt::PollWaitableTimers();
    assert(main.pendingApcs.size()==1); // at most one outstanding periodic APC
    assert(call("SleepEx",{0,1}).returnValue==0xc0 && memory.Read32(0x14000)==123 && memory.Read32(0x1400c)==1);
    const uint64_t timestamp=uint64_t(memory.Read32(0x14004))|(uint64_t(memory.Read32(0x14008))<<32);
    assert(timestamp>=before && timestamp<=rt::Clock::FileTime() && memory.Read32(0x14010)==main.id);
    rt::Clock::Sleep(25);rt::PollWaitableTimers();assert(main.pendingApcs.size()==1);
    assert(call("CancelWaitableTimer",{alias}).returnValue && main.pendingApcs.empty());
    assert(call("SleepEx",{0,1}).returnValue==0 && memory.Read32(0x1400c)==1);
    assert(arm(timer,-1,0,0x20000));rt::Clock::Sleep(1);rt::PollWaitableTimers();
    assert(main.pendingApcs.size()==1 && arm(timer,-1000000) && main.pendingApcs.empty());
    // A callback timer is canceled on setter exit, a plain timer survives it.
    process.Scheduler().SetCurrent(&peer);
    uint32_t onExit=call("CreateWaitableTimerA",{0,0,0}).returnValue;
    assert(arm(onExit,-1000000,0,0x20000) && arm(timer,-10000));
    process.Scheduler().Exit(peer,0);process.Scheduler().SetCurrent(&main);
    rt::Clock::Sleep(2);rt::PollWaitableTimers();
    assert(!std::static_pointer_cast<rt::TimerObject>(process.Handles().Get(onExit))->active);
    assert(call("WaitForSingleObject",{timer,0}).returnValue==0);
    // Last close cancels queued callbacks and expires the weak timer/name.
    assert(arm(timer,-1,1,0x20000));rt::Clock::Sleep(1);rt::PollWaitableTimers();
    assert(main.pendingApcs.size()==1);
    assert(call("CloseHandle",{restricted}).returnValue && call("CloseHandle",{alias}).returnValue);
    std::weak_ptr<rt::KernelObject> weak=process.Handles().Get(timer);
    assert(call("CloseHandle",{timer}).returnValue && weak.expired() && main.pendingApcs.empty());
    assert(!process.Handles().FindNamed(u"TimerNative"));
    assert(call("CloseHandle",{manual}).returnValue && call("CloseHandle",{onExit}).returnValue);
    // Infinite wait has no finite wait deadline: timer hook must keep idle alive.
    uint32_t idleTimer=call("CreateWaitableTimerA",{0,0,0}).returnValue;
    assert(arm(idleTimer,-30000));
    rt::GuestThread canceler;
    canceler.id=0x1010;canceler.teb=0x12000;canceler.cpu.ESP()=0x2c000;
    canceler.status=rt::GuestThreadStatus::Runnable;canceler.suspendCount=1;
    assert(process.Scheduler().Add(canceler));
    uint32_t cancelTarget=call("CreateWaitableTimerA",{0,0,0}).returnValue;
    bool done=false,canceled=false;
    {
        rt::GuestExecution execution(process,[&](rt::GuestThread& t) {
            if(t.id==main.id) {
                assert(call("SignalObjectAndWait",{call("CreateEventA",{0,0,0,0}).returnValue,idleTimer,0xffffffff,0}).returnValue==0);
                assert(arm(cancelTarget,-1,0,0x20000));rt::Clock::Sleep(1);rt::PollWaitableTimers();
                assert(main.pendingApcs.size()==1);
                canceler.suspendCount=0;main.yieldRequested=true;
                // Force the queue-check/checkpoint/cancel/resume race. The APC
                // dispatcher must revalidate the deque before reading front().
                assert(!win32::DispatchUserApcs(t.cpu,memory));
                assert(canceled && main.pendingApcs.empty() && memory.Read32(0x1400c)==1);
                done=true;
            } else {
                assert(t.id==canceler.id && main.pendingApcs.size()==1);
                assert(call("CancelWaitableTimer",{cancelTarget}).returnValue);canceled=true;
            }
            process.Scheduler().Exit(t,0);return true;
        });
        for(unsigned i=0;i<100 && !done;++i) {
            auto result=execution.RunOne();
            if(result==rt::GuestExecution::Result::Idle) {
                assert(process.Scheduler().HasPendingTimers());rt::Clock::Sleep(1);
            }
        }
    }
    assert(done && canceled);rt::Clock::SetFastForward(false);rt::g_process=nullptr;
    puts("PASSED: waitable timer deadlines/reset/periods, names/access, x86 APC ABI/coalescing/cancel/exit/lifetime idle wake and cancel-at-dispatch race");
}
