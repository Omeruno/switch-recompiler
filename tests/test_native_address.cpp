#include "source/cpu/arm64_emitter.h"
#include "source/cpu/interpreter.h"
#include "source/runtime/workload_profile.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
using namespace cpu;
namespace {
uint32_t rng=0x643879a1;
uint32_t Random(){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
void Equal(const CpuState& a,const CpuState& b){
    assert(a.eip==b.eip && !std::memcmp(a.regs,b.regs,sizeof(a.regs)) && a.GetEflags()==b.GetEflags());
    assert(a.eflags==b.eflags && a.lazyOp==b.lazyOp && a.lazyWidth==b.lazyWidth && a.lazyCarryIn==b.lazyCarryIn);
    assert(a.lazyA==b.lazyA && a.lazyB==b.lazyB && a.lazyRes==b.lazyRes);
    assert(!std::memcmp(a.segBase,b.segBase,sizeof(a.segBase)) && !std::memcmp(a.segSel,b.segSel,sizeof(a.segSel)));
}
CpuState State(uint32_t pc,unsigned mode){CpuState c;c.eip=pc;
    constexpr uint32_t edge[]={0,1,0x7fffffffu,0x80000000u,0xffffffffu,0xfffffffeu};
    for(unsigned n=0;n<8;++n)c.regs[n]=mode<6?edge[(mode+n)%6]:Random();
    c.SetEflags(Random());if(mode&1){c.SetLazy(LazyOp::Adc,32,Random(),Random(),Random());c.lazyCarryIn=Random()&1;}
    for(auto& base:c.segBase)base=Random();
    for(auto& selector:c.segSel)selector=uint16_t(Random());
    return c;
}
}
int main(){
    PagedGuestMemory memory,reference;for(auto* m:{&memory,&reference}){assert(m->Map(0x10000,0x3000));m->TrackIntegerExecution(true);}
    reference.EnableIntegerFastPath(false);CodeStore store(memory.Identity());
    uint64_t layouts=0,cases=0,nativeCases=0;rt::WorkloadProfile irProfile,referenceProfile,nativeProfile;
    auto test=[&](const std::vector<uint8_t>& bytes,uint32_t pc,unsigned states,uint32_t maxBudget){
        memory.LoadBytes(pc,bytes.data(),bytes.size());reference.LoadBytes(pc,bytes.data(),bytes.size());
        const auto block=ir::Build(memory,pc);assert(block.Count() && block.Matches(memory));
        for(size_t n=0;n<block.Count();++n){const auto& node=block.At(n);
            assert(node.op==ir::Op::Address && !node.flagsRead && !node.flagsWrite);
            assert(node.registersWritten==(1u<<node.dst));
            const unsigned reads=(node.base<8?1u<<node.base:0)|(node.index<8?1u<<node.index:0);assert(node.registersRead==reads);
        }
        arm64::Translation translated;assert(arm64::Compile(store,memory,block,translated));++layouts;
        for(unsigned state=0;state<states;++state)for(uint32_t budget=0;budget<=maxBudget;++budget){
            auto initial=State(pc,state),a=initial,b=initial,c=initial;
            ir::Result portable;{rt::WorkloadProfileScope scope(&irProfile);portable=ir::Execute(block,memory,a,budget);}
            Interpreter interpreter(reference);
            {rt::WorkloadProfileScope scope(&referenceProfile);for(unsigned n=0;n<portable.executed;++n)assert(interpreter.Step(b)==StepResult::Continue);}
            Equal(a,b);assert(!memory.Fault().pending && !reference.Fault().pending);
            arm64::Result native;{rt::WorkloadProfileScope scope(&nativeProfile);native=arm64::Execute(translated,store,memory,c,budget);}
            if(memory.NativeArchitectureAvailable()){
                assert(native.status==arm64::Status::Complete && native.execution.reason==portable.reason && native.execution.executed==portable.executed);
                Equal(c,b);if(portable.executed)++nativeCases;
            }else{assert(native.status==(budget?arm64::Status::ArchitectureUnavailable:arm64::Status::Complete));Equal(c,initial);}
            assert(irProfile.opcodes==referenceProfile.opcodes && irProfile.instructionCount==referenceProfile.instructionCount);
            if(memory.NativeArchitectureAvailable())assert(nativeProfile.opcodes==referenceProfile.opcodes && nativeProfile.instructionCount==referenceProfile.instructionCount);
            ++cases;
        }
        assert(store.Retire(translated.Token()) && !store.Stats().images);
    };
    // Every legal unprefixed32-bit ModRM/SIB form,all destinations,scales,
    // absent base/index,ESP/EBP,aliases,negative displacements and page crossings.
    for(unsigned mr=0;mr<192;++mr){const auto rm=mr&7,mode=mr>>6;
        for(unsigned sib=0;sib<(rm==4?256u:1u);++sib){std::vector<uint8_t> bytes={0x8d,uint8_t(mr)};
            if(rm==4)bytes.push_back(uint8_t(sib));
            if(mode==1)bytes.push_back(0x80);
            else if(mode==2 || (!mode && (rm==5 || (rm==4 && (sib&7)==5))))bytes.insert(bytes.end(),{0xf0,0xff,0xff,0xff});
            const auto length=bytes.size();bytes.push_back(0xcc);
            test(bytes,(mr&1)?0x10fff:0x11080,12,1);
            const auto block=ir::Build(memory,(mr&1)?0x10fff:0x11080);assert(block.Count()==1 && block.At(0).length==length);
        }
    }
    std::vector<uint8_t> maximal;
    for(unsigned n=0;n<32;++n)maximal.insert(maximal.end(),{0x8d,0x04,0x88});
    test(maximal,0x11080,12,33);
    std::vector<uint8_t> longest;
    for(unsigned n=0;n<32;++n)longest.insert(longest.end(),{0x8d,0x04,0xc5,0xf0,0xff,0xff,0xff});
    test(longest,0x11080,12,33);
    const auto capacity=ir::Build(memory,0x11080);assert(capacity.Count()==27 && capacity.SourceSize()==189 && capacity.Boundary()==ir::Stop::Capacity);
    memory.LoadBytes(0x11080,maximal.data(),maximal.size());
    const auto full=ir::Build(memory,0x11080);assert(full.Count()==32 && full.SourceSize()==96);
    arm64::Translation translated;assert(arm64::Compile(store,memory,full,translated));
    auto c=State(full.Entry(),0),initial=c;
    memory.Fault()={true,true,0x87654321};auto zero=arm64::Execute(translated,store,memory,c,0);
    assert(!zero.execution.executed && memory.Fault().pending && memory.Fault().address==0x87654321);Equal(c,initial);memory.ClearFault();
    if(memory.NativeArchitectureAvailable()){
        struct Hook {PagedGuestMemory* m;unsigned calls=0;static bool Run(void* p,const CpuState&){auto& h=*static_cast<Hook*>(p);if(++h.calls==2)h.m->HostPointer(0x11084)[0]^=1;return true;}} hook{&memory};
        auto result=arm64::Execute(translated,store,memory,c,32,&Hook::Run,&hook);
        assert(result.execution.reason==ir::Exit::CodeChanged && result.execution.executed==1 && c.eip==full.Entry()+3);
        memory.HostPointer(0x11084)[0]^=1;
    }
    assert(store.Retire(translated.Token()));
    for(const auto& bytes:std::vector<std::vector<uint8_t>>{{0x8d,0xc0},{0x66,0x8d,0x00},{0x67,0x8d,0x00},{0x64,0x8d,0x00},{0xf0,0x8d,0x00}}){
        memory.LoadBytes(0x11080,bytes.data(),bytes.size());const auto refused=ir::Build(memory,0x11080);assert(!refused.Count() && refused.Boundary()==ir::Stop::Unsupported);
    }
    reference.Write8(0x11080,0x8d);reference.Write8(0x11081,0xc0);c=State(0x11080,0);initial=c;
    Interpreter illegal(reference);assert(illegal.Step(c)==StepResult::UnknownOpcode && illegal.LastException().code==EXC_ILLEGAL_INSTRUCTION);Equal(c,initial);
    for(const auto& bytes:std::vector<std::vector<uint8_t>>{{0x8d},{0x8d,0x04},{0x8d,0x04,0x25},{0x8d,0x85,0xf0,0xff}}){
        const auto pc=0x13000-uint32_t(bytes.size());memory.LoadBytes(pc,bytes.data(),bytes.size());memory.Fault()={true,true,0x1234};
        const auto partial=ir::Build(memory,pc);assert(!partial.Count() && partial.Boundary()==ir::Stop::FetchUnavailable && memory.Fault().pending && memory.Fault().address==0x1234);
        memory.ClearFault();
    }
    assert(store.Close()==CodeClose::Drained && !GlobalCodeStats().images);
    std::printf("Native address PASS (%llu ModRM/SIB layouts,%llu IR/reference cases,%llu actual native comparisons;raw flags,segments,unmapped effective addresses,alias/ESP/EBP/scales/wrap,page-crossing fetch,metadata,budgets/checkpoint mutation,illegal/prefix/truncated fallback;architecture %s;not physical FPS)\n",(unsigned long long)layouts,(unsigned long long)cases,(unsigned long long)nativeCases,memory.NativeArchitectureAvailable()?"ARM64 executed":"host emission only");
}
