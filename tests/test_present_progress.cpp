#include "source/win32/user32.h"
#include "source/runtime/clock.h"
#include "source/ui/presentation.h"
#include <cassert>
#include <cstdio>

namespace user=win32::user;
static unsigned frames;
static int expectedX,expectedY;
static uint32_t expectedRgb;
static ui::SoftwarePresentation software;
static ui::Presentation presentation(&software);
static std::vector<uint32_t> pixels(1280*720);

static void BusyQueue(bool wait,bool posted,bool remove) {
    auto& q=user::Q();q.posted.clear();q.input.clear();q.quit=false;
    auto& messages=posted?q.posted:q.input;
    user::Msg event{};event.message=posted?user::WM_APP:user::WM_MOUSEMOVE;
    event.wParam=1; // held left button, as in a continuous brush stroke
    messages.push_back(event);messages.push_back(event);
    const uint32_t dc=ui::g_gdi.CreateDc(ui::DcKind::Screen);assert(dc);
    for(int i=0;i<12;++i) {
        expectedX=32+i;expectedY=64+(posted?1:0);expectedRgb=0x123400u+uint32_t(i);
        assert(ui::g_gdi.SetPixel(dc,expectedX,expectedY,ui::RgbToColorref(expectedRgb)));
        rt::Clock::Sleep(20);
        const auto before=frames;const auto queued=messages.size();
        user::Msg result;
        assert(user::FetchMessage(result,0,0,0,remove,wait));
        assert(result.message==event.message && result.wParam==1);
        assert(frames>before); // pixels must reach output BEFORE input stops
        assert(messages.size()==queued-(remove?1:0) && !messages.empty());
        if(remove)messages.push_back(event); // never allow an idle queue
    }
    // Busy polling without screen changes does not submit duplicate frames.
    const auto before=frames;
    for(int i=0;i<5;++i) {
        rt::Clock::Sleep(20);user::Msg result;
        assert(user::FetchMessage(result,0,0,0,false,wait));
    }
    assert(frames==before);
    // A changed screen still respects the 16 ms presentation throttle.
    expectedRgb=0xABCDEF;
    assert(ui::g_gdi.SetPixel(dc,expectedX,expectedY,ui::RgbToColorref(expectedRgb)));
    const auto throttleStart=user::NowMs();user::S().lastPresentMs=throttleStart;
    user::Msg result;assert(user::FetchMessage(result,0,0,0,false,wait));
    // A descheduled CI process may cross the deadline during FetchMessage.
    if(user::NowMs()-throttleStart<16)assert(frames==before);
    rt::Clock::Sleep(20);assert(user::FetchMessage(result,0,0,0,false,wait));
    assert(frames>before && messages.size()==2);
    assert(ui::g_gdi.DeleteDc(dc));messages.clear();
}

int main() {
    rt::Clock::SetFastForward(true);user::EnableUi(1280,720);
    user::g_platform.wait=[](int,std::vector<user::InputEvent>&) {return true;};
    user::g_platform.present=[](const ui::Surface& surface) {
        assert(surface.Get(expectedX,expectedY)==expectedRgb);
        assert(presentation.Submit(surface));
        assert(presentation.Render({pixels.data(),1280,720,1280,pixels.size(),true}));
        const auto rgb=expectedRgb;
        const uint32_t rgba=0xFF000000u|((rgb>>16)&255)|(rgb&0xFF00)|((rgb&255)<<16);
        assert(pixels[size_t(expectedY)*1280+expectedX]==rgba);++frames;
    };
    for(bool wait:{false,true})for(bool posted:{false,true})for(bool remove:{false,true})
        BusyQueue(wait,posted,remove);
    presentation.Detach();user::g_platform={};
    puts("PASSED: continuous drag/post traffic presents updated GDI pixels without idle; Get/Peek, remove/no-remove, throttle and no-op");
}
