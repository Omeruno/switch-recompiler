#include "source/runtime/guest_session.h"
#include "source/runtime/guest_execution.h"
#include "source/runtime/workload_profile.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
constexpr uint32_t Entry=0x10000,DataPc=Entry+10;
struct Case {
    const char* name;std::vector<uint8_t> code;
    uint32_t base=0x20ffe,sp=0x22800,ss=0,fs=0;
    bool split=false,fault=true,write=false,fetch=false;
    unsigned effect=0; //1=mapped store,2=PUSH ESP,3=completed CALL return slot,4=POP ESP
};
struct Result {
    cpu::CpuState cpu;cpu::CpuException exception;cpu::StepResult status=cpu::StepResult::Continue;
    std::array<uint8_t,12288> data{};rt::WorkloadProfile profile;rt::EngineCensus engines;
    uint64_t native=0,quanta=0;
};
Result Run(const Case& spec,unsigned mode){Result out;
    {
        rt::GuestSession session;auto& p=session.ProcessObject();auto& m=session.Memory();auto& thread=p.MainThread();
        assert(m.Map(Entry,0x2000) && m.Map(0x20000,0x1000) && m.Map(0x22000,0x1000));
        if(spec.split)assert(m.Map(0x21000,0x1000));
        for(uint32_t i=0;i<0x3000;++i)if(m.IsMapped(0x20000+i))m.Write8(0x20000+i,(i>=0x1000 && i<0x2000)?0x5a:0xa5);
        const uint8_t prefix[]={0xb8,0x78,0x56,0x34,0x12,0x83,0xc0,3,0x40,0x90};
        m.LoadBytes(Entry,prefix,sizeof(prefix));m.LoadBytes(DataPc,spec.code.data(),spec.code.size());m.Write8(DataPc+uint32_t(spec.code.size()),0xf4);
        thread.id=1;thread.status=rt::GuestThreadStatus::Runnable;thread.cpu.eip=Entry;thread.cpu.ESP()=spec.sp;
        thread.cpu.regs[3]=spec.base;thread.cpu.segBase[cpu::SEG_SS]=spec.ss;thread.cpu.segBase[cpu::SEG_FS]=spec.fs;thread.cpu.SetEflags(0x203);
        assert(p.Scheduler().Add(thread));m.EnableIntegerFastPath(mode!=0);m.EnableNativeExecution(mode>=2);m.EnableNativeBatching(mode==3);
        if(mode>=2){cpu::Interpreter warm(m);for(unsigned n=0;n<cpu::NativeCache::HotVisits;++n){auto copy=thread.cpu;assert(warm.Step(copy)==cpu::StepResult::Continue);}}
        m.TrackIntegerExecution(true);bool ended=false;
        {rt::EngineCensusScope bind(out.profile,out.engines);rt::WorkloadProfileScope profile(&out.profile);
            rt::GuestExecution execution(p,[&](rt::GuestThread& t){cpu::Interpreter interpreter(m);auto run=rt::GuestExecution::ExecuteCpu(interpreter,t.cpu);assert(run.executed);
                if(run.status==cpu::StepResult::Continue)return true;
                out.status=run.status;out.exception=interpreter.LastException();out.cpu=t.cpu;ended=true;return false;
            });
            for(;out.quanta<32;++out.quanta)if(execution.RunOne()==rt::GuestExecution::Result::Stopped){++out.quanta;break;}
        }
        assert(ended && !out.profile.engines && !m.Fault().pending);
        if(spec.fault){
            if(out.status!=cpu::StepResult::Exception || out.exception.code!=cpu::EXC_ACCESS_VIOLATION || out.exception.faultAddress!=(spec.fetch?spec.base:0x21000))
                std::fprintf(stderr,"boundary %s mode%u status%u code%08x fault%08x write%u eip%08x\n",spec.name,mode,unsigned(out.status),out.exception.code,out.exception.faultAddress,unsigned(out.exception.write),out.cpu.eip);
            assert(out.status==cpu::StepResult::Exception && out.exception.code==cpu::EXC_ACCESS_VIOLATION);
            assert(out.exception.faultAddress==(spec.fetch?spec.base:0x21000) && out.exception.write==spec.write);
            assert(out.cpu.eip==(spec.fetch?spec.base:DataPc) && out.exception.eip==out.cpu.eip);
            assert(out.cpu.regs[0]==0x1234567c);
        }else{assert(out.status==cpu::StepResult::Halted && !out.exception.code && out.cpu.eip==DataPc+spec.code.size()+1);
            if(!spec.effect)assert(out.cpu.regs[0]==0x5a5aa5a5);
        }
        const uint32_t expectedSp=spec.effect==2 || spec.effect==3?spec.sp-4:spec.effect==4?0xa5a5a5a5:spec.sp;
        assert(out.cpu.ESP()==expectedSp);
        std::array<uint8_t,12288> expected{};
        for(uint32_t i=0;i<expected.size();++i){const auto addr=0x20000+i;if(m.IsMapped(addr)){out.data[i]=m.Read8(addr);expected[i]=(i>=0x1000 && i<0x2000)?0x5a:0xa5;}}
        if(spec.effect && spec.effect!=4){const uint32_t address=spec.effect==1?spec.base:expectedSp+spec.ss;
            const uint32_t value=spec.effect==1?0x1234567c:spec.effect==2?spec.sp:DataPc+uint32_t(spec.code.size());
            for(unsigned byte=0;byte<4;++byte)expected[address-0x20000+byte]=uint8_t(value>>(byte*8));
        }
        assert(out.data==expected); // In particular,failed stores/CALL/PUSH commit no partial bytes.
        const auto& stats=m.IntegerStats();out.native=stats.native;
        assert(out.profile.instructionCount==stats.fast+stats.reference+stats.native && out.engines.instructionCount==out.profile.instructionCount);
        for(size_t opcode=0;opcode<rt::EngineCensus::OpcodeSlots;++opcode){uint64_t sum=0;for(const auto& row:out.engines.opcodes)sum+=row[opcode];assert(sum==out.profile.opcodes[opcode]);}
        assert(!out.engines.totals[size_t(rt::ExecutionEngine::Unclassified)]);
        if(mode>=2 && m.NativeArchitectureAvailable())assert(out.native==(!std::strcmp(spec.name,"load-split-blocks")?5u:4u));else assert(!out.native);
    }
    assert(!cpu::GlobalCodeStats().images);return out;
}
int main(){
    std::vector<Case> cases;
    cases.push_back({"load-missing",{0x8b,0x03},0x21000});
    cases.push_back({"store-missing",{0x89,0x03},0x21000});cases.back().write=true;
    cases.push_back({"load-cross-hole",{0x8b,0x03}});
    cases.push_back({"store-cross-hole",{0x89,0x03}});cases.back().write=true;
    cases.push_back({"load-split-blocks",{0x8b,0x03}});cases.back().split=true;cases.back().fault=false;
    cases.push_back({"store-split-blocks",{0x89,0x03}});cases.back().split=true;cases.back().fault=false;cases.back().effect=1;
    cases.push_back({"push-cross-hole",{0x53}});cases.back().sp=0x21002;cases.back().write=true;
    cases.push_back({"pop-cross-hole",{0x5b}});cases.back().sp=0x20ffe;
    cases.push_back({"pop-esp-cross-hole",{0x5c}});cases.back().sp=0x20ffe;
    cases.push_back({"call-push-cross-hole",{0xe8,0x20,0,0,0}});cases.back().sp=0x21002;cases.back().write=true;
    cases.push_back({"indirect-target-cross-hole",{0xff,0x13}});
    cases.push_back({"ret-cross-hole",{0xc3}});cases.back().sp=0x20ffe;
    cases.push_back({"ss-push-cross-hole",{0x53}});cases.back().sp=0x20002;cases.back().ss=0x1000;cases.back().write=true;
    cases.push_back({"fs-load-cross-hole",{0x64,0x8b,0x03},0x1fffe});cases.back().fs=0x1000;
    cases.push_back({"push-original-esp",{0x54}});cases.back().fault=false;cases.back().effect=2;
    cases.push_back({"pop-overrides-esp",{0x5c}});cases.back().fault=false;cases.back().effect=4;
    cases.push_back({"completed-call-then-fetch-fault",{0xff,0xd3},0x30000});cases.back().fetch=true;cases.back().effect=3;
    unsigned compared=0;
    for(const auto& spec:cases){const auto reference=Run(spec,0);for(unsigned mode=1;mode<4;++mode){const auto value=Run(spec,mode);
        assert(value.status==reference.status && value.data==reference.data && value.quanta==reference.quanta);
        assert(value.profile.instructionCount==reference.profile.instructionCount && value.profile.opcodes==reference.profile.opcodes && value.profile.hostCount==reference.profile.hostCount);
        const auto& a=value.cpu;const auto& b=reference.cpu;
        assert(a.eip==b.eip && !std::memcmp(a.regs,b.regs,sizeof(a.regs)) && a.GetEflags()==b.GetEflags());
        assert(a.eflags==b.eflags && a.lazyOp==b.lazyOp && a.lazyWidth==b.lazyWidth && a.lazyCarryIn==b.lazyCarryIn && a.lazyA==b.lazyA && a.lazyB==b.lazyB && a.lazyRes==b.lazyRes);
        assert(!std::memcmp(a.segBase,b.segBase,sizeof(a.segBase)) && value.exception.code==reference.exception.code && value.exception.eip==reference.exception.eip && value.exception.faultAddress==reference.exception.faultAddress && value.exception.write==reference.exception.write);
        ++compared;
    }}
    std::printf("Native memory boundary PASS (%zu cases/68 sessions,%u reference comparisons;primed native prefix then real load/store/PUSH/POP/CALL/RET/fetch fault,full EIP/ESP/raw flags and 12KiB bytes,atomic32 stores,split blocks,SS/FS,engine/census/quanta/retirement;architecture %s;stores/stack remain interpreter;not physical FPS)\n",cases.size(),compared,cpu::PagedGuestMemory::NativeArchitectureAvailable()?"ARM64 executed":"host fallback only");
}
