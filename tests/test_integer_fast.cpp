#include "source/cpu/interpreter.h"
#include "source/runtime/workload_profile.h"
#include "source/runtime/integer_execution_report.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
using namespace cpu;
static uint32_t seed=0x135791;static uint32_t Random(){seed=1664525u*seed+1013904223u;return seed;}
static void Equal(const CpuState& a,const CpuState& b){
    assert(a.eip==b.eip && !std::memcmp(a.regs,b.regs,sizeof(a.regs)));
    assert(a.eflags==b.eflags && a.lazyOp==b.lazyOp && a.lazyWidth==b.lazyWidth && a.lazyCarryIn==b.lazyCarryIn);
    assert(a.lazyA==b.lazyA && a.lazyB==b.lazyB && a.lazyRes==b.lazyRes && a.GetEflags()==b.GetEflags());
    assert(!std::memcmp(a.segBase,b.segBase,sizeof(a.segBase)) && !std::memcmp(a.xmm,b.xmm,sizeof(a.xmm)));
}
int main(){
    PagedGuestMemory fast,reference;assert(fast.Map(0x10000,0x3000) && reference.Map(0x10000,0x3000));
    reference.EnableIntegerFastPath(false);fast.TrackIntegerExecution(true);reference.TrackIntegerExecution(true);
    uint64_t cases=0;rt::WorkloadProfile pf,pr;pf.instructionLimit=pr.instructionLimit=1000000;
    auto probe=[&](const std::vector<uint8_t>& code,uint32_t address,bool mapped=false){
        fast.LoadBytes(address,code.data(),code.size());reference.LoadBytes(address,code.data(),code.size());
        CpuState a;for(auto& reg:a.regs)reg=Random();a.eip=address;
        if(mapped){for(auto& reg:a.regs)reg=0x10040;a.segBase[SEG_SS]=(Random()&1)?0x1000:0;}
        a.SetEflags((Random()&0xcd5u)|0x202u);if(Random()&1)a.SetLazy(LazyOp::Add,32,Random(),Random(),Random());
        CpuState b=a;Interpreter fi(fast),ri(reference);Instruction ia,ib;StepResult sa,sb;
        {rt::WorkloadProfileScope scope(&pf);sa=fi.Step(a,&ia);}
        {rt::WorkloadProfileScope scope(&pr);sb=ri.Step(b,&ib);}
        assert(sa==sb && ia.length==ib.length && ia.rawOpcodeForDebug==ib.rawOpcodeForDebug);Equal(a,b);
        if(sa==StepResult::Exception || sa==StepResult::UnknownOpcode){
            const auto x=fi.LastException(),y=ri.LastException();assert(x.code==y.code && x.eip==y.eip && x.faultAddress==y.faultAddress && x.write==y.write);
        }
        for(uint32_t at=0x10000;at<0x10020;++at)assert(fast.Read8(at)==reference.Read8(at));
        if(mapped)for(uint32_t offset=0;offset<256;++offset){
            assert(fast.Read8(0x10000+offset)==reference.Read8(0x10000+offset));
            assert(fast.Read8(0x11000+offset)==reference.Read8(0x11000+offset));
        }
        ++cases;
    };
    for(unsigned n=0;n<32;++n){
        for(unsigned op=0;op<256;++op){
            if((op<0x40 && ((op&7)==1 || (op&7)==3 || (op&7)==5)) ||
               (op>=0x40 && op<=0x4f) || (op>=0x90 && op<=0x97) || (op>=0xb8 && op<=0xbf) ||
               op==0x69 || op==0x6b || op==0x81 || op==0x83 || op==0x85 || op==0x87 || op==0x89 || op==0x8b || op==0xa9 || op==0xc7 || op==0xc1 || op==0xd1 || op==0xd3){
                for(unsigned mr=0xc0;mr<256;++mr){
                    std::vector<uint8_t> code={uint8_t(op),uint8_t(mr),uint8_t(Random()),uint8_t(Random()),uint8_t(Random()),uint8_t(Random()),0x90};
                    probe(code,(n&1)?0x10ffd:0x11020);
                }
            }
        }
        for(unsigned mr=0xc0;mr<256;++mr)probe({0x0f,0xaf,uint8_t(mr)},0x10fff);
    }

    for(unsigned pass=0;pass<16;++pass){
        for(uint8_t op:{0x01,0x03,0x09,0x0b,0x11,0x13,0x19,0x1b,0x21,0x23,0x29,0x2b,0x31,0x33,0x39,0x3b,0x81,0x83,0x85,0x89,0x8b,0x8d,0xc7})
            for(unsigned mod=0;mod<3;++mod)for(unsigned reg=0;reg<8;++reg)for(unsigned rm=0;rm<8;++rm){
                std::vector<uint8_t> code={op,uint8_t((mod<<6)|(reg<<3)|rm)};
                if(rm==4)code.push_back(0x24); // SIB, no index,ESP base
                if(mod==0 && rm==5){code.insert(code.end(),{0x40,0x00,0x01,0x00});}
                if(mod==1)code.push_back(0xfd);
                if(mod==2)code.insert(code.end(),{31,0,0,0});
                if(op==0x81 || op==0x83 || op==0xc7)code.insert(code.end(),{0x7f,0x80,0xff,0x01});
                probe(code,0x12020,true);
            }
        for(uint8_t op=0x50;op<=0x5f;++op)probe({op},0x12020,true);
        probe({0x68,0x78,0x56,0x34,0x12},0x12020,true);probe({0x6a,0x80},0x12020,true);
    }
    for(auto code:std::vector<std::vector<uint8_t>>{{0x66,0x83,0xc0,1},{0x67,0x89,0xc8},{0xf0,0x01,0xc8},
        {0x8b,0x00},{0x83,0x00,1},{0xf7,0xf1},{0x0f,0x0b},{0xc7,0xc8,0,0,0,0},{0xf3,0xa4},{0xd9,0xe8},{0x0f,0x57,0xc0}}) {
        // REP receives a bounded ECX only through a separate deterministic case below.
        if(code[0]!=0xf3)probe(code,0x11020);
    }
    // A successful store may overwrite its own opcode;diagnostics still report
    // the fetched instruction, and the next fetch observes those new bytes.
    probe({0x89,0x05,0x20,0x20,0x01,0x00},0x12020,true);
    // Missing instruction tails,including immediate and ModRM at a mapping edge.
    fast.Unmap(0x11000,0x2000);reference.Unmap(0x11000,0x2000);
    for(auto code:std::vector<std::vector<uint8_t>>{{0xb8},{0x83},{0x81,0xc0},{0x69,0xc0},{0x0f,0xaf},{0xc1,0xc0}})
        probe(code,0x11000-uint32_t(code.size()));
    // Cross-page mapped-memory refusal retains the reference's exact fault and
    // partial-write behavior when the second data page is absent.
    probe({0xc7,0x05,0xfe,0x0f,0x01,0x00,0x78,0x56,0x34,0x12},0x10080);
    assert(fast.Read16(0x10ffe)==reference.Read16(0x10ffe));
    probe({0x8b,0x05,0xfe,0x0f,0x01,0x00},0x10080);
    assert(fast.Map(0x11000,0x1000) && reference.Map(0x11000,0x1000));
    // Raw host alias writes and unmap/remap must affect the very next instruction.
    const uint8_t mov[]={0xb8,1,0,0,0};fast.LoadBytes(0x11000,mov,5);CpuState c;c.eip=0x11000;Interpreter fi(fast);
    assert(fi.Step(c)==StepResult::Continue && c.regs[0]==1);
    fast.HostPointer(0x11001)[0]=7;c.eip=0x11000;assert(fi.Step(c)==StepResult::Continue && c.regs[0]==7);
    fast.Unmap(0x11000,0x1000);assert(fast.Map(0x11000,0x1000));fast.LoadBytes(0x11000,mov,5);c.eip=0x11000;
    assert(fi.Step(c)==StepResult::Continue && c.regs[0]==1);
    auto timestampDelta=[&](bool enabled){
        fast.EnableIntegerFastPath(enabled);const uint8_t rdtsc[]={0x0f,0x31};fast.LoadBytes(0x11000,rdtsc,2);
        CpuState state;state.eip=0x11000;assert(fi.Step(state)==StepResult::Continue);
        const uint64_t first=uint64_t(state.regs[0])|(uint64_t(state.regs[2])<<32);
        fast.Write8(0x11010,0x90);for(unsigned n=0;n<32;++n){state.eip=0x11010;assert(fi.Step(state)==StepResult::Continue);}
        state.eip=0x11000;assert(fi.Step(state)==StepResult::Continue);
        return (uint64_t(state.regs[0])|(uint64_t(state.regs[2])<<32))-first;
    };
    assert(timestampDelta(true)==132 && timestampDelta(false)==132);fast.EnableIntegerFastPath(true);
    assert(pf.instructionCount==cases && pr.instructionCount==cases && pf.opcodes==pr.opcodes);
    assert(fast.IntegerStats().mapped>50000);
    assert(fast.IntegerStats().fast>100000 && fast.IntegerStats().reference>0 && !reference.IntegerStats().fast);
    // Counters/policy belong to the memory owner; new/suspended sessions do not leak.
    {PagedGuestMemory fresh;assert(fresh.IntegerFastPathEnabled() && !fresh.IntegerStats().fast);fresh.EnableIntegerFastPath(false);}
    assert(fast.IntegerFastPathEnabled() && !reference.IntegerFastPathEnabled());
    FILE* report=std::tmpfile();assert(report);rt::WriteIntegerExecutionReport(report,fast,true);assert(!std::ferror(report));
    std::rewind(report);char text[2048]={};assert(std::fread(text,1,sizeof(text)-1,report)>0);std::fclose(report);
    assert(std::strstr(text,"CPU fast memory/LEA/stack steps:") && std::strstr(text,"CPU counters enabled: 1"));
    std::printf("Integer fast PASS (%llu differential cases;register+mapped ALU/MOV/LEA/stack/SIB/segment forms,shift/imul encodings,exact lazy+materialized flags,profile/diagnostic parity,prefix/memory/illegal/fault fallback,cross-page fetch,raw mutation/remap,owner isolation;not dynarec/FPS)\n",(unsigned long long)cases);
}
