#include "source/win32/user32.h"
#include "source/runtime/clock.h"
#include "source/ui/presentation.h"
#include <algorithm>
#include <cassert>
#include <cstdio>

namespace user=win32::user;
static user::WndClass cls;
static bool destroyOnPaint;
static unsigned painted;
static uint32_t Paint(uint32_t hwnd,uint32_t message,uint32_t,uint32_t,bool) {
    if(message!=user::WM_PAINT)return 0;
    ++painted;
    if(destroyOnPaint) {user::DestroyWindowInternal(hwnd);return 0;}
    auto* w=user::Win(hwnd);assert(w);
    auto dc=ui::g_gdi.CreateDc(ui::DcKind::Window,hwnd);
    auto brush=ui::g_gdi.CreateSolidBrush(w->userData);
    assert(ui::g_gdi.FillRect(dc,{0,0,w->client.Width(),w->client.Height()},brush));
    ui::g_gdi.DeleteObject(brush);ui::g_gdi.DeleteDc(dc);
    w=user::Win(hwnd);assert(w);w->update.Clear();w->erase=false;w->paintAnnounced=false;
    return 0;
}
static void Add(uint32_t hwnd,ui::Rect r,uint32_t color,uint32_t thread=0x1004) {
    auto w=std::make_unique<user::Window>();w->hwnd=hwnd;w->cls=&cls;w->wndProc=0xF00F9000;
    w->threadId=thread;w->style=user::WS_POPUP|user::WS_VISIBLE;w->parent=user::S().desktop;
    w->rect=r;w->client={0,0,r.Width(),r.Height()};w->userData=color;
    user::S().windows.emplace(hwnd,std::move(w));
    user::Desktop()->children.insert(user::Desktop()->children.begin(),hwnd);
    ui::g_gdi.InvalidateLayout();
}
int main() {
    rt::Clock::SetFastForward(true);user::EnableUi(1280,720);
    cpu::CpuState cpu;cpu::FlatGuestMemory memory(0x10000,0x10000);
    user::G().c=&cpu;user::G().m=&memory;
    user::RegisterHostWndProc(0xF00F9000,&Paint,true);
    Add(0x20000,{10,40,500,400},0x0000FF);Paint(0x20000,user::WM_PAINT,0,0,true);
    const ui::Rect popup{40,50,170,180};
    Add(0x20004,popup,0x0000FF00);Paint(0x20004,user::WM_PAINT,0,0,true);
    assert(user::S().screen.s.Get(60,70)==0x00FF00);
    user::Msg move{};move.message=user::WM_MOUSEMOVE;
    user::Q().input.push_back(move);user::Q().input.push_back(move);
    assert(user::DestroyWindowInternal(0x20004));
    assert(user::NeedsPaint(user::Win(0x20000)) && user::S().screen.s.Get(60,70)==0x00FF00);
    const auto before=painted;
    user::UpdateExposedWindows(popup);
    assert(painted==before+1 && user::S().screen.s.Get(60,70)==0xFF0000);
    assert(!user::NeedsPaint(user::Win(0x20000)) && user::Q().input.size()==2);
    // Never run another GUI thread's paint procedure directly from menu cleanup.
    Add(0x20008,{600,40,900,400},0xFF0000,0x1008);
    user::Win(0x20008)->internalPaint=true;
    user::UpdateExposedWindows({0,0,1280,720});assert(painted==before+1);
    user::S().windows.erase(0x20008);
    auto& children=user::Desktop()->children;children.erase(std::remove(children.begin(),children.end(),0x20008),children.end());
    // A paint callback can destroy its own window; no retained Window* is used.
    user::Win(0x20000)->internalPaint=true;destroyOnPaint=true;
    auto changes=ui::g_gdi.ScreenChangeCounter();user::UpdateExposedWindows({0,0,1280,720});
    assert(!user::Win(0x20000) && ui::g_gdi.ScreenChangeCounter()>changes);
    assert(user::Q().input.size()==2);
    user::G()={};
    puts("PASSED: popup exposure repaints before input idle, preserves queued input, skips peer painting and tolerates callback destruction; desktop changes are tracked");
}
