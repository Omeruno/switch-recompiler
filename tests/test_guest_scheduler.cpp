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
    puts("PASSED: round-robin, suspend, wait, wake and exit");
}
