#include "source/cpu/native_cache.h"
#include "source/cpu/interpreter.h"
#include "source/runtime/workload_profile.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
using namespace cpu;
namespace {
uint32_t rng=0x1679a391;
uint32_t Random(){rng=rng*1664525u+1013904223u;return rng;}
void Equal(const CpuState& a,const CpuState& b){
    assert(a.eip==b.eip && !std::memcmp(a.regs,b.regs,sizeof(a.regs)) && a.GetEflags()==b.GetEflags());
    assert(a.eflags==b.eflags && a.lazyOp==b.lazyOp && a.lazyWidth==b.lazyWidth && a.lazyCarryIn==b.lazyCarryIn && a.lazyA==b.lazyA && a.lazyB==b.lazyB && a.lazyRes==b.lazyRes);
    assert(!std::memcmp(a.segBase,b.segBase,sizeof(a.segBase)));
}
uint32_t Address(const ir::Node& n,const CpuState& c){return n.value+(n.base<8?c.regs[n.base]:0)+(n.index<8?c.regs[n.index]<<n.scale:0);}
}
int main(){
    constexpr uint32_t pc=0x11080;
    PagedGuestMemory memory,reference;for(auto* m:{&memory,&reference}){assert(m->Map(0x10000,0x3000));assert(m->Map(0x40000,4096));assert(m->Map(0x41000,4096));m->TrackIntegerExecution(true);}
    reference.EnableIntegerFastPath(false);CodeStore store(memory.Identity());
    uint64_t layouts=0,valid=0,refused=0,native=0;
    auto test=[&](std::vector<uint8_t> code){code.push_back(0xcc);
        for(auto* m:{&memory,&reference})m->LoadBytes(pc,code.data(),code.size());
        const auto block=ir::Build(memory,pc);assert(block.Count()==1 && block.At(0).op==ir::Op::Load && !block.At(0).flagsRead && !block.At(0).flagsWrite);
        arm64::Translation translated;assert(arm64::Compile(store,memory,block,translated));++layouts;
        for(unsigned state=0;state<10;++state){
            CpuState c;c.eip=pc;for(auto& reg:c.regs)reg=Random();for(auto& seg:c.segBase)seg=Random();
            c.SetEflags(Random());if(state&1){c.SetLazy(LazyOp::Sbb,32,Random(),Random(),Random());c.lazyCarryIn=Random()&1;}
            const auto& node=block.At(0);const uint32_t address=state<8?0x40000+(state%4==0?0xffe:state%4==1?0xfff:state%4):0x42000;
            c.segBase[node.segment]=address-Address(node,c);
            constexpr uint32_t edges[]={0,0xffffffffu,0x80000000u,0x7fffffffu};
            const uint32_t value=state<4?edges[state]:Random();if(state<8)for(auto* m:{&memory,&reference})m->Write32(address,value);
            auto a=c,b=c,d=c;
            rt::WorkloadProfile irProfile,nativeProfile,refProfile;
            ir::Result portable;{rt::WorkloadProfileScope scope(&irProfile);portable=ir::Execute(block,memory,a,1);}
            const auto fault=memory.Fault();
            arm64::Result result;{rt::WorkloadProfileScope scope(&nativeProfile);result=arm64::Execute(translated,store,memory,d,1);}
            if(state<8){
                assert(portable.executed==1 && a.regs[node.dst]==value);
                Interpreter interpreter(reference);{rt::WorkloadProfileScope scope(&refProfile);assert(interpreter.Step(b)==StepResult::Continue);}
                Equal(a,b);assert(irProfile.opcodes==refProfile.opcodes);++valid;
                if(memory.NativeArchitectureAvailable()){assert(result.status==arm64::Status::Complete && result.execution.executed==1);Equal(d,b);assert(nativeProfile.opcodes==refProfile.opcodes);++native;}
                else{assert(result.status==arm64::Status::ArchitectureUnavailable);Equal(d,c);}
            }else{
                assert(portable.reason==ir::Exit::MemoryRefusal && !portable.executed && !irProfile.instructionCount);Equal(a,c);
                if(memory.NativeArchitectureAvailable()){assert(result.status==arm64::Status::Complete && result.execution.reason==ir::Exit::MemoryRefusal && !result.execution.executed && !nativeProfile.instructionCount);Equal(d,c);}
                assert(memory.Fault().pending==fault.pending && memory.Fault().address==fault.address);++refused;
            }
        }
        assert(store.Retire(translated.Token()));
    };
    for(unsigned mr=0;mr<192;++mr)for(unsigned sib=0;sib<((mr&7)==4?256u:1u);++sib){
        std::vector<uint8_t> code={0x8b,uint8_t(mr)};if((mr&7)==4)code.push_back(uint8_t(sib));
        const unsigned mode=mr>>6;
        if(mode==1)code.push_back(0x80);
        else if(mode==2 || (!mode && ((mr&7)==5 || ((mr&7)==4 && (sib&7)==5))))code.insert(code.end(),{0x78,0x56,0x34,0x92});
        test(code);
    }
    test({0xa1,0x78,0x56,0x34,0x92});
    // All budgets/interior entries in a maximal load block;each entry returns
    // a relative completed count,not its absolute node index.
    std::vector<uint8_t> maximal;for(unsigned n=0;n<32;++n)maximal.insert(maximal.end(),{0x8b,0x13});
    for(auto* m:{&memory,&reference}){m->LoadBytes(pc,maximal.data(),maximal.size());m->Write32(0x40000,0x76543210);}
    arm64::Translation full;assert(arm64::Compile(store,memory,ir::Build(memory,pc),full));
    for(unsigned first:{0u,1u,15u,31u})for(unsigned budget=0;budget<=33;++budget){
        CpuState a;a.eip=pc+first*2;a.regs[3]=0x40000;a.SetLazy(LazyOp::Adc,32,1,2,4);a.lazyCarryIn=1;auto b=a,c=a;
        auto portable=ir::Execute(full.Source(),memory,a,budget);Interpreter ref(reference);
        for(unsigned n=0;n<portable.executed;++n)assert(ref.Step(b)==StepResult::Continue);
        Equal(a,b);
        auto result=arm64::Execute(full,store,memory,c,budget);
        if(memory.NativeArchitectureAvailable()){assert(result.execution.executed==portable.executed && result.execution.reason==portable.reason);Equal(c,b);}
    }
    assert(store.Retire(full.Token()));
    // A checkpoint can remap data between invocations. No mapping/value is
    // captured in the translation or retained across the callback.
    const uint8_t two[]={0x8b,0x13,0x8b,0x33,0xcc};memory.LoadBytes(pc,two,sizeof(two));
    arm64::Translation remap;assert(arm64::Compile(store,memory,ir::Build(memory,pc),remap));
    if(memory.NativeArchitectureAvailable()){
        struct Hook {PagedGuestMemory* memory;unsigned calls=0;static bool Run(void* context,const CpuState&){auto& h=*static_cast<Hook*>(context);
            if(++h.calls==2){h.memory->Unmap(0x40000,4096);assert(h.memory->Map(0x40000,4096));h.memory->Write32(0x40000,0x13579bdf);}return true;}} hook{&memory};
        memory.Write32(0x40000,0x2468ace0);CpuState state;state.eip=pc;state.regs[3]=0x40000;
        auto result=arm64::Execute(remap,store,memory,state,32,&Hook::Run,&hook);
        assert(result.execution.executed==2 && state.regs[2]==0x2468ace0 && state.regs[6]==0x13579bdf && hook.calls==2);
    }
    assert(store.Retire(remap.Token()));
    // Native progress at a later load;no speculative register/EIP or fault commit.
    const uint8_t sequence[]={0x40,0x8b,0x13,0x41,0x8b,0x37,0xf4};
    for(auto* m:{&memory,&reference}){m->LoadBytes(pc,sequence,sizeof(sequence));m->Write32(0x40000,0x87654321);}
    const auto block=ir::Build(memory,pc);assert(block.Count()==4);
    arm64::Translation translated;assert(arm64::Compile(store,memory,block,translated));
    CpuState c;c.eip=pc;c.regs[3]=0x40000;c.regs[7]=0x42000;c.regs[6]=0xabcdef01;memory.Write32(0x40000,0x87654321);
    auto expected=c;expected.regs[0]=1;expected.regs[2]=0x87654321;expected.regs[1]=1;expected.eip=pc+4;
    for(unsigned budget=0;budget<=5;++budget){auto a=c,b=c,d=c;
        const auto portable=ir::Execute(block,memory,a,budget);Interpreter ref(reference);
        for(unsigned n=0;n<portable.executed;++n)assert(ref.Step(b)==StepResult::Continue);
        Equal(a,b);const auto result=arm64::Execute(translated,store,memory,d,budget);
        if(memory.NativeArchitectureAvailable()){assert(result.execution.reason==portable.reason && result.execution.executed==portable.executed);Equal(d,b);}
    }
    if(memory.NativeArchitectureAvailable()){
        const auto before=memory.IntegerStats().native;auto r=arm64::Execute(translated,store,memory,c,32);
        assert(r.execution.executed==3 && r.execution.reason==ir::Exit::MemoryRefusal && c.eip==expected.eip && !std::memcmp(c.regs,expected.regs,sizeof(c.regs)) && memory.IntegerStats().native==before+3);
        const auto saved=c;memory.Fault()={true,true,0x5555};r=arm64::Execute(translated,store,memory,c,32);
        assert(!r.execution.executed && r.execution.reason==ir::Exit::MemoryRefusal && memory.Fault().pending && memory.Fault().address==0x5555);Equal(c,saved);memory.ClearFault();
    }
    assert(store.Retire(translated.Token()));
    // Cache refusal stays retryable;the common interpreter produces the AV once.
    {NativeCache cache(memory,store);memory.EnableNativeExecution(true);memory.EnableNativeBatching(true);Interpreter interpreter(memory);
        for(unsigned n=0;n<NativeCache::HotVisits;++n){CpuState warm;warm.eip=pc;assert(interpreter.Step(warm)==StepResult::Continue);}
        c=CpuState{};c.eip=pc;c.regs[3]=0x40000;c.regs[7]=0x42000;
        for(unsigned n=0;n<8 && c.eip!=pc+4;++n){auto r=interpreter.Run(c,32);assert(r.executed && r.status==StepResult::Continue);}
        assert(c.eip==pc+4);const auto saved=c;auto r=interpreter.Run(c,32);assert(r.executed==1 && r.status==StepResult::Exception && interpreter.LastException().code==EXC_ACCESS_VIOLATION && interpreter.LastException().faultAddress==0x42000);Equal(c,saved);
        assert(!memory.NativeStats().disabled);const auto compiles=memory.NativeStats().compiles;
        assert(memory.Map(0x42000,4096));memory.Write32(0x42000,0x1234abcd);
        r=interpreter.Run(c,1);assert(r.executed==1 && r.status==StepResult::Continue && c.regs[6]==0x1234abcd && c.eip==pc+6 && memory.NativeStats().compiles==compiles);
    }
    assert(store.Close()==CodeClose::Drained && !GlobalCodeStats().images);
    std::printf("Native load PASS (%llu layouts,%llu mapped IR/reference checks,%llu refused loads,%llu actual ARM64 loads;ModRM/SIB/moffs,DS/SS,unaligned split mappings,raw flags/aliases,budgets/interior resume/data-remap hook,dynamic prefix,EIP/fault preservation and cache map-after-refusal;architecture %s;not physical FPS)\n",(unsigned long long)layouts,(unsigned long long)valid,(unsigned long long)refused,(unsigned long long)native,memory.NativeArchitectureAvailable()?"ARM64 executed":"host fallback only");
}
