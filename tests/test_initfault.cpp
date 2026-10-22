#include "source/runtime/guest_execution.h"
#include "source/runtime/guest_fault.h"
#include "source/runtime/clock.h"
#include "source/cpu/interpreter.h"
#include "source/win32/host_functions.h"
#include "source/win32/message_loop.h"
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstring>

struct State32 {
    uint32_t mode,phase,entry,afterFault,handlerCalls,handlerAfter,failure,count;
    uint32_t events[128][5];
    uint32_t afterNested,workerBody,otherHandled,go,ready,worker,suspended;
};
static uint32_t ExpectedCode(uint32_t mode) {
    if(mode==4 || mode==6 || mode==13)return 0xc0000005;
    if(mode==8)return 0xe042f002;
    if(mode==9)return 0xc0000025;
    if(mode==12)return 0xc000001d;
    return 0xe042f001;
}
static uint32_t Controller(rt::Process& process) {
    auto* module=process.FindByName("initfault_control.dll");assert(module);
    uint32_t fn=process.GetProcAddress(module,"InitFaultControl");assert(fn);
    auto state=process.MainThread().cpu;
    return win32::CallGuestFunctionStdcall(state,process.Memory(),fn,{},process.MainThread().teb,process.MainThread().teb);
}
static void Startup(const char* path,uint32_t mode,uint32_t phase,bool abort=false) {
    win32::g_guestExitRequested=false;win32::g_guestExitCode=0;
    cpu::PagedGuestMemory memory;rt::Process process(memory);rt::g_process=&process;
    std::string error;assert(process.LoadMainExe(path,error));
    auto& main=process.MainThread();assert(process.PrepareMainThread(main.cpu,error));
    uint32_t state=Controller(process);assert(state);
    memory.Write32(state,mode);memory.Write32(state+4,phase);
    bool complete=false,initializationFailed=false,observedScope=false;
    const bool succeeds=mode<=2 || mode==11;
    const bool explicitExit=mode==15 || mode==16;
    {
        rt::GuestExecution execution(process,[&](rt::GuestThread& thread) {
            win32::HostCallOutcome result;
            if(win32::TryHandleHostCall(thread.cpu,memory,result))return !result.requestExit;
            cpu::Interpreter interpreter(memory,thread.teb,thread.teb);
            auto step=interpreter.Step(thread.cpu);
            assert(step==cpu::StepResult::Continue || step==cpu::StepResult::Halted);return true;
        },rt::GuestExecution::Startup::InitializeModules);
        for(unsigned n=0;n<50000;++n) {
            auto result=execution.RunOne();assert(result!=rt::GuestExecution::Result::AllocationFailed);
            if(main.guestFaultBoundary)observedScope=true;
            if(abort && main.guestFaultBoundary && main.status==rt::GuestThreadStatus::Waiting &&
               (!mode || mode!=14 || main.activeSehDepth)) {complete=true;break;}
            if(result==rt::GuestExecution::Result::Stopped) {
                initializationFailed=execution.InitializationFailed();
                assert(initializationFailed==(!succeeds && !explicitExit));
                assert(uint32_t(win32::g_guestExitCode)==(explicitExit?(mode==15?38u:39u):succeeds?0u:0xc0000142u));
                if(!succeeds && !explicitExit)assert(execution.InitializationError().find("initialization exception")!=std::string::npos);
                complete=true;break;
            }
            if(result==rt::GuestExecution::Result::Idle){assert(process.Scheduler().HasPendingTimers());rt::Clock::Sleep(1);}
        }
    }
    assert(complete && !main.guestFaultBoundary && !main.activeSehDepth && !main.callbackDepth && main.memoryReleased);
    assert(!memory.Read32(state+offsetof(State32,failure)));
    assert(memory.Read32(state+offsetof(State32,entry))==(!abort && succeeds?1u:0u));
    if(!succeeds || abort)assert(!memory.Read32(state+offsetof(State32,afterFault)));
    if(mode==8 || mode==13)assert(!memory.Read32(state+offsetof(State32,handlerAfter)));
    auto* failed=phase==2?process.MainModule():process.FindByName("initfault_module.dll");assert(failed);
    if(!abort && !succeeds && !explicitExit) {
        assert(failed->initializationFailed && failed->initializationExceptionCode==ExpectedCode(mode));
        const uint32_t at=failed->initializationExceptionAddress;
        assert((mode==6 && !at) || (at>=failed->base && at<failed->base+failed->size));
        assert(initializationFailed);
    }
    if(explicitExit)assert(!failed->initializationExceptionCode && !initializationFailed);
    if(abort)assert(observedScope && !initializationFailed && !failed->initializationExceptionCode);
    if(mode==11 && !abort) {
        assert(memory.Read32(state+offsetof(State32,afterNested))==1);
        bool sawChild=false;
        for(const auto& module:process.Modules())if(module->name=="initfault_child.dll") {
            assert(module->unloaded && module->initializationExceptionCode==0xe042f001u);sawChild=true;
        }
        assert(sawChild);
    }
    unsigned dllDetach=0,exeDetach=0;
    for(uint32_t i=0;i<memory.Read32(state+offsetof(State32,count));++i) {
        uint32_t e=state+offsetof(State32,events)+i*20;
        uint32_t id=memory.Read32(e),reason=memory.Read32(e+4),reserved=memory.Read32(e+12);
        if(reason!=0)continue;
        if(id==11 || id==12) {
            ++dllDetach;
            if(mode==10 && !phase)assert(!reserved);
            else assert(reserved);
        }
        if(id==31)++exeDetach;
        // The rejected nested child never received an attach-completion entry.
        assert(id!=21 && id!=22);
    }
    assert(dllDetach==(abort || explicitExit?0u:(succeeds || phase==2 || mode==10)?2u:0u));
    assert(exeDetach==(!abort && succeeds?1u:0u));
    if(!abort && !succeeds)assert(!memory.Read32(state+offsetof(State32,workerBody)));
    for(auto& object:process.Handles().Objects())if(object->type==rt::ObjectKind::Thread) {
        auto* thread=process.FindThread(static_cast<rt::ThreadObject&>(*object).threadId);
        assert(thread && thread->memoryReleased && !thread->guestFaultBoundary && !thread->activeSehDepth && !thread->callbackDepth);
    }
    assert(memory.IsMapped(process.PebAddress()));rt::g_process=nullptr;
    win32::g_guestExitRequested=false;win32::g_guestExitCode=0;
}
static void Standalone(const char* path,uint32_t mode) {
    win32::g_guestExitRequested=false;
    cpu::PagedGuestMemory memory;rt::Process process(memory);rt::g_process=&process;
    std::string error;assert(process.LoadMainExe(path,error));auto& main=process.MainThread();
    assert(process.PrepareMainThread(main.cpu,error));
    // Only controller/EXE dependencies initialize here; fault DLL is dynamic.
    assert(process.InitializeModules(main.cpu,error));
    uint32_t state=Controller(process),seh=memory.Read32(main.teb);assert(state);
    memory.Write32(state,mode);uint32_t code=0;
    assert(!process.LoadLibraryInitialized(main.cpu,"initfault_module.dll",error,code) && code==1114);
    assert(!main.guestFaultBoundary && !main.activeSehDepth && memory.Read32(main.teb)==seh);
    assert(!process.FindByName("initfault_module.dll"));
    bool saw=false;
    for(const auto& module:process.Modules())if(module->name=="initfault_module.dll") {
        assert(module->unloaded && module->initializationExceptionCode==ExpectedCode(mode));saw=true;
    }
    assert(saw);rt::g_process=nullptr;
}
static void Dynamic(const char* path) {
    win32::g_guestExitRequested=false;win32::g_guestExitCode=0;
    cpu::PagedGuestMemory memory;rt::Process process(memory);rt::g_process=&process;
    std::string error;assert(process.LoadMainExe(path,error));auto& main=process.MainThread();
    assert(process.PrepareMainThread(main.cpu,error));uint32_t state=Controller(process);assert(state);
    bool complete=false;
    {
        rt::GuestExecution execution(process,[&](rt::GuestThread& thread) {
            win32::HostCallOutcome outcome;
            if(win32::TryHandleHostCall(thread.cpu,memory,outcome))return !outcome.requestExit;
            cpu::Interpreter interpreter(memory,thread.teb,thread.teb);
            auto step=interpreter.Step(thread.cpu);assert(step==cpu::StepResult::Continue || step==cpu::StepResult::Halted);return true;
        },rt::GuestExecution::Startup::InitializeModules);
        for(unsigned n=0;n<50000;++n) {
            auto result=execution.RunOne();assert(result!=rt::GuestExecution::Result::AllocationFailed);
            if(result==rt::GuestExecution::Result::Stopped){assert(win32::g_guestExitCode==0);complete=true;break;}
            if(result==rt::GuestExecution::Result::Idle){assert(process.Scheduler().HasPendingTimers());rt::Clock::Sleep(1);}
        }
    }
    assert(complete && memory.Read32(state+offsetof(State32,otherHandled))==1);
    const uint32_t codes[]={0,0,0,0xe042f001,0xc0000005,0xc0000005,0xe042f001,0xe042f002,0xc0000025,0xe042f001,0,0xc000001d,0xc0000005,0xe042f001,0xe042f001,0};
    unsigned index=0;
    for(auto& module:process.Modules())if(module->name=="initfault_module.dll") {
        assert(index<16 && module->unloaded && !memory.IsMapped(module->base));
        // The penultimate failure must retain the original initializer code,
        // even though another thread raised a different exception meanwhile.
        assert(module->initializationExceptionCode==codes[index++]);
    }
    assert(index==16);
    for(auto& object:process.Handles().Objects())if(object->type==rt::ObjectKind::Thread) {
        auto* t=process.FindThread(static_cast<rt::ThreadObject&>(*object).threadId);
        assert(t && t->memoryReleased && !t->guestFaultBoundary && !t->activeSehDepth && !t->callbackDepth);
    }
    rt::g_process=nullptr;win32::g_guestExitRequested=false;win32::g_guestExitCode=0;
}
int main(int argc,char** argv) {
    assert(argc==3);rt::Clock::SetFastForward(true);
    const uint32_t modes[]={0,1,2,3,4,6,7,8,9,10,11,12,13,14,15,16};
    for(auto mode:modes)Startup(argv[1],mode,0);
    for(auto mode:{1u,2u,3u,4u,6u,7u,8u,9u,12u,13u})Startup(argv[1],mode,1);
    for(auto mode:{1u,2u,3u,4u,6u,7u,8u,9u,12u,13u})Startup(argv[1],mode,2);
    Startup(argv[1],3,0,true);Startup(argv[1],14,0,true);
    for(auto mode:{3u,6u,7u,8u,9u,12u,13u})Standalone(argv[2],mode);
    Dynamic(argv[2]);
    puts("PASSED: 36 startup DLL/TLS/EXE-TLS modes, handler ExitThread/Process, two parked aborts, seven standalone scopes and dynamic cross-thread fault isolation");
}
