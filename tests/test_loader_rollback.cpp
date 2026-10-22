#include "source/runtime/guest_execution.h"
#include "source/runtime/clock.h"
#include "source/win32/message_loop.h"
#include <cassert>
#include <cstdio>
#include <set>

int main(int argc,char** argv) {
    assert(argc==2);rt::Clock::SetFastForward(true);
    cpu::PagedGuestMemory memory; rt::Process process(memory);rt::g_process=&process;
    std::string error;
    // Preserve an implemented optional CRT HLE even without a native DLL.
    auto* crt=process.LoadLibrary("msvcrt.dll",error);assert(crt && crt->hle);
    assert(process.LoadMainExe(argv[1],error));
    auto& main=process.MainThread();assert(process.PrepareMainThread(main.cpu,error));
    auto* peer=process.CreateGuestThread(main.cpu.eip,0,0x10000,true,error);assert(peer);
    auto* control=process.FindByName("rollback_control.dll");assert(control);
    uint32_t state=0;bool done=false;size_t first=0;unsigned observed=0;
    std::set<std::pair<uint32_t,uint32_t>> captured;
    rt::GuestExecution execution(process,[&](rt::GuestThread& thread) {
        assert(!done && &thread==&main);
        assert(process.InitializeModules(thread.cpu,error));
        // Get the stable controller-owned state via its actual x86 export.
        uint32_t fn=process.GetProcAddress(control,"RollbackControl");assert(fn);
        cpu::CpuState local=thread.cpu;
        state=win32::CallGuestFunctionStdcall(local,memory,fn,{},main.teb,main.teb);assert(state);
        memory.Write32(state,1);memory.Write32(state+4,1);
        const size_t mainCount=main.memoryRegions.size(), peerCount=peer->memoryRegions.size();
        for(unsigned cycle=0;cycle<80;++cycle) {
            first=process.Modules().size();captured.clear();memory.Write32(state+20,0);
            uint32_t code=0;
            assert(!process.LoadLibraryInitialized(thread.cpu,"rollback_top.dll",error,code) && code==1114);
            assert(process.Modules().size()>=first+2);
            assert(memory.Read32(state+20)==8 && !memory.Read32(state+16));
            assert(!process.FindByName("rollback_top.dll") && !process.FindByName("rollback_dep.dll"));
            for(size_t i=first;i<process.Modules().size();++i) {
                auto& m=*process.Modules()[i];assert(m.unloaded && !memory.IsMapped(m.base));
                for(auto* t:{&main,peer}) {
                    assert(!t->staticTlsRegions.count(m.tlsIndex));
                    if(m.tlsIndex<rt::Process::kTlsSlots) assert(!memory.Read32(t->tlsArray+m.tlsIndex*4));
                }
            }
            assert(captured.size()>=6); // two images plus both threads' TLS
            for(auto region:captured) for(uint32_t offset=0;offset<region.second;offset+=0x1000)
                assert(!memory.IsMapped(region.first+offset));
            assert(main.memoryRegions.size()==mainCount && peer->memoryRegions.size()==peerCount);
            assert(memory.IsMapped(process.PebAddress()) && memory.IsMapped(main.teb) && memory.IsMapped(peer->teb));
        }
        for(const char* name:{"rollback_invalid_parent.dll","rollback_invalid_tls.dll"}) {
            first=process.Modules().size();uint32_t code=0;
            assert(!process.LoadLibraryInitialized(thread.cpu,name,error,code));
            assert(code==(std::string(name)=="rollback_invalid_tls.dll"?8u:193u));
            for(size_t i=first;i<process.Modules().size();++i)
                assert(process.Modules()[i]->unloaded && !memory.IsMapped(process.Modules()[i]->base));
            assert(main.memoryRegions.size()==mainCount && peer->memoryRegions.size()==peerCount);
        }
        memory.Write32(state+4,0);memory.Write32(state+20,0);uint32_t code=0;
        auto* good=process.LoadLibraryInitialized(thread.cpu,"rollback_top.dll",error,code);
        assert(good && !code && good->tlsIndex<rt::Process::kTlsSlots);
        assert(process.FreeLibrary(thread.cpu,good->base));
        memory.Write32(state,0);done=true;process.Scheduler().Exit(thread,0);return true;
    });
    for(unsigned n=0;n<100000 && !done;++n) {
        auto result=execution.RunOne();assert(result!=rt::GuestExecution::Result::AllocationFailed);
        if(state && !done) for(size_t i=first;i<process.Modules().size();++i) {
            const auto& m=*process.Modules()[i];if(m.unloaded || m.hle)continue;
            captured.emplace(m.base,m.size);
            for(auto* t:{&main,peer}) {
                auto it=t->staticTlsRegions.find(m.tlsIndex);
                if(it!=t->staticTlsRegions.end()) {
                    captured.insert(it->second);
                    if(t==peer) memory.Write32(t->tlsArray+m.tlsIndex*4,process.PebAddress());
                }
            }
        }
        if(result==rt::GuestExecution::Result::Idle) {++observed;rt::Clock::Sleep(1);}
    }
    assert(done && observed);
    puts("PASSED: 80 failed-load image/TLS reclamations, suspended-peer corrupt slots, failed map/TLS setup and clean retry");
    rt::g_process=nullptr;
}
