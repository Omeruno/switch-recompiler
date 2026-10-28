#include "source/win32/shell_session.h"
#include <cassert>
#include <cstdio>
using namespace win32::user;
using namespace win32::hle;
static constexpr uint32_t VK_SHIFT=0x10,VK_CONTROL=0x11,VK_MENU=0x12;
static std::vector<InputEvent> raw;
static bool alive=true;
static void Key(InputEvent::Type type,uint32_t vk) {InputEvent e;e.type=type;e.vk=vk;raw.push_back(e);}
static void Observe(InputEvent::Type type,uint32_t vk) {InputEvent e;e.type=type;e.vk=vk;ObservePhysicalInput(e);}
int main() {
    EnableUi(1280,720);
    for(const auto first:{0xA0u,0xA2u,0xA4u}) {
        const auto aggregate=first==0xA0?VK_SHIFT:first==0xA2?VK_CONTROL:VK_MENU;
        Observe(InputEvent::KeyDown,first);Observe(InputEvent::KeyDown,first+1);
        Observe(InputEvent::KeyUp,first);assert(S().asyncKeys[aggregate]&0x80);
        Observe(InputEvent::KeyUp,first+1);assert(!S().asyncKeys[aggregate]);
    }
    // Generic VK + physical right-side scan is not the left modifier.
    for(const auto& pair:{std::pair<uint32_t,uint16_t>{VK_SHIFT,0x36},{VK_CONTROL,0xe01d},{VK_MENU,0xe038}}){
        InputEvent e;e.type=InputEvent::KeyDown;e.vk=pair.first;e.scanCode=pair.second;
        ObservePhysicalInput(e);const auto right=0xa1+(pair.first-VK_SHIFT)*2;
        assert(S().asyncKeys[right]&0x80);assert(!S().asyncKeys[right-1]);
        e.type=InputEvent::KeyUp;e.physicalObserved=false;ObservePhysicalInput(e);assert(!S().asyncKeys[right]);
    }
    InputEvent pointer;pointer.x=400;pointer.y=300;ObservePhysicalInput(pointer);
    const auto original=g_platform;
    g_platform.wait=[](int,std::vector<InputEvent>& out){out.swap(raw);return alive;};
    ui::DesktopShell shell;
    {
        ShellSession bridge(shell);assert(bridge.Attach("input probe"));
        Key(InputEvent::KeyDown,VK_MENU);Key(InputEvent::KeyDown,0x73);Key(InputEvent::KeyUp,VK_MENU);
        assert(bridge.ServiceHost()==ShellSession::HostStatus::Running);
        assert(!S().asyncKeys[VK_MENU]);
        std::vector<InputEvent> events;assert(g_platform.wait(0,events));assert(events.size()==3);
        assert(events[0].physicalObserved && events[0].altDown && !events[0].wasDown);
        assert(events[1].altDown && !events[2].altDown && events[2].wasDown);
        assert(events[0].scanCode==0x38 && events[1].scanCode==0x3e && events[2].scanCode==0x38);
        assert(events[1].x==400 && events[1].y==300 && !S().asyncKeys[VK_MENU]);
        InputEvent sided;sided.type=InputEvent::KeyDown;sided.vk=VK_CONTROL;sided.scanCode=0xe01d;raw.push_back(sided);
        sided.type=InputEvent::KeyUp;raw.push_back(sided);
        assert(bridge.ServiceHost()==ShellSession::HostStatus::Running);events.clear();assert(g_platform.wait(0,events));
        assert(events.size()==2 && events[0].scanCode==0xe01d && events[1].scanCode==0xe01d && !S().asyncKeys[0xa3]);
        // Repeated fill/drain wraps the fixed ring without reordering or growing.
        for(unsigned pass=0;pass<5;++pass) {
            for(unsigned i=0;i<ShellSession::MaxGuestInput;++i)Key(i%2?InputEvent::KeyUp:InputEvent::KeyDown,'J');
            assert(bridge.ServiceHost()==ShellSession::HostStatus::Running);
            assert(bridge.PendingGuestInput()==ShellSession::MaxGuestInput);
            events.clear();assert(g_platform.wait(0,events));
            assert(events.size()==ShellSession::MaxGuestInput && !bridge.PendingGuestInput());
            for(size_t i=0;i<events.size();++i)assert(events[i].type==(i%2?InputEvent::KeyUp:InputEvent::KeyDown) && events[i].scanCode==0x24);
        }
        // The ordinary USER path preserves Quit for WM_CLOSE handling.
        InputEvent quit;quit.type=InputEvent::Quit;raw.push_back(quit);events.clear();
        assert(g_platform.wait(0,events) && events.size()==1 && events[0].type==InputEvent::Quit);
        assert(bridge.Status()==ShellSession::HostStatus::Running);
        alive=false;assert(bridge.ServiceHost()==ShellSession::HostStatus::SourceClosed);
    }
    assert(g_platform.wait && !g_platform.shellContext);g_platform=original;
    std::puts("host input: physical modifier snapshots, paired ring wrap, async state, USER Quit and source close PASS");
}
