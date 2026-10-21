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
    puts("PASSED: round-robin, suspend, wait, wake and exit");
}
