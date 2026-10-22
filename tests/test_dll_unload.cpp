#include "source/runtime/guest_execution.h"
#include "source/runtime/clock.h"
#include <cassert>
#include <cstdio>

int main(int argc,char** argv) {
    assert(argc==2);
    rt::Clock::SetFastForward(true);
    cpu::PagedGuestMemory memory;
    rt::Process process(memory); rt::g_process=&process;
    std::string error;
    assert(process.LoadMainExe(argv[1],error));
    auto& main=process.MainThread();
    assert(process.PrepareMainThread(main.cpu,error));
    auto* worker=process.CreateGuestThread(main.cpu.eip,0,0x10000,true,error);
    assert(worker);
    bool done=false;
    rt::GuestExecution execution(process,[&](rt::GuestThread& thread) {
        assert(!done && &thread==&main);
        assert(process.InitializeModules(thread.cpu,error));
        size_t mainRegions=main.memoryRegions.size(), workerRegions=worker->memoryRegions.size();
        for(unsigned i=0;i<80;++i) {
            auto* top=process.LoadLibrary("unload_top.dll",error); assert(top);
            assert(process.InitializeNewModules(thread.cpu,error));
            auto* dep=process.FindByName("unload_dep.dll"); assert(dep);
            assert(top->tlsIndex<rt::Process::kTlsSlots && dep->tlsIndex<rt::Process::kTlsSlots);
            // Deliberate import cycle: roots must keep both alive, then both
            // must be collectible rather than retaining each other's count.
            dep->dependencies.push_back(top);
            auto topBase=top->base, depBase=dep->base;
            std::vector<std::pair<uint32_t,uint32_t>> regions;
            for(auto* t:{&main,worker}) for(auto* m:{top,dep}) {
                auto region=t->staticTlsRegions.at(m->tlsIndex);
                assert(memory.IsRangeMapped(region.first,region.second)); regions.push_back(region);
                // Never infer ownership from the guest-writable TLS array.
                memory.Write32(t->tlsArray+m->tlsIndex*4,process.PebAddress());
            }
            assert(process.ReferenceModule(top));
            assert(process.FreeLibrary(thread.cpu,topBase));
            assert(process.FindByHandle(topBase)==top && memory.IsMapped(depBase));
            assert(process.FreeLibrary(thread.cpu,topBase));
            assert(top->unloaded && dep->unloaded && !process.FindByHandle(topBase));
            assert(!memory.IsMapped(topBase) && !memory.IsMapped(depBase));
            for(auto region:regions) assert(!memory.IsMapped(region.first));
            for(auto* t:{&main,worker}) for(auto* m:{top,dep})
                assert(!memory.Read32(t->tlsArray+m->tlsIndex*4));
            assert(main.memoryRegions.size()==mainRegions && worker->memoryRegions.size()==workerRegions);
            assert(memory.IsMapped(process.PebAddress()) && memory.IsMapped(main.teb) && memory.IsMapped(worker->teb));
            assert(!process.FreeLibrary(thread.cpu,topBase));
        }
        done=true;
        process.Scheduler().Exit(thread,0);
        return true;
    });
    for(unsigned n=0;n<10000 && !done;++n) {
        auto result=execution.RunOne();
        assert(result!=rt::GuestExecution::Result::AllocationFailed);
        if(result==rt::GuestExecution::Result::Idle) rt::Clock::Sleep(1);
    }
    assert(done);
    puts("PASSED: 80 cyclic DLL unloads, image/TLS reclamation, suspended peer, corrupt guest slots and PEB retention");
    rt::g_process=nullptr;
}
