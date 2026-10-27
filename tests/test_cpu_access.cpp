#include "source/ui/gdi.h"
#include <cassert>
#include <cstdio>
#include <utility>
struct Gate:ui::CpuRasterGate {
    unsigned calls=0;bool ok=true;ui::Gdi* reenter=nullptr;
    bool Prepare() override {++calls;if(reenter)assert(!reenter->PrepareCpuAccess());return ok;}
};
int main() {
    ui::OwnedSurface a,b;a.Allocate(8,6);b.Allocate(8,6);
    ui::Gdi good;assert(good.SetScreen(&a.s));auto gate=std::make_shared<Gate>();assert(good.InstallCpuGate(gate));
    const auto* read=good.Screen();assert(read==&a.s && gate->calls==1);
    const auto version=good.ScreenChangeCounter();auto* write=good.ScreenForWrite();assert(write==&a.s && gate->calls==2);
    write->Set(3,2,0x123abc);assert(good.ScreenChangeCounter()>version && good.Screen()->Get(3,2)==0x123abc);
    assert(good.RemoveCpuGate(gate));
    ui::Gdi expired;assert(expired.SetScreen(&a.s));gate=std::make_shared<Gate>();assert(expired.InstallCpuGate(gate));gate.reset();
    assert(expired.CpuAccessFault()); // owner loss is visible even before a pixel operation
    assert(!expired.Screen() && expired.CpuAccessFault() && !expired.ScreenForWrite());
    assert(!expired.SetScreen(&b.s) && !expired.InstallCpuGate(std::make_shared<Gate>()));
    ui::Gdi failed;assert(failed.SetScreen(&a.s));gate=std::make_shared<Gate>();gate->ok=false;assert(failed.InstallCpuGate(gate));
    assert(!failed.PrepareCpuAccess() && failed.CpuAccessFault());gate->ok=true;
    assert(!failed.Screen() && !failed.PrepareCpuAccess() && gate->calls==1);
    assert(failed.RemoveCpuGate(gate) && !failed.PrepareCpuAccess()); // detach cannot erase a lost-authority fault
    ui::Gdi fresh;assert(fresh.SetScreen(&b.s));std::swap(failed,fresh);
    assert(!failed.CpuAccessFault() && failed.Screen()==&b.s && fresh.CpuAccessFault() && !fresh.Screen());
    ui::Gdi recursive;assert(recursive.SetScreen(&a.s));gate=std::make_shared<Gate>();gate->reenter=&recursive;
    assert(recursive.InstallCpuGate(gate));assert(!recursive.PrepareCpuAccess() && recursive.CpuAccessFault() && gate->calls==1);
    puts("CPU host access: PASS (guarded views, write revision, expired/failed/reentrant gate, sticky fault and moved store isolation)");
}
