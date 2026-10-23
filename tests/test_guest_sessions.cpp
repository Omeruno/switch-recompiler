#include "source/runtime/guest_session.h"
#include "source/runtime/guest_execution.h"
#include "source/runtime/clock.h"
#include "source/cpu/interpreter.h"
#include "source/win32/host_functions.h"
#include "source/win32/iat_patcher.h"
#include "source/win32/user32.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <new>
#include <string>
#include <type_traits>

struct OwnedProbe {static int live;OwnedProbe(){++live;}~OwnedProbe(){--live;}};
int OwnedProbe::live;
static unsigned finishCalls;
static void CheckContext(rt::GuestSession& session) {
    assert(rt::g_process==&session.ProcessObject());
    assert(win32::ImportBindingMemory()==&session.Memory());
}
static void Binding() {
    assert(!rt::g_process);win32::g_guestExitRequested=true;win32::g_guestExitCode=71;win32::g_heapBase=0x111;
    {
        rt::GuestSession outer;CheckContext(outer);assert(!win32::g_guestExitRequested && !win32::g_guestExitCode);
        outer.ProcessObject().LocalState<OwnedProbe>();assert(OwnedProbe::live==1);
        const auto p=rt::ProcessHeap().Alloc(32,true);assert(p);outer.Memory().Write32(p,0x13579B);
        win32::g_heapBase=0x222;
        {
            rt::GuestSession inner;CheckContext(inner);inner.ProcessObject().LocalState<OwnedProbe>();
            assert(OwnedProbe::live==2 && !win32::g_heapBase);
            const auto q=rt::ProcessHeap().Alloc(32,true);assert(q==p && inner.Memory().Read32(q)==0);
            inner.Memory().Write32(q,0xABCDEF);
        }
        CheckContext(outer);assert(OwnedProbe::live==1 && win32::g_heapBase==0x222);
        assert(outer.Memory().Read32(p)==0x13579B);
    }
    assert(!rt::g_process && !win32::ImportBindingMemory() && !OwnedProbe::live);
    assert(win32::g_guestExitRequested && win32::g_guestExitCode==71 && win32::g_heapBase==0x111);
    win32::g_guestExitRequested=false;win32::g_guestExitCode=0;win32::g_heapBase=0;
}
static uint32_t Scenario(const char* exe,const std::string& mode,void* storage) {
    auto* session=new(storage)rt::GuestSession;CheckContext(*session);
    auto& process=session->ProcessObject();auto& memory=session->Memory();
    std::string error;process.SetCommandLine(std::string(exe)+" "+mode);
    assert(process.LoadMainExe(exe,error));assert(process.PrepareMainThread(process.MainThread().cpu,error));
    const auto probe=process.GetProcAddress(process.MainModule(),"SessionProbe");assert(probe);
    bool stopped=false,aborted=false;
    {
        rt::GuestExecution execution(process,[&](rt::GuestThread& thread) {
            win32::HostCallOutcome result;
            if(win32::TryHandleHostCall(thread.cpu,memory,result))return !result.requestExit;
            cpu::Interpreter interpreter(memory,thread.teb,thread.teb);
            const auto step=interpreter.Step(thread.cpu);
            assert(step==cpu::StepResult::Continue || step==cpu::StepResult::Halted);return true;
        },rt::GuestExecution::Startup::InitializeModules);
        for(unsigned i=0;i<300000;++i) {
            auto result=execution.RunOne();assert(result!=rt::GuestExecution::Result::AllocationFailed);
            if(mode=="abort" && memory.Read32(probe)==1){aborted=true;break;}
            if(result==rt::GuestExecution::Result::Stopped){if(win32::g_guestExitCode)std::fprintf(stderr,"session %s exit %u\n",mode.c_str(),win32::g_guestExitCode);assert(win32::g_guestExitCode==0);stopped=true;break;}
            if(result==rt::GuestExecution::Result::Idle)rt::Clock::Sleep(1);
        }
        assert(aborted || stopped);
    }
    // Continuations drain before session-local resources or address space vanish.
    assert(!process.Scheduler().HasLiveThreads());
    assert(memory.Read32(probe)==(mode=="first"?2:mode=="abort"?1:0));
    ++finishCalls;CheckContext(*session);
    session->~GuestSession();assert(!rt::g_process && !win32::ImportBindingMemory());
    return probe;
}
int main(int argc,char** argv) {
    assert(argc==2);rt::Clock::SetFastForward(true);cpu::g_consoleAvailable=false;Binding();
    const std::string exe=argv[1],root=exe.substr(0,exe.find_last_of("/\\"));
    std::aligned_storage_t<sizeof(rt::GuestSession),alignof(rt::GuestSession)> storage;
    // A failed load must discard its heap/caches and restore bindings too.
    {
        auto* failed=new(&storage)rt::GuestSession;
        failed->ProcessObject().LocalState<OwnedProbe>();
        assert(rt::ProcessHeap().Alloc(48,true));
        std::string error;
        assert(!failed->ProcessObject().LoadMainExe(exe+".missing",error));
        assert(!error.empty());
        failed->~GuestSession();
        assert(!rt::g_process && !win32::ImportBindingMemory() && !OwnedProbe::live);
    }
    uint32_t firstAddress=0;
    for(const auto& mode:{"first","second","abort","second"}) {
        const auto address=Scenario(argv[1],mode,&storage);
        if(!firstAddress)firstAddress=address;
        assert(address==firstAddress);
        std::ifstream deleted(root+"/session-delete.tmp");assert(!deleted.good());
        if(std::strcmp(mode,"first")==0 || std::strcmp(mode,"abort")==0) {
            std::ifstream file(root+"/session-leaked-stream.txt");
            const std::string text{std::istreambuf_iterator<char>(file),{}};assert(text=="flushed");
            std::remove((root+"/session-leaked-stream.txt").c_str());
        }
    }
    assert(finishCalls==4 && !OwnedProbe::live);
    puts("PASSED: four core guest sessions at reused memory/Process storage and guest addresses, normal/abort restart, heap/CRT/TLS/environment/FD isolation, file flush/delete and context restoration");
}
