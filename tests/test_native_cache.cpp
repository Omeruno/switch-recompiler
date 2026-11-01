#include "source/cpu/native_cache.h"
#include "source/cpu/interpreter.h"
#include "source/runtime/workload_profile.h"
#include "source/runtime/integer_execution_report.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cerrno>
using namespace cpu;
namespace {
void Equal(const CpuState& a,const CpuState& b){
    assert(a.eip==b.eip && !std::memcmp(a.regs,b.regs,sizeof(a.regs)) && a.GetEflags()==b.GetEflags());
    assert(a.eflags==b.eflags && a.lazyOp==b.lazyOp && a.lazyWidth==b.lazyWidth && a.lazyCarryIn==b.lazyCarryIn);
    assert(a.lazyA==b.lazyA && a.lazyB==b.lazyB && a.lazyRes==b.lazyRes);
}
class Refuse final:public CodeBackend {
public:
    unsigned calls=0;
    size_t Granule()const override{return 4096;}
    std::unique_ptr<CodeRegion> Allocate(size_t)override{++calls;errno=ENOMEM;return {};}
};
}
int main(){
    constexpr uint32_t pc=0x10000;
    PagedGuestMemory memory,reference;for(auto* m:{&memory,&reference}){assert(m->Map(pc,0x4000));m->TrackIntegerExecution(true);}
    reference.EnableIntegerFastPath(false);CodeStore storage(memory.Identity());NativeCache cache(memory,storage);
    assert(!memory.NativeExecutionRequested() && !memory.NativeExecutionEnabled() && storage.Stats().images==0);
    memory.EnableNativeExecution(true);assert(memory.NativeExecutionEnabled());
    Interpreter actual(memory),expected(reference);uint64_t compared=0;
    const uint8_t initial[]={0x83,0xc0,1,0x83,0xd0,0,0x40,0x75,0xf7};
    for(auto* m:{&memory,&reference})m->LoadBytes(pc,initial,sizeof(initial));
    rt::WorkloadProfile a,b;
    auto step=[&](CpuState& first,CpuState& second){Instruction na,nb;
        StepResult ra,rb;{rt::WorkloadProfileScope scope(&a);ra=actual.Step(first,&na);}
        {rt::WorkloadProfileScope scope(&b);rb=expected.Step(second,&nb);}
        assert(ra==rb && na.length==nb.length && na.rawOpcodeForDebug==nb.rawOpcodeForDebug);Equal(first,second);
        assert(a.instructionCount==b.instructionCount && a.opcodes==b.opcodes);++compared;return ra;
    };
    CpuState first,second;first.eip=second.eip=pc;
    for(unsigned n=0;n<400;++n)assert(step(first,second)==StepResult::Continue);
    assert(memory.NativeStats().requests==400);
    if(memory.NativeArchitectureAvailable()){
        assert(memory.IntegerStats().native>300 && memory.NativeStats().compiles>0 && memory.NativeStats().hits>300 && storage.Stats().images>0);
    }else assert(!memory.IntegerStats().native && !memory.NativeStats().compiles && !storage.Stats().images);
    // A modification beyond the negative/admission probe must still invalidate
    // the complete translated source,even when only its first node is requested.
    for(auto* m:{&memory,&reference})for(unsigned n=0;n<32;++n)m->Write8(pc+0x80+n,0x90);
    for(unsigned n=0;n<12;++n){first.eip=second.eip=pc+0x80;assert(step(first,second)==StepResult::Continue);}
    const auto oldInvalidations=memory.NativeStats().invalidations;
    for(auto* m:{&memory,&reference})m->HostPointer(pc+0x80)[31]=0x40;
    first.eip=second.eip=pc+0x80;assert(step(first,second)==StepResult::Continue);
    if(memory.NativeArchitectureAvailable())assert(memory.NativeStats().invalidations==oldInvalidations+1);
    // A raw alias changes the old loop;the entire
    // translated source must be refused even when executing its first node.
    memory.HostPointer(pc)[6]=0x48;reference.HostPointer(pc)[6]=0x48;first.eip=second.eip=pc;
    assert(step(first,second)==StepResult::Continue);
    if(memory.NativeArchitectureAvailable())assert(memory.NativeStats().invalidations>0);
    for(unsigned n=0;n<80;++n){first.eip=second.eip=pc;assert(step(first,second)==StepResult::Continue);}
    // Neither stale native code nor stale negative admission survives remapping.
    for(auto* m:{&memory,&reference}){m->Unmap(pc,4096);assert(m->Map(pc,4096));m->Write8(pc,0x90);m->Write8(pc+1,0xcc);}
    for(unsigned n=0;n<20;++n){first.eip=second.eip=pc;assert(step(first,second)==StepResult::Continue);}
    for(auto* m:{&memory,&reference}){m->Write8(pc,0x8b);m->Write8(pc+1,0x00);}
    for(unsigned n=0;n<24;++n){first.eip=second.eip=pc;first.regs[0]=second.regs[0]=0;assert(step(first,second)==StepResult::Exception);}
    for(auto* m:{&memory,&reference})m->Write8(pc,0x90);
    for(unsigned n=0;n<20;++n){first.eip=second.eip=pc;assert(step(first,second)==StepResult::Continue);}
    if(memory.NativeArchitectureAvailable())assert(memory.NativeStats().negativeHits>0);
    // Full reference selection wins,without disturbing native publications.
    const auto oldRequests=memory.NativeStats().requests;memory.EnableIntegerFastPath(false);
    for(unsigned n=0;n<10;++n){first.eip=second.eip=pc;assert(step(first,second)==StepResult::Continue);}
    assert(memory.NativeStats().requests==oldRequests);memory.EnableIntegerFastPath(true);
    // More hot sites than slots exercise eviction and physical quota recycling.
    for(unsigned site=0;site<40;++site){const auto at=pc+0x100+site*32;
        for(auto* m:{&memory,&reference}){m->Write8(at,0x40);m->Write8(at+1,0xcc);}
        for(unsigned n=0;n<NativeCache::HotVisits+2;++n){first.eip=second.eip=at;assert(step(first,second)==StepResult::Continue);}
        assert(storage.Stats().images<=NativeCache::Entries);
    }
    if(memory.NativeArchitectureAvailable())assert(memory.NativeStats().evictions>10 && memory.NativeStats().compiles>40);
    // Prefix and 0F debug metadata must match the reference output contract.
    for(auto code:std::array<std::array<uint8_t,7>,2>{{{{0x0f,0x85,0,0,0,0,0xcc}},{{0x66,0x40,0xcc,0,0,0,0}}}}){
        for(auto* m:{&memory,&reference})m->LoadBytes(pc,code.data(),code.size());
        for(unsigned n=0;n<20;++n){first.eip=second.eip=pc;assert(step(first,second)==StepResult::Continue);}
    }
    // Closed storage fails closed once;remaining instructions still execute via
    // the interpreter with exactly one timestamp/census charge per step.
    storage.Close();for(auto* m:{&memory,&reference}){m->Write8(pc,0x40);m->Write8(pc+1,0xcc);}
    for(unsigned n=0;n<30;++n){first.eip=second.eip=pc;assert(step(first,second)==StepResult::Continue);}
    if(memory.NativeArchitectureAvailable())assert(memory.NativeStats().disabled && memory.NativeStats().failures==1);
    cache.Close();assert(!memory.NativeExecutionEnabled() && storage.Close()==CodeClose::Drained);
    assert(!GlobalCodeStats().images);
    {
        PagedGuestMemory owner;assert(owner.Map(pc,4096));owner.Write8(pc,0x40);owner.Write8(pc+1,0xcc);owner.EnableNativeExecution(true);
        auto backend=std::make_unique<Refuse>();auto* refused=backend.get();CodeStore code(owner.Identity(),{},std::move(backend));NativeCache scoped(owner,code);
        Interpreter interpreter(owner);CpuState state;
        for(unsigned n=0;n<100;++n){state.eip=pc;errno=EACCES;assert(interpreter.Step(state)==StepResult::Continue);assert(errno==EACCES);}
        assert(state.regs[0]==100);
        if(owner.NativeArchitectureAvailable())assert(refused->calls==1 && owner.NativeStats().failures==1 && owner.NativeStats().disabled);
        else assert(!refused->calls && !owner.NativeStats().failures);
    }
    assert(!GlobalCodeStats().images);
    assert(memory.IntegerStats().fast+memory.IntegerStats().reference+memory.IntegerStats().native==compared);
    FILE* report=std::tmpfile();assert(report);rt::WriteIntegerExecutionReport(report,memory,true);std::rewind(report);char text[4096]={};assert(std::fread(text,1,sizeof(text)-1,report)>0);std::fclose(report);
    assert(std::strstr(text,"CPU native requested: 1") && std::strstr(text,"CPU native cache requests/hits"));
    std::printf("Native cache PASS: %llu exact Interpreter::Step/reference comparisons;raw code/remap/negative invalidation,interior reuse,LRU quotas,debug/census/fault parity,reference priority,lazy ownership and release;native architecture %s;not batching or Switch FPS\n",(unsigned long long)compared,memory.NativeArchitectureAvailable()?"ARM64 executed":"unavailable (reference fallback only)");
}
