#include "source/cpu/block_ir.h"
#include "source/cpu/interpreter.h"
#include "source/runtime/workload_profile.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <new>
#include <vector>
using namespace cpu;
namespace {
uint32_t seed=0x872693;
uint32_t Random(){seed=seed*1664525u+1013904223u;return seed;}
void Equal(const CpuState& a,const CpuState& b){
    assert(a.eip==b.eip && !std::memcmp(a.regs,b.regs,sizeof(a.regs)) && a.GetEflags()==b.GetEflags());
    assert(a.eflags==b.eflags && a.lazyOp==b.lazyOp && a.lazyWidth==b.lazyWidth && a.lazyCarryIn==b.lazyCarryIn);
    assert(a.lazyA==b.lazyA && a.lazyB==b.lazyB && a.lazyRes==b.lazyRes);
    assert(!std::memcmp(a.segBase,b.segBase,sizeof(a.segBase)) && !std::memcmp(a.xmm,b.xmm,sizeof(a.xmm)));
}
struct Hook {
    PagedGuestMemory* memory=nullptr;unsigned calls=0,stop=0,mutate=0;uint32_t target=0;
    static bool Run(void* context,const CpuState&){auto& h=*static_cast<Hook*>(context);++h.calls;
        if(h.calls==h.mutate)h.memory->HostPointer(h.target)[0]^=1;
        return h.calls!=h.stop;
    }
};
}
int main(){
    PagedGuestMemory memory,reference;assert(memory.Map(0x10000,0x3000) && reference.Map(0x10000,0x3000));
    reference.EnableIntegerFastPath(false);memory.TrackIntegerExecution(true);reference.TrackIntegerExecution(true);
    uint64_t cases=0,executed=0;rt::WorkloadProfile actual,expected;actual.instructionLimit=expected.instructionLimit=10000000;
    auto test=[&](const std::vector<uint8_t>& code,uint32_t address,uint32_t budget){
        memory.LoadBytes(address,code.data(),code.size());reference.LoadBytes(address,code.data(),code.size());
        CpuState a;for(auto& r:a.regs)r=Random();a.eip=address;a.SetEflags(Random()|0x202);
        if(Random()&1)a.SetLazy(LazyOp::Add,32,Random(),Random(),Random());
        CpuState b=a;
        const auto block=ir::Build(memory,address);assert(block.Count()>0 && block.Matches(memory));
        ir::Result result;
        {rt::WorkloadProfileScope scope(&actual);result=ir::Execute(block,memory,a,budget);}
        Interpreter interpreter(reference);
        {rt::WorkloadProfileScope scope(&expected);for(unsigned n=0;n<result.executed;++n)assert(interpreter.Step(b)==StepResult::Continue);}
        Equal(a,b);assert(result.executed<=budget && result.executed<=block.Count());executed+=result.executed;++cases;
        assert(actual.instructionCount==expected.instructionCount && actual.opcodes==expected.opcodes);
    };
    // Exhaustive unprefixed register ALU/immediate/move metadata+execution across
    // all register selections, including aliases and lazy carry dependencies.
    for(unsigned pass=0;pass<12;++pass){
        for(unsigned op=0;op<256;++op){
            const bool included=(op<0x40 && ((op&7)==1 || (op&7)==3 || (op&7)==5)) ||
                (op>=0x40 && op<=0x4f) || (op>=0x90 && op<=0x97) || (op>=0xb8 && op<=0xbf) ||
                op==0x81 || op==0x83 || op==0x85 || op==0x87 || op==0x89 || op==0x8b || op==0xa9 || op==0xc7;
            if(!included)continue;
            for(unsigned mr=0xc0;mr<256;++mr){if(op==0xc7 && ((mr>>3)&7))continue;
                std::vector<uint8_t> code={uint8_t(op),uint8_t(mr),uint8_t(Random()),uint8_t(Random()),uint8_t(Random()),uint8_t(Random()),0xcc};
                test(code,(pass&1)?0x10ffd:0x11020,1);
            }
        }
        for(unsigned cc=0;cc<16;++cc){
            test({uint8_t(0x70+cc),0x80},0x11020,32);test({0x0f,uint8_t(0x80+cc),0xff,0xff,0xff,0x7f},0x11020,32);
        }
    }
    // Mixed blocks exercise data dependencies and every prefix budget. These
    // independently compute flag/register state through the reference interpreter.
    for(unsigned pass=0;pass<20000;++pass){
        std::vector<uint8_t> code;
        for(unsigned n=0;n<8;++n){const unsigned dst=Random()%8,src=Random()%8;
            switch(Random()%5){
                case 0:code.insert(code.end(),{0x89,uint8_t(0xc0|(src<<3)|dst)});break;
                case 1:code.insert(code.end(),{0x83,uint8_t(0xc0|((Random()%8)<<3)|dst),uint8_t(Random())});break;
                case 2:code.push_back(uint8_t(0x40+(Random()%16)));break;
                case 3:code.insert(code.end(),{0x85,uint8_t(0xc0|(src<<3)|dst)});break;
                default:code.insert(code.end(),{0x31,uint8_t(0xc0|(src<<3)|dst)});break;
            }
        }
        code.insert(code.end(),{uint8_t(0x70+(Random()%16)),uint8_t(Random())});
        test(code,0x11020,pass%11);
    }
    std::vector<uint8_t> maximal;
    for(unsigned n=0;n<32;++n)maximal.insert(maximal.end(),{0xc7,0xc0,0x78,0x56,0x34,0x12});
    test(maximal,0x11000,32);const auto maximum=ir::Build(memory,0x11000);
    assert(maximum.Count()==32 && maximum.SourceSize()==192 && maximum.At(31).offset==186);
    const uint8_t sequence[]={0xb8,1,0,0,0,0x83,0xc0,2,0x83,0xd0,0,0x75,0xf3};
    memory.LoadBytes(0x11000,sequence,sizeof(sequence));const auto block=ir::Build(memory,0x11000);
    assert(block.Count()==4 && block.Boundary()==ir::Stop::Branch);
    assert(block.At(0).op==ir::Op::Move && block.At(0).registersRead==0 && block.At(0).registersWritten==1);
    assert(block.At(2).flagsRead==FLAG_CF && block.At(2).flagsWrite==FLAGS_ARITH);
    assert(block.At(3).flagsRead==FLAG_ZF && block.At(3).flagsWrite==0 && block.At(3).registersWritten==0);
    CpuState state;state.eip=block.Entry();Hook stop{&memory,0,2,0,0};
    auto r=ir::Execute(block,memory,state,32,&Hook::Run,&stop);assert(r.reason==ir::Exit::Checkpoint && r.executed==1 && state.regs[0]==1 && state.eip==0x11005);
    r=ir::Execute(block,memory,state,1);assert(r.reason==ir::Exit::Budget && r.executed==1 && state.regs[0]==3);
    memory.Fault()={true,true,0x1234};state.eip=block.Entry();r=ir::Execute(block,memory,state,0);
    assert(r.reason==ir::Exit::Budget && !r.executed && memory.Fault().pending && memory.Fault().address==0x1234);
    // No retained alias: mutation during a checkpoint or before entry refuses
    // the next instruction without stale work,even when the changed byte is later.
    Hook mutation{&memory,0,0,2,0x11006};state.eip=block.Entry();
    r=ir::Execute(block,memory,state,32,&Hook::Run,&mutation);
    assert(r.reason==ir::Exit::CodeChanged && r.executed==1 && state.eip==0x11005);
    assert(!block.Matches(memory));memory.LoadBytes(0x11000,sequence,sizeof(sequence));assert(block.Matches(memory));
    state=CpuState{};state.eip=block.Entry();auto redirected=state;++redirected.eip;
    auto redirect=+[](void* context,const CpuState&){++static_cast<CpuState*>(context)->eip;return true;};
    r=ir::Execute(block,memory,state,32,redirect,&state);assert(r.reason==ir::Exit::WrongEntry && !r.executed);Equal(state,redirected);
    reference.LoadBytes(0x11000,sequence,sizeof(sequence));state.eip=block.Entry();
    r=ir::Execute(block,reference,state,32);assert(r.reason==ir::Exit::WrongOwner && !r.executed);
    state.eip=0x11001;r=ir::Execute(block,memory,state,1);assert(r.reason==ir::Exit::WrongEntry && !r.executed);
    memory.Unmap(0x11000,0x1000);assert(!block.Matches(memory));assert(memory.Map(0x11000,0x1000));
    memory.LoadBytes(0x11000,sequence,sizeof(sequence));assert(block.Matches(memory)); // current identical bytes are valid,not retained host pointers
    // Prefixes/data accesses/calls/faults remain for the full interpreter.
    for(auto code:std::vector<std::vector<uint8_t>>{{0x66,0x89,0xc8},{0x89,0},{0xe8,0,0,0,0},{0xc3},{0x0f,0x0b},{0xf3,0xa4},{0xf0,0x01,0xc8}}){
        memory.LoadBytes(0x11000,code.data(),code.size());const auto refused=ir::Build(memory,0x11000);
        assert(refused.Count()==0 && refused.Boundary()==ir::Stop::Unsupported);
    }
    std::vector<uint8_t> nops(40,0x90);memory.LoadBytes(0x11000,nops.data(),nops.size());
    const auto full=ir::Build(memory,0x11000);assert(full.Count()==32 && full.SourceSize()==32 && full.Boundary()==ir::Stop::Capacity);
    auto delta=[&](bool portable){
        const uint8_t rdtsc[]={0x0f,0x31};memory.LoadBytes(0x10080,rdtsc,2);Interpreter interpreter(memory);CpuState c;c.eip=0x10080;
        assert(interpreter.Step(c)==StepResult::Continue);const uint64_t first=uint64_t(c.regs[0])|(uint64_t(c.regs[2])<<32);
        c.eip=full.Entry();if(portable){const auto r=ir::Execute(full,memory,c,32);assert(r.executed==32 && r.reason==ir::Exit::Tail);}
        else for(unsigned i=0;i<32;++i)assert(interpreter.Step(c)==StepResult::Continue);
        assert(g_lastKnownEip==full.Entry()+31);c.eip=0x10080;assert(interpreter.Step(c)==StepResult::Continue);
        return (uint64_t(c.regs[0])|(uint64_t(c.regs[2])<<32))-first;
    };
    assert(delta(true)==132 && delta(false)==132);
    auto host=+[](void*,uint32_t pc){return pc==0x11004;};const auto boundary=ir::Build(memory,0x11000,host);
    assert(boundary.Count()==4 && boundary.Boundary()==ir::Stop::HostBoundary);
    state.eip=boundary.Entry();r=ir::Execute(boundary,memory,state,32);assert(r.reason==ir::Exit::HostBoundary && r.executed==4 && state.eip==0x11004);
    const auto hostEntry=ir::Build(memory,0x11004,host);r=ir::Execute(hostEntry,memory,state,32);assert(r.reason==ir::Exit::HostBoundary && !r.executed);
    memory.Unmap(0x11000,0x2000);memory.Write8(0x10fff,0xb8);memory.Fault()={true,true,0x8888};
    const auto partial=ir::Build(memory,0x10fff);assert(!partial.Count() && partial.Boundary()==ir::Stop::FetchUnavailable);
    assert(memory.Fault().pending && memory.Fault().address==0x8888); // preview does not consume/raise a fault
    state=CpuState{};state.eip=0x10fff;const auto before=state;
    r=ir::Execute(partial,memory,state,32);assert(r.reason==ir::Exit::FetchUnavailable && !r.executed);Equal(state,before);
    Interpreter fault(memory);assert(fault.Step(state)==StepResult::Exception);
    assert(fault.LastException().code==EXC_ACCESS_VIOLATION && fault.LastException().eip==0x10fff && fault.LastException().faultAddress==0x11000);
    Equal(state,before);

    assert(memory.Map(0xfffff000,0x1000));memory.Write8(0xffffffff,0x90);
    const auto top=ir::Build(memory,0xffffffff);assert(top.Count()==1 && top.End()==0 && top.Boundary()==ir::Stop::AddressBoundary);
    state.eip=0xffffffff;r=ir::Execute(top,memory,state,32);assert(r.executed==1 && state.eip==0);
    // A new address space at the same native address still has a fresh identity.
    alignas(PagedGuestMemory) unsigned char storage[sizeof(PagedGuestMemory)];
    auto* first=new(storage) PagedGuestMemory();assert(first->Map(0x10000,0x1000));first->Write8(0x10000,0x90);
    const auto old=ir::Build(*first,0x10000);const auto identity=first->Identity();first->~PagedGuestMemory();
    auto* second=new(storage) PagedGuestMemory();assert(second->Identity()!=identity && !old.BelongsTo(*second));second->~PagedGuestMemory();
    assert(memory.IntegerStats().ir>=executed && memory.IntegerStats().ir>100000);
    std::printf("Block IR PASS (%llu differential cases,%llu executed nodes;register ALU/moves/flags/branches,budgets/checkpoints,source mutation/remap,owner ABA,fetch/host/fallback boundaries,metadata/census parity;not ARM64 dynarec/FPS)\n",(unsigned long long)cases,(unsigned long long)executed);
}
