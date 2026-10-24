#include "source/ui/performance_metrics.h"
#include "source/ui/gdi.h"
#include "source/ui/presentation.h"
#include "source/win32/user32.h"
#include "source/runtime/clock.h"
#include <cassert>
#include <cstdio>
#include <vector>

static uint64_t fakeNs,clockReads;
static uint64_t FakeClock() {++clockReads;return fakeNs;}
static void Boundaries() {
    ui::ExecutionPhaseMetrics phases(&FakeClock);
    {ui::ExecutionPhaseMetrics::Scope off(phases,ui::ExecutionPhase::Quantum);}
    assert(!clockReads);phases.Reset(true);fakeNs=10;
    {ui::ExecutionPhaseMetrics::Scope run(phases,ui::ExecutionPhase::Quantum);fakeNs=45;}
    // A parked fiber's wait/peer work belongs to later outer phases, not run.
    fakeNs=1000;
    {ui::ExecutionPhaseMetrics::Scope native(phases,ui::ExecutionPhase::NativeService);fakeNs=1020;}
    {ui::ExecutionPhaseMetrics::Scope idle(phases,ui::ExecutionPhase::IdleSleep);fakeNs=1040;}
    assert(clockReads==6 && phases.Phase(ui::ExecutionPhase::Quantum).totalNs==35);
    assert(phases.Phase(ui::ExecutionPhase::NativeService).totalNs==20);
    assert(phases.Phase(ui::ExecutionPhase::IdleSleep).totalNs==20);
    phases.Reset();assert(!phases.Enabled() && !phases.Phase(ui::ExecutionPhase::Quantum).count);clockReads=0;
    ui::RasterMetrics raster(&FakeClock);
    {ui::RasterMetrics::Scope off(raster,ui::RasterStage::Fill);}
    assert(!clockReads);raster.Reset(true);fakeNs=100;
    {
        ui::RasterMetrics::Scope outer(raster,ui::RasterStage::Text);
        fakeNs=150;
        {ui::RasterMetrics::Scope nested(raster,ui::RasterStage::Fill);fakeNs=200;}
        fakeNs=300;
    }
    assert(clockReads==2 && raster.Stage(ui::RasterStage::Text).count==1);
    assert(raster.Stage(ui::RasterStage::Text).totalNs==200 && !raster.Stage(ui::RasterStage::Fill).count);
    raster.Reset();assert(!raster.Enabled() && !raster.Stage(ui::RasterStage::Text).count);

    ui::InputFrameMetrics input;input.Removed(100,150,1);assert(!input.Pending());
    input.Reset(true);input.OutputQueued(50,1,1);
    input.Removed(100,150,1);input.Removed(120,160,2);
    assert(input.Pending()==2 && input.Stats().queueWait.totalNs==90);
    input.OutputQueued(200,1,1);assert(input.Pending()==2); // unchanged content
    input.OutputQueued(220,2,2);assert(input.Pending()==1); // second needs a change after its removal
    assert(input.Stats().observedToFrame.totalNs==120 && input.Stats().removedToFrame.totalNs==70);
    input.OutputQueued(300,3,3);assert(!input.Pending());
    assert(input.Stats().observedToFrame.totalNs==300 && input.Stats().removedToFrame.totalNs==210);
    input.Removed(1000,900,3);assert(input.Stats().invalid==1 && !input.Pending());
    input.Reset(true);
    for(size_t i=0;i<ui::InputFrameMetrics::Capacity+7;++i)input.Removed(100+i,200+i,1);
    assert(input.Pending()==ui::InputFrameMetrics::Capacity && input.Stats().dropped==7);
    input.OutputQueued(1000,2,1);assert(!input.Pending());
    assert(input.Stats().observedToFrame.count==ui::InputFrameMetrics::Capacity);
    input.Reset();assert(!input.Pending() && !input.Stats().dropped);
}
static std::vector<uint8_t> Draw(bool measured) {
    ui::OwnedSurface image;image.Allocate(96,80);ui::Gdi gdi;gdi.SetScreen(&image.s);
    auto dc=gdi.CreateDc(ui::DcKind::Screen),brush=gdi.CreateSolidBrush(0x123456);assert(dc && brush);
    ui::g_rasterMetrics.Reset(measured);
    assert(gdi.FillRect(dc,{0,0,96,80},brush));
    assert(gdi.FrameRect(dc,{2,2,90,70},brush)); // four nested fills, one outer call
    assert(gdi.Polygon(dc,{{5,5},{50,5},{25,40}}));
    assert(gdi.SetPixel(dc,3,3,0x112233));assert(gdi.GetPixel(dc,3,3)==0x112233);
    assert(gdi.InvertRect(dc,{8,8,12,12})); // nested PatBlt must not double-count
    assert(gdi.BitBlt(dc,40,30,10,10,dc,0,0,0x00CC0020));
    ui::Rect text{10,50,90,78};gdi.DrawText(dc,u"CPU GDI",text,0);
    if(measured) {
        assert(ui::g_rasterMetrics.Stage(ui::RasterStage::Fill).count==3);
        assert(ui::g_rasterMetrics.Stage(ui::RasterStage::Shape).count==1);
        assert(ui::g_rasterMetrics.Stage(ui::RasterStage::Blit).count==1);
        assert(ui::g_rasterMetrics.Stage(ui::RasterStage::Text).count==1);
        assert(ui::g_rasterMetrics.Stage(ui::RasterStage::Pixel).count==1);
        assert(ui::g_rasterMetrics.Stage(ui::RasterStage::Readback).count==1);
    }
    gdi.DeleteDc(dc);gdi.DeleteObject(brush);gdi.SetScreen(nullptr);
    return image.storage;
}
static void QueueToOutput() {
    namespace user=win32::user;
    rt::Clock::SetFastForward(true);user::EnableUi(1280,720);
    user::g_platform.wait=[](int,std::vector<user::InputEvent>&){return true;};
    auto& input=user::S().inputFrames;input.Reset(true);
    user::Msg message;message.message=user::WM_MOUSEMOVE;message.observedNs=ui::PerformanceNowNs();
    user::Q().input.push_back(message);
    user::Msg fetched;
    assert(user::FetchMessage(fetched,0,0,0,false,false));assert(!input.Pending());
    assert(user::FetchMessage(fetched,0,0,0,true,false));assert(input.Pending()==1);
    assert(fetched.observedNs==message.observedNs && input.Stats().queueWait.count==1);
    ui::SoftwarePresentation backend;ui::Presentation output(&backend);
    std::vector<uint32_t> pixels(1280*720);
    assert(output.Submit(user::S().screen.s));
    auto queue=[&] {
        assert(output.Render({pixels.data(),1280,720,1280,pixels.size(),true}));
        input.OutputQueued(ui::PerformanceNowNs(),ui::g_gdi.ScreenChangeCounter(),output.ContentSerial());
    };
    queue();assert(input.Pending()==1); // cursor/initial copy, no newer GDI work
    auto dc=ui::g_gdi.CreateDc(ui::DcKind::Screen);assert(dc);
    assert(ui::g_gdi.SetPixel(dc,70,70,0x13579B));assert(output.Submit(user::S().screen.s));
    assert(!output.Render({}));assert(input.Pending()==1); // failed output cannot consume input
    rt::Clock::Sleep(100000); // virtual clock jump must not become a measured wait
    queue();assert(!input.Pending() && input.Stats().observedToFrame.count==1);
    assert(input.Stats().observedToFrame.maxNs<100000000000ull);
    ui::g_gdi.DeleteDc(dc);input.Reset();ui::g_rasterMetrics.Reset();user::g_platform={};
}
int main() {
    Boundaries();assert(Draw(false)==Draw(true));ui::g_rasterMetrics.Reset();QueueToOutput();
    puts("PASSED: raster scopes preserve pixels and count nested operations once; real-clock input removal/changed output, no-peek/no-op/failure samples, bounded pending ring and session reset");
}
