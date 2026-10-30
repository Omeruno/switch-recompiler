#include "source/runtime/guest_session.h"
#include "source/runtime/guest_execution.h"
#include "source/cpu/interpreter.h"
#include "source/win32/user32.h"
#include "source/win32/window_manager.h"
#include "source/win32/window_lookup.h"
#include <cassert>
#include <cstdio>
int main(int argc,char** argv) {
    assert(argc==2);unsigned normal=0;
    for (unsigned mode:{0u,1u,0u,1u,2u}) {
        rt::GuestSession session;auto& p=session.ProcessObject();auto& m=session.Memory();std::string error;
        if (mode!=1)win32::user::EnableUi(800,600);else win32::EnableHostLayer(win32::kUiLayer,false);
        assert(p.LoadMainExe(argv[1],error) && p.PrepareMainThread(p.MainThread().cpu,error));
        const auto probe=p.GetProcAddress(p.MainModule(),"LookupProbe");assert(probe);m.Write32(probe,mode);
        bool stopped=false,parked=false,observed=false;
        {
            rt::GuestExecution execution(p,[&](rt::GuestThread& thread){win32::HostCallOutcome out;
                if(win32::TryHandleHostCall(thread.cpu,m,out))return !out.requestExit;
                cpu::Interpreter cpu(m,thread.teb,thread.teb);const auto step=cpu.Step(thread.cpu);
                assert(step==cpu::StepResult::Continue || step==cpu::StepResult::Halted);return true;
            },rt::GuestExecution::Startup::InitializeModules);
            for(unsigned n=0;n<500000;++n) {
                const auto result=execution.RunOne();assert(result!=rt::GuestExecution::Result::AllocationFailed);
                if (m.Read32(probe+4)==1 && !observed) {
                    observed=true;const auto hwnd=m.Read32(probe+8),atom=m.Read32(probe+12);
                    if(mode==1) {
                        const auto* w=win32::g_windowManager.GetWindow(hwnd);assert(w && w->title==u"First Title");
                        assert(win32::g_windowManager.GetWindowClass(hwnd)->atom==atom);
                    } else {const auto* w=win32::user::Win(hwnd);assert(w && w->text==u"First Title" && w->cls->atom==atom);}
                    if(mode!=2)m.Write32(probe+16,1);
                }
                if(mode==2 && observed && p.MainThread().status==rt::GuestThreadStatus::Waiting){parked=true;break;}
                if(result==rt::GuestExecution::Result::Stopped){stopped=true;break;}
            }
        }
        if(mode==2)assert(parked && !stopped && !m.Read32(probe+20));
        else {
            if(!stopped || p.MainThread().exitCode)fprintf(stderr,"Lookup mode%u stopped=%d observed=%d exit/source-line=%u\n",mode,stopped,observed,p.MainThread().exitCode);
            assert(stopped && observed && !p.MainThread().exitCode && m.Read32(probe+20));++normal;
        }
    }
    // Dispatch and malformed-pointer tests use an actual current thread/TEB.
    rt::GuestSession session;auto& p=session.ProcessObject();auto& m=session.Memory();assert(m.Map(0x10000,0x30000,cpu::kProtRW));
    auto& t=p.MainThread();t.id=4;t.teb=0x10000;t.status=rt::GuestThreadStatus::Runnable;assert(p.Scheduler().Add(t));p.Scheduler().SetCurrent(&t);
    for(bool modern:{false,true}) {
        if(modern)win32::user::EnableUi(800,600);else win32::EnableHostLayer(win32::kUiLayer,false);
        auto call=[&](const char* name,std::initializer_list<uint32_t> args) {
            auto& c=t.cpu;c.ESP()=0x3f000;m.Write32(c.ESP(),0x12345678);unsigned count=0;
            for(auto a:args)m.Write32(c.ESP()+4+4*count++,a);
            const auto index=win32::FindHostFunction("USER32.dll",name);assert(index>=0);c.eip=win32::IndexToHostAddress(index);
            win32::HostCallOutcome out;assert(win32::TryHandleHostCall(c,m,out));assert(c.ESP()==0x3f004+count*4 && c.eip==0x12345678);return out.returnValue;
        };
        // Session teardown must remove every old real window, including parked ones.
        assert(!call("FindWindowA",{0,0}) && !call("FindWindowExW",{0,0,0,0}));
        for(unsigned i=0;i<32768;++i)m.Write8(0x18000+i,'x');
        assert(!call("FindWindowA",{0x18000,0}) && m.Read32(t.teb+0x34)==206);
        m.Write8(0x18000+32767,0);assert(!call("FindWindowA",{0x18000,0}));
        m.Write16(0x3fffe,'x');assert(!call("FindWindowW",{0,0x3fffe}) && m.Read32(t.teb+0x34)==998);
        assert(!call("FindWindowExA",{0xfffffffd,0,0,0}) && m.Read32(t.teb+0x34)==120);
    }
    assert(normal==4);
    puts("Window lookup PASS (4 normal new/legacy x86 sessions + parked abort;actual classes/atoms/titles/CP1252,hidden/top-level/owned/direct-child/after,modern rename+destroy,session isolation,checked pointers/bounds,stdcall8/16;no gameplay or hardware acceptance)");
}
