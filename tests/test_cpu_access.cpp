#include "source/ui/gdi.h"
#include <cassert>
#include <cstdio>
#include <utility>
struct Gate:ui::CpuRasterGate {
    unsigned calls=0;bool ok=true;ui::Gdi* reenter=nullptr;
    bool Prepare() override {++calls;if(reenter)assert(!reenter->PrepareCpuAccess());return ok;}
};
struct Backend:ui::SolidRasterBackend {
    unsigned calls=0;ui::RasterDispatch result=ui::RasterDispatch::Failed;
    ui::RasterDispatch Fill(uint32_t,ui::Rect,uint32_t) override {++calls;return result;}
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
    // A still-live CPU gate cannot hide a revoked or failed raster owner.
    for(bool expiry:{false,true}) {
        ui::Gdi g;assert(g.SetScreen(&a.s));auto owner=std::make_shared<Gate>();assert(g.InstallCpuGate(owner));
        auto backend=std::make_shared<Backend>();assert(g.InstallSolidBackend(backend));
        const auto dc=g.CreateDc(ui::DcKind::Window,1),brush=g.CreateSolidBrush(0x123456);
        if(expiry)backend.reset();
        assert(!g.FrameRect(dc,{0,0,4,4},brush));assert(g.CpuAccessFault() && !g.PrepareCpuAccess());
        if(!expiry)assert(backend->calls==1);
        assert(owner->calls==0);
        assert(!g.PatBlt(dc,0,0,4,4,0x00f00021));
    }
    puts("CPU host access: PASS (guarded views, write revision, expired/failed/reentrant gate, sticky fault and moved store isolation)");
}
