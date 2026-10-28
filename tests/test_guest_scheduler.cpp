// Standalone host regression for the cooperative guest scheduler.
#include <cassert>
#include <cstdio>
#include "source/runtime/guest_scheduler.h"
int main() {
    rt::GuestScheduler scheduler;
    rt::GuestThread a,b,c;
    a.id=0x1004; b.id=0x1008; c.id=0x100c;
    a.status=b.status=c.status=rt::GuestThreadStatus::Runnable;
    assert(scheduler.Add(a) && scheduler.Add(b) && scheduler.Add(c));
    assert(!scheduler.Add(a));
    rt::GuestThread duplicate; duplicate.id=a.id;
    assert(!scheduler.Add(duplicate));
    assert(scheduler.Size()==3);
    assert(scheduler.Find(b.id)==&b);
    assert(scheduler.Next()==&a);
    assert(scheduler.Next()==&b);
    assert(scheduler.Next()==&c);
    assert(scheduler.Next()==&a);
    assert(scheduler.Suspend(b)==0 && b.suspendCount==1);
    assert(scheduler.Next()==&c);
    assert(scheduler.Next()==&a);
    assert(scheduler.Resume(b)==1 && b.suspendCount==0);
    assert(scheduler.Wait(a,0x2222));
    assert(scheduler.Wait(c,0x2222));
    assert(scheduler.Next()==&b);
    assert(scheduler.Signal(0x2222,false)==1);
    assert(a.IsRunnable() && !c.IsRunnable());
    assert(scheduler.Signal(0x2222,true)==1);
    assert(c.IsRunnable());
    scheduler.Exit(b,42);
    assert(!b.IsRunnable() && b.exitCode==42);
    assert(scheduler.Next()==&c);
    assert(scheduler.Next()==&a);
    scheduler.Exit(a,0); scheduler.Exit(c,0);
    assert(scheduler.Next()==nullptr);
    assert(!scheduler.Wait(a,1));
    assert(scheduler.Resume(a)==0);
    // Two guest CPUs genuinely alternate after bounded instruction quanta.
    rt::GuestScheduler execution;
    rt::GuestThread first,second;
    first.id=1; second.id=2;
    first.status=second.status=rt::GuestThreadStatus::Runnable;
    assert(execution.Add(first) && execution.Add(second));
    auto step=[](rt::GuestThread& t) {
        ++t.cpu.eip; // deterministic instruction-side effect
        return true;
    };
    auto q1=execution.RunQuantum(step,3);
    auto q2=execution.RunQuantum(step,2);
    auto q3=execution.RunQuantum(step,4);
    assert(q1.threadId==1 && q1.executed==3);
    assert(q2.threadId==2 && q2.executed==2);
    assert(q3.threadId==1 && q3.executed==4);
    assert(first.cpu.eip==7 && second.cpu.eip==2);
    // A wait entered by the guest instruction must stop the same quantum.
    auto waitStep=[&execution](rt::GuestThread& t) {
        ++t.cpu.eip;
        execution.Wait(t,0x3333);
        return true;
    };
    auto q4=execution.RunQuantum(waitStep,4096);
    assert(q4.threadId==2 && q4.executed==1 && !second.IsRunnable());
    auto q5=execution.RunQuantum(step,2);
    assert(q5.threadId==1 && q5.executed==2);
    assert(second.cpu.eip==3 && first.cpu.eip==9);
    assert(execution.Signal(0x3333,true)==1);
    auto q6=execution.RunQuantum([](rt::GuestThread& t) {
        ++t.cpu.eip;
        return false; // host call or yield
    },4096);
    assert(q6.threadId==2 && q6.executed==1 && q6.stoppedByCallback);
    execution.Exit(first,0);
    execution.Exit(second,0);
    assert(execution.RunQuantum(step,12).executed==0);
    // Guest Sleep blocks only one virtual CPU and wakes at its guest-clock
    // deadline. Fast-forward mode keeps this deterministic and nonblocking.
    rt::Clock::SetFastForward(true);
    rt::GuestScheduler sleeps;
    rt::GuestThread sleeper,peer;
    sleeper.id=101;peer.id=102;
    sleeper.status=peer.status=rt::GuestThreadStatus::Runnable;
    assert(sleeps.Add(sleeper) && sleeps.Add(peer));
    sleeps.Sleep(sleeper,25);
    assert(!sleeper.IsRunnable() && sleeps.Next()==&peer);
    sleeps.Sleep(peer,50);
    assert(sleeps.Next()==nullptr && sleeps.HasSleepingThreads());
    rt::Clock::Sleep(26);
    assert(sleeps.Next()==&sleeper && !peer.IsRunnable());
    rt::Clock::Sleep(25);
    assert(sleeps.Next()==&peer);
    sleeps.Sleep(peer,0);
    assert(peer.yieldRequested);
    peer.yieldRequested=false;
    assert(!peer.yieldRequested);
    rt::Clock::SetFastForward(false);
    rt::Clock::SetFastForward(true);
    rt::GuestScheduler timed;
    rt::GuestThread timeoutThread,signaledThread;
    timeoutThread.id=211;signaledThread.id=212;
    timeoutThread.status=signaledThread.status=rt::GuestThreadStatus::Runnable;
    assert(timed.Add(timeoutThread) && timed.Add(signaledThread));
    assert(timed.WaitFor(timeoutThread,0xaaaau,25));
    assert(timed.WaitFor(signaledThread,0xbbbbu,100));
    assert(timed.Next()==nullptr && timed.HasPendingTimers());
    rt::Clock::Sleep(26);
    assert(timed.Next()==&timeoutThread);
    assert(timeoutThread.timedOutHandle==0xaaaau);
    assert(!signaledThread.IsRunnable());
    assert(timed.Signal(0xbbbbu,true)==1);
    assert(signaledThread.waitDeadlineUs==0 && signaledThread.timedOutHandle==0);
    assert(timed.Next()==&signaledThread);
    rt::Clock::SetFastForward(false);
    // A signal for a non-first handle must wake a wait-any thread, and
    // finite multiple waits must expire using the first-handle token.
    rt::Clock::SetFastForward(true);
    rt::GuestScheduler multi;
    rt::GuestThread multiple, other;
    multiple.id=301;other.id=302;
    multiple.status=other.status=rt::GuestThreadStatus::Runnable;
    assert(multi.Add(multiple) && multi.Add(other));
    assert(multi.WaitForMultiple(multiple,{0x5555,0x6666},false,20));
    assert(multiple.waitHandles.size()==2 && !multiple.IsRunnable());
    assert(multi.Signal(0x6666,false)==1 && multiple.IsRunnable());
    assert(multiple.waitHandles.empty() && multiple.timedOutHandle==0);
    assert(multi.WaitForMultiple(multiple,{0x5555,0x6666},true,20));
    rt::Clock::Sleep(21);
    multi.Next();
    assert(multiple.IsRunnable() && multiple.timedOutHandle==0x5555);
    assert(multiple.waitHandles.empty());
    rt::Clock::SetFastForward(false);
    // Retry after a partial signal keeps the original absolute deadline.
    rt::Clock::SetFastForward(true);
    rt::GuestScheduler deadlines;
    rt::GuestThread waiting;
    waiting.id=401;waiting.status=rt::GuestThreadStatus::Runnable;
    assert(deadlines.Add(waiting));
    assert(deadlines.WaitForMultiple(waiting,{0x1111,0x2222},true,100));
    const uint64_t originalDeadline=waiting.waitDeadlineUs;
    rt::Clock::Sleep(40);
    assert(deadlines.Signal(0x1111,false)==1);
    assert(waiting.IsRunnable());
    assert(waiting.retryWaitDeadlineUs==originalDeadline);
    assert(deadlines.WaitForMultiple(waiting,{0x1111,0x2222},true,100));
    assert(waiting.waitDeadlineUs==originalDeadline);
    rt::Clock::Sleep(61);
    deadlines.Next();
    assert(waiting.IsRunnable() && waiting.timedOutHandle==0x1111);
    rt::Clock::SetFastForward(false);
    // Service workers get normal quanta but do not keep the process alive.
    rt::GuestScheduler services;
    rt::GuestThread app,service;
    app.id=501;service.id=502;service.background=true;
    app.status=service.status=rt::GuestThreadStatus::Runnable;
    assert(services.Add(app) && services.Add(service));
    assert(!services.HasLivePeer(app.id) && services.HasLivePeer(service.id));
    assert(services.Next()==&app && services.Next()==&service);
    services.Exit(app,55);
    assert(!services.HasLiveThreads() && services.Next()==&service);
    services.Exit(service,0);
    assert(!services.Next());
    puts("PASSED: round-robin, suspend, wait, wake, background liveness and exit");
}
