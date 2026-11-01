#include "source/cpu/arm64_emitter.h"
#include "source/cpu/interpreter.h"
#include "source/runtime/workload_profile.h"
#include "source/runtime/host_fiber.h"
#include "source/runtime/integer_execution_report.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>
using namespace cpu;
#if defined(__aarch64__)
extern "C" uint64_t sr_check_translated_abi(void*,CpuState*,uint32_t,uint32_t);
asm(R"(
.text
.align 2
.global sr_check_translated_abi
.type sr_check_translated_abi,%function
sr_check_translated_abi:
 stp x19,x20,[sp,#-96]!
 stp x21,x22,[sp,#16]
 stp x23,x24,[sp,#32]
 stp x25,x26,[sp,#48]
 stp x27,x28,[sp,#64]
 stp x29,x30,[sp,#80]
 mov x16,x0
 mov x0,x1
 mov w1,w2
 mov w2,w3
 mov x19,#19
 movk x19,#0xa5a5,lsl #48
 mov x20,#20
 movk x20,#0xa5a5,lsl #48
 mov x21,#21
 movk x21,#0xa5a5,lsl #48
 mov x22,#22
 movk x22,#0xa5a5,lsl #48
 mov x23,#23
 movk x23,#0xa5a5,lsl #48
 mov x24,#24
 movk x24,#0xa5a5,lsl #48
 mov x25,#25
 movk x25,#0xa5a5,lsl #48
 mov x26,#26
 movk x26,#0xa5a5,lsl #48
 mov x27,#27
 movk x27,#0xa5a5,lsl #48
 mov x28,#28
 movk x28,#0xa5a5,lsl #48
 mov x29,#29
 movk x29,#0xa5a5,lsl #48
 blr x16
 mov x0,#0
 mov x9,#19
 movk x9,#0xa5a5,lsl #48
 cmp x19,x9
 b.ne 1f
 mov x9,#20
 movk x9,#0xa5a5,lsl #48
 cmp x20,x9
 b.ne 1f
 mov x9,#21
 movk x9,#0xa5a5,lsl #48
 cmp x21,x9
 b.ne 1f
 mov x9,#22
 movk x9,#0xa5a5,lsl #48
 cmp x22,x9
 b.ne 1f
 mov x9,#23
 movk x9,#0xa5a5,lsl #48
 cmp x23,x9
 b.ne 1f
 mov x9,#24
 movk x9,#0xa5a5,lsl #48
 cmp x24,x9
 b.ne 1f
 mov x9,#25
 movk x9,#0xa5a5,lsl #48
 cmp x25,x9
 b.ne 1f
 mov x9,#26
 movk x9,#0xa5a5,lsl #48
 cmp x26,x9
 b.ne 1f
 mov x9,#27
 movk x9,#0xa5a5,lsl #48
 cmp x27,x9
 b.ne 1f
 mov x9,#28
 movk x9,#0xa5a5,lsl #48
 cmp x28,x9
 b.ne 1f
 mov x9,#29
 movk x9,#0xa5a5,lsl #48
 cmp x29,x9
 b.ne 1f
 mov x9,sp
 tst x9,#15
 b.ne 1f
 mov x0,#1
1:
 ldp x29,x30,[sp,#80]
 ldp x27,x28,[sp,#64]
 ldp x25,x26,[sp,#48]
 ldp x23,x24,[sp,#32]
 ldp x21,x22,[sp,#16]
 ldp x19,x20,[sp],#96
 ret
.size sr_check_translated_abi,.-sr_check_translated_abi
)");
#endif

namespace {
uint32_t randomState=0x78394721;
uint32_t Random(){randomState^=randomState<<13;randomState^=randomState>>17;randomState^=randomState<<5;return randomState;}
CpuState State(uint32_t entry,unsigned mode){
    CpuState c;for(auto& r:c.regs)r=Random();c.eip=entry;c.SetEflags(Random());
    constexpr uint32_t edges[]={0,1,0x7fffffffu,0x80000000u,0xffffffffu,0xfffffffeu,0x7ffffffeu,0x80000001u,0x80,0xff,0xffff,0x10000};
    if(mode<36)for(unsigned i=0;i<8;++i)c.regs[i]=edges[(mode+i)%12];
    if(mode%9){const auto width=uint8_t(mode%3==0?8:mode%3==1?16:32);
        c.SetLazy(LazyOp(mode%9),width,Random(),Random(),Random());c.lazyCarryIn=Random()&1;}
    if(mode>=144 && mode<208){
        constexpr uint32_t bits[]={FLAG_CF,FLAG_PF,FLAG_AF,FLAG_ZF,FLAG_SF,FLAG_OF};
        uint32_t flags=c.eflags&~FLAGS_ARITH;for(unsigned i=0;i<6;++i)if((mode-144)&(1u<<i))flags|=bits[i];
        c.SetEflags(flags);
    }
    for(auto& s:c.segBase)s=Random();
    for(auto& s:c.segSel)s=uint16_t(Random());
    for(auto& x:c.xmm)for(auto& d:x.d)d=Random();
    c.st[0]=3.25;c.fpuLastIp=Random();
    return c;
}
void Equal(const CpuState& a,const CpuState& b){
    if(a.eip!=b.eip || std::memcmp(a.regs,b.regs,sizeof(a.regs)) || a.GetEflags()!=b.GetEflags() ||
       a.eflags!=b.eflags || a.lazyOp!=b.lazyOp || a.lazyWidth!=b.lazyWidth || a.lazyCarryIn!=b.lazyCarryIn ||
       a.lazyA!=b.lazyA || a.lazyB!=b.lazyB || a.lazyRes!=b.lazyRes){
        std::fprintf(stderr,"native/ref EIP %08x/%08x flags %08x/%08x lazy %u/%u A %08x/%08x B %08x/%08x res %08x/%08x carry %u/%u\n",a.eip,b.eip,a.GetEflags(),b.GetEflags(),unsigned(a.lazyOp),unsigned(b.lazyOp),a.lazyA,b.lazyA,a.lazyB,b.lazyB,a.lazyRes,b.lazyRes,a.lazyCarryIn,b.lazyCarryIn);
        for(unsigned i=0;i<8;++i)std::fprintf(stderr,"r%u %08x/%08x\n",i,a.regs[i],b.regs[i]);
        assert(false);
    }
    // Remaining architectural state,including x87/SSE and segments,is untouched.
    const size_t start=offsetof(CpuState,segSel);assert(!std::memcmp(reinterpret_cast<const uint8_t*>(&a)+start,reinterpret_cast<const uint8_t*>(&b)+start,sizeof(CpuState)-start));
}
struct Hook {
    unsigned calls=0,stop=0,mutate=0,redirect=0,retire=0;
    IGuestMemory* memory=nullptr;uint32_t address=0;CodeStore* store=nullptr;CodeToken token;
    static bool Run(void* context,const CpuState& state){auto& h=*static_cast<Hook*>(context);++h.calls;
        if(h.calls==h.mutate)h.memory->HostPointer(h.address)[0]^=1;
        if(h.calls==h.redirect)++const_cast<CpuState&>(state).eip;
        if(h.calls==h.retire)assert(h.store->Retire(h.token));
        return h.calls!=h.stop;
    }
};
}
#if defined(__aarch64__)
struct SuspendedTranslation {
    rt::HostFiber root,fiber;PagedGuestMemory memory;
    std::unique_ptr<CodeStore> store;arm64::Translation translated;
    CpuState state;arm64::Result result;bool entered=false,done=false;
    static bool Hook(void* context,const CpuState&){auto& s=*static_cast<SuspendedTranslation*>(context);
        if(!s.entered){s.entered=true;s.fiber.Suspend();}return true;}
    static void Start(void* context){auto& s=*static_cast<SuspendedTranslation*>(context);
        s.result=arm64::Execute(s.translated,*s.store,s.memory,s.state,32,&Hook,&s);s.done=true;}
};
#endif
int main(){
    constexpr uint32_t entry=0x11000;
    PagedGuestMemory memory,portable,reference;for(auto* m:{&memory,&portable,&reference}){assert(m->Map(0x10000,0x3000));m->TrackIntegerExecution(true);}
    reference.EnableIntegerFastPath(false);CodeStore store(memory.Identity());
    uint64_t layouts=0,cases=0,nodes=0;[[maybe_unused]] uint64_t hooked=0;rt::WorkloadProfile nativeProfile,irProfile,referenceProfile;
    nativeProfile.instructionLimit=irProfile.instructionLimit=referenceProfile.instructionLimit=10000000;
    auto load=[&](const std::vector<uint8_t>& code){for(auto* m:{&memory,&portable,&reference})m->LoadBytes(entry,code.data(),code.size());};
    auto compare=[&](const arm64::Translation& translated,CpuState initial,uint32_t budget,bool checkpoint){
        auto a=initial,b=initial,c=initial;const auto block=ir::Build(portable,entry);
        Hook first,second;
        arm64::Result native;
        {rt::WorkloadProfileScope scope(&nativeProfile);native=arm64::Execute(translated,store,memory,a,budget,checkpoint?&Hook::Run:nullptr,&first);}
#if defined(__aarch64__)
        assert(native.status==arm64::Status::Complete);
        ir::Result interpreted;
        {rt::WorkloadProfileScope scope(&irProfile);interpreted=ir::Execute(block,portable,b,budget,checkpoint?&Hook::Run:nullptr,&second);}
        assert(native.execution.reason==interpreted.reason && native.execution.executed==interpreted.executed && first.calls==second.calls);
        Interpreter interpreter(reference);
        {rt::WorkloadProfileScope scope(&referenceProfile);for(unsigned n=0;n<interpreted.executed;++n)assert(interpreter.Step(c)==StepResult::Continue);}
        Equal(a,b);Equal(a,c);nodes+=interpreted.executed;++cases;if(checkpoint)++hooked;
        assert(nativeProfile.opcodes==irProfile.opcodes && nativeProfile.opcodes==referenceProfile.opcodes);
        assert(nativeProfile.instructionCount==irProfile.instructionCount && nativeProfile.instructionCount==referenceProfile.instructionCount);
#else
        (void)b;(void)c;(void)block;(void)second;(void)checkpoint;
        assert(native.status==(budget?arm64::Status::ArchitectureUnavailable:arm64::Status::Complete));Equal(a,initial);
#endif
    };
    auto codeCase=[&](std::vector<uint8_t> code,unsigned samples){
        code.push_back(0xcc);load(code);const auto block=ir::Build(memory,entry);assert(block.Count());
        const auto image=arm64::Emit(block);assert(image && image.Bytes()<=arm64::Image::MaxWords*4);
        assert(image.Words()[0]==0xa9be53f3 && image.Words()[image.Bytes()/4-1]==0xd65f03c0);
        arm64::Translation translated;assert(arm64::Compile(store,memory,block,translated));++layouts;
        for(unsigned n=0;n<samples;++n)compare(translated,State(entry,n),32,n&1);
        assert(store.Retire(translated.Token()));assert(store.Stats().images==0);
    };
    for(unsigned kind=0;kind<8;++kind)for(unsigned dst=0;dst<8;++dst)for(unsigned src=0;src<8;++src){
        codeCase({uint8_t(kind*8+1),uint8_t(0xc0|(src<<3)|dst)},4);
        codeCase({uint8_t(kind*8+3),uint8_t(0xc0|(dst<<3)|src)},4);
    }
    for(unsigned op:{0x81u,0x83u})for(unsigned kind=0;kind<8;++kind)for(unsigned dst=0;dst<8;++dst)
        codeCase({uint8_t(op),uint8_t(0xc0|(kind<<3)|dst),0x80,0xff,0xff,0x7f},9);
    for(unsigned op:{0x89u,0x8bu,0x87u,0x85u})for(unsigned dst=0;dst<8;++dst)for(unsigned src=0;src<8;++src)
        codeCase({uint8_t(op),uint8_t(0xc0|(src<<3)|dst)},3);
    for(unsigned op=0x40;op<0x50;++op)codeCase({uint8_t(op)},36);
    for(unsigned reg=0;reg<8;++reg){codeCase({uint8_t(0xb8+reg),0xff,0xff,0xff,0x7f},9);codeCase({0xc7,uint8_t(0xc0|reg),0,0,0,0x80},9);codeCase({uint8_t(0x90+reg)},9);}
    for(unsigned cc=0;cc<16;++cc){codeCase({uint8_t(0x70+cc),0x80},208);codeCase({0x0f,uint8_t(0x80+cc),0xfe,0xff,0xff,0x7f},208);}
    codeCase({0xe9,0xff,0xff,0xff,0x7f},9);codeCase({0xeb,0x80},9);codeCase({0xa9,0xff,0xff,0xff,0x7f},9);
#if defined(__aarch64__)
    // Mixed dependency chains and every budget prefix;both bulk native execution
    // and one-node checkpoint entries resume at arbitrary valid node boundaries.
    for(unsigned pass=0;pass<2048;++pass){
        std::vector<uint8_t> code;
        for(unsigned n=0;n<8;++n){const auto dst=Random()%8,src=Random()%8;
            switch(Random()%6){
                case 0:code.insert(code.end(),{0x89,uint8_t(0xc0|(src<<3)|dst)});break;
                case 1:code.insert(code.end(),{0x83,uint8_t(0xc0|((Random()%8)<<3)|dst),uint8_t(Random())});break;
                case 2:code.push_back(uint8_t(0x40+Random()%16));break;
                case 3:code.insert(code.end(),{0x85,uint8_t(0xc0|(src<<3)|dst)});break;
                case 4:code.insert(code.end(),{0x87,uint8_t(0xc0|(src<<3)|dst)});break;
                default:code.insert(code.end(),{uint8_t((Random()%8)*8+1),uint8_t(0xc0|(src<<3)|dst)});break;
            }
        }
        code.insert(code.end(),{uint8_t(0x70+Random()%16),uint8_t(Random())});load(code);
        arm64::Translation translated;assert(arm64::Compile(store,memory,ir::Build(memory,entry),translated));++layouts;
        for(unsigned budget=0;budget<11;++budget)compare(translated,State(entry,pass+budget),budget,pass&1);
        for(unsigned i=1;i<9;++i){auto state=State(entry+translated.Source().At(i).offset,i);compare(translated,state,32,pass&1);}
        assert(store.Retire(translated.Token()));
    }
#endif
    std::vector<uint8_t> maximal;for(unsigned n=0;n<32;++n)maximal.insert(maximal.end(),{0xc7,0xc0,0x78,0x56,0x34,0x12});
    load(maximal);arm64::Translation translated;assert(arm64::Compile(store,memory,ir::Build(memory,entry),translated));
    for(unsigned budget=0;budget<34;++budget)compare(translated,State(entry,budget),budget,budget&1);
    assert(store.Retire(translated.Token()));
#if defined(__aarch64__)
    load({0x83,0xd0,0xff,0x40,0x75,0x80});
    assert(arm64::Compile(store,memory,ir::Build(memory,entry),translated));
    struct AbiCall {
        CpuState state;uint32_t count;
        static bool Run(void* context,const void* entry,size_t){auto& c=*static_cast<AbiCall*>(context);
            return sr_check_translated_abi(const_cast<void*>(entry),&c.state,0,c.count)==1;}
    } abi{State(entry,0),uint32_t(translated.Source().Count())};
    auto lease=store.Acquire(memory,translated.Token());assert(lease.Invoke(memory,&AbiCall::Run,&abi));
    assert(store.Retire(translated.Token()));lease={};
#endif
    // Guard failures and code-storage lifetime are observable without executing
    // ARM64 bytes on x86. No host-only result is labelled native translation.
    load({0xb8,1,0,0,0,0x83,0xd0,1,0x40,0xcc});
    assert(arm64::Compile(store,memory,ir::Build(memory,entry),translated));const auto snapshot=translated;
    auto state=State(entry,0),before=state;
    memory.Fault()={true,true,0x5678};
    auto result=arm64::Execute(translated,store,memory,state,0);assert(result.execution.reason==ir::Exit::Budget && !result.execution.executed);Equal(state,before);assert(memory.Fault().pending && memory.Fault().address==0x5678);
    result=arm64::Execute(translated,store,portable,state,32);assert(result.execution.reason==ir::Exit::WrongOwner);Equal(state,before);
    ++state.eip;before=state;result=arm64::Execute(translated,store,memory,state,32);assert(result.execution.reason==ir::Exit::WrongEntry);Equal(state,before);
    state.eip=entry;before=state;memory.HostPointer(entry)[1]^=1;
    result=arm64::Execute(translated,store,memory,state,32);assert(result.execution.reason==ir::Exit::CodeChanged);Equal(state,before);
    arm64::Translation refused;assert(!arm64::Compile(store,memory,translated.Source(),refused) && !refused);memory.HostPointer(entry)[1]^=1;
    assert(!memory.IntegerStats().native || memory.IntegerStats().native==nodes);
    arm64::Translation empty;result=arm64::Execute(empty,store,memory,state,32);assert(result.status==arm64::Status::Unavailable);
    {CodeStore limited(memory.Identity(),{4096,1});arm64::Translation one,two;
        assert(arm64::Compile(limited,memory,translated.Source(),one));
        assert(!arm64::Compile(limited,memory,translated.Source(),two) && !two && limited.LastError()==CodeError::LocalQuota);
        assert(limited.Stats().published==1);assert(limited.Retire(one.Token()));}
    CodeStore foreign(memory.Identity());result=arm64::Execute(translated,foreign,memory,state,32);assert(result.status==arm64::Status::Unavailable);Equal(state,before);
#if defined(__aarch64__)
    Hook stopping;stopping.stop=2;state=State(entry,0);
    result=arm64::Execute(translated,store,memory,state,32,&Hook::Run,&stopping);assert(result.execution.reason==ir::Exit::Checkpoint && result.execution.executed==1 && state.regs[0]==1);
    Hook mutation;mutation.mutate=2;mutation.memory=&memory;mutation.address=entry+6;state=State(entry,0);
    result=arm64::Execute(translated,store,memory,state,32,&Hook::Run,&mutation);assert(result.execution.reason==ir::Exit::CodeChanged && result.execution.executed==1);memory.HostPointer(entry+6)[0]^=1;
    Hook redirect;redirect.redirect=2;state=State(entry,0);
    result=arm64::Execute(translated,store,memory,state,32,&Hook::Run,&redirect);assert(result.execution.reason==ir::Exit::WrongEntry && result.execution.executed==1);
    Hook retirement;retirement.retire=2;retirement.store=&store;retirement.token=translated.Token();state=State(entry,0);
    result=arm64::Execute(translated,store,memory,state,32,&Hook::Run,&retirement);assert(result.status==arm64::Status::Unavailable && result.execution.executed==1 && state.eip==entry+5);
#else
    assert(store.Retire(translated.Token()));
#endif
    state.eip=entry;result=arm64::Execute(snapshot,store,memory,state,32);assert(result.status==arm64::Status::Unavailable);
    assert(store.Close()==CodeClose::Drained && GlobalCodeStats().images==0);
#if defined(__aarch64__)
    for(unsigned mode=0;mode<3;++mode){
        SuspendedTranslation suspended;assert(suspended.memory.Map(entry,4096));
        const uint8_t code[]={0xb8,1,0,0,0,0x83,0xd0,1,0x40,0xcc};
        suspended.memory.LoadBytes(entry,code,sizeof(code));suspended.state=State(entry,0);suspended.state.SetEflags(0x202);
        suspended.store=std::make_unique<CodeStore>(suspended.memory.Identity());
        assert(arm64::Compile(*suspended.store,suspended.memory,ir::Build(suspended.memory,entry),suspended.translated));
        assert(suspended.fiber.Initialize(&SuspendedTranslation::Start,&suspended));
        assert(suspended.fiber.Resume(suspended.root) && suspended.entered && !suspended.done);
        suspended.translated={}; // Wrapper owns its source before the callback.
        if(mode==1)suspended.store.reset();
        if(mode==2)suspended.memory.HostPointer(entry)[1]^=1;
        assert(GlobalCodeStats().images==1);
        assert(suspended.fiber.Resume(suspended.root) && suspended.done && suspended.fiber.Finished());
        if(mode==0)assert(suspended.result.status==arm64::Status::Complete && suspended.result.execution.executed==3 && suspended.state.regs[0]==3);
        if(mode==1)assert(suspended.result.status==arm64::Status::Unavailable && !suspended.result.execution.executed && GlobalCodeStats().images==0);
        if(mode==2)assert(suspended.result.execution.reason==ir::Exit::CodeChanged && !suspended.result.execution.executed);
        suspended.store.reset();assert(GlobalCodeStats().images==0);
    }
    {
        PagedGuestMemory m;assert(m.Map(entry,4096));CodeStore owner(m.Identity());
        m.Write8(entry,0x90);auto host=+[](void*,uint32_t pc){return pc==entry+1;};
        arm64::Translation t;assert(arm64::Compile(owner,m,ir::Build(m,entry,host),t));
        CpuState c;c.eip=entry;auto r=arm64::Execute(t,owner,m,c,32);assert(r.execution.reason==ir::Exit::HostBoundary && r.execution.executed==1 && c.eip==entry+1);
        assert(owner.Retire(t.Token()));
        // Missing tail remains a fetch boundary,not a fabricated native node.
        m.Write8(entry+4095,0x90);assert(arm64::Compile(owner,m,ir::Build(m,entry+4095),t));
        c.eip=entry+4095;r=arm64::Execute(t,owner,m,c,32);assert(r.execution.reason==ir::Exit::FetchUnavailable && r.execution.executed==1 && c.eip==entry+4096);
        assert(owner.Retire(t.Token()));
        assert(m.Map(0xfffff000,4096));m.Write8(0xffffffff,0x90);assert(arm64::Compile(owner,m,ir::Build(m,0xffffffff),t));
        c.eip=0xffffffff;r=arm64::Execute(t,owner,m,c,32);assert(r.execution.reason==ir::Exit::Tail && r.execution.executed==1 && !c.eip);assert(owner.Retire(t.Token()));
        const uint8_t faulting[]={0xbb,0,0,0,0,0x8b,0x03};m.LoadBytes(entry,faulting,sizeof(faulting));
        assert(arm64::Compile(owner,m,ir::Build(m,entry),t));c=CpuState{};c.eip=entry;
        r=arm64::Execute(t,owner,m,c,32);assert(r.execution.reason==ir::Exit::Unsupported && r.execution.executed==1 && c.eip==entry+5);
        const auto saved=c;Interpreter interpreter(m);assert(interpreter.Step(c)==StepResult::Exception && interpreter.LastException().code==EXC_ACCESS_VIOLATION);Equal(c,saved);assert(owner.Retire(t.Token()));
        // Accounting retains the same synthetic RDTSC delta and debug EIP.
        std::vector<uint8_t> nops(32,0x90);m.LoadBytes(entry,nops.data(),nops.size());assert(arm64::Compile(owner,m,ir::Build(m,entry),t));
        const uint8_t rdtsc[]={0x0f,0x31};m.LoadBytes(entry+100,rdtsc,2);c.eip=entry+100;assert(interpreter.Step(c)==StepResult::Continue);
        const uint64_t first=uint64_t(c.regs[0])|(uint64_t(c.regs[2])<<32);c.eip=entry;
        r=arm64::Execute(t,owner,m,c,32);assert(r.execution.executed==32 && g_lastKnownEip==entry+31);
        c.eip=entry+100;assert(interpreter.Step(c)==StepResult::Continue);assert((uint64_t(c.regs[0])|(uint64_t(c.regs[2])<<32))-first==132);
        assert(owner.Retire(t.Token()));
    }
#endif
    load({0x8b,0x00});assert(!arm64::Compile(foreign,memory,ir::Build(memory,entry),refused));
    state=CpuState{};state.eip=entry;Interpreter fallback(memory);assert(fallback.Step(state)==StepResult::Exception && fallback.LastException().code==EXC_ACCESS_VIOLATION && state.eip==entry);
#if defined(__aarch64__)
    assert(cases>50000 && nodes>100000 && hooked>10000);
    assert(memory.IntegerStats().native==nodes+4 && !memory.IntegerStats().ir);
    // Three extra successful one-instruction prefixes above +retirement prefix.
    assert(portable.IntegerStats().ir==nodes);
    FILE* report=std::tmpfile();assert(report);rt::WriteIntegerExecutionReport(report,memory,true);std::rewind(report);
    char text[2048]={};assert(std::fread(text,1,sizeof(text)-1,report)>0);std::fclose(report);
    char expected[128];std::snprintf(expected,sizeof(expected),"CPU native ARM64 steps: %llu",(unsigned long long)memory.IntegerStats().native);
    assert(std::strstr(text,expected));
    std::printf("ARM64 emitter PASS: %llu native/reference/IR cases,%llu native translated nodes,%llu checkpoint comparisons;all register ALU/MOV/XCHG/TEST/INC/DEC/Jcc,exact raw flags/state/opcodes/budgets/source/owner/retirement,callee-saved ABI,parked checkpoint/source/store lifetime,RDTSC and fault fallback;not production dispatch or Switch FPS\n",(unsigned long long)cases,(unsigned long long)nodes,(unsigned long long)hooked);
#else
    assert(!cases && !nodes && !memory.IntegerStats().native);
    std::printf("ARM64 emitter host PASS: %llu emitted/published layouts,guard/refusal/reference-fault fallback;native execution unavailable on this architecture (ARM64/QEMU is required)\n",(unsigned long long)layouts);
#endif
}
