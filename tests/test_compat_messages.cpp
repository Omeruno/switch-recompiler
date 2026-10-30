#include "source/runtime/guest_session.h"
#include "source/runtime/clock.h"
#include "source/win32/hle_util.h"
#include <cassert>
#include <cstdio>
static uint32_t tick=0;static uint32_t Hook(){return tick;}
int main(){
    rt::GuestSession session;auto& p=session.ProcessObject();auto& m=session.Memory();assert(m.Map(0x10000,0x20000,cpu::kProtRW));
    auto& thread=p.MainThread();thread.id=4;thread.teb=0x10000;thread.status=rt::GuestThreadStatus::Runnable;assert(p.Scheduler().Add(thread));p.Scheduler().SetCurrent(&thread);
    auto call=[&](const char* dll,const char* name,std::initializer_list<uint32_t> args){auto& c=thread.cpu;c.ESP()=0x2f000;m.Write32(c.ESP(),0x12345678);unsigned n=0;for(auto a:args)m.Write32(c.ESP()+4+4*n++,a);
        auto id=win32::FindHostFunction(dll,name);assert(id>=0);c.eip=win32::IndexToHostAddress(id);win32::HostCallOutcome out;assert(win32::TryHandleHostCall(c,m,out));assert(c.ESP()==0x2f004+n*4 && c.eip==0x12345678);return out.returnValue;};
    const auto saved=win32::g_getTickCountImpl;win32::g_getTickCountImpl=&Hook;
    tick=0xfffffffe;const auto a=call("WINMM.dll","timeGetTime",{});tick=3;const auto b=call("WINMM.dll","timeGetTime",{});
    assert(a==0xfffffffe && b==3 && uint32_t(b-a)==5 && call("KERNEL32.dll","GetTickCount",{})==3);win32::g_getTickCountImpl=nullptr;
    rt::Clock::SetFastForward(true);const auto before=call("WINMM.dll","timeGetTime",{});rt::Clock::Sleep(25);assert(uint32_t(call("WINMM.dll","timeGetTime",{})-before)>=25);rt::Clock::SetFastForward(false);win32::g_getTickCountImpl=saved;
    const uint32_t text=0x12000;for(unsigned n=0;n<255;++n)m.Write8(text+n,'x');m.Write8(text+255,0);
    const auto id=call("USER32.dll","RegisterWindowMessageA",{text});assert(id>=0xc000 && id<=0xffff);
    m.Write8(text+255,'x');m.Write8(text+256,0);assert(!call("USER32.dll","RegisterWindowMessageA",{text}) && m.Read32(thread.teb+0x34)==206);
    m.Write8(text+255,0);assert(call("USER32.dll","RegisterWindowMessageA",{text})==id);
    assert(!call("USER32.dll","RegisterWindowMessageW",{0xfffffffe}) && m.Read32(thread.teb+0x34)==998);
    puts("Compat messages PASS (actual shared platform/monotonic tick source,32-bit wrap,fast-forward,stdcall cleanup,255/256-unit names,no truncated registration,invalid wide pointer)");
}
