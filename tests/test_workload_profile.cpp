// Public deterministic IA-32 workload, no Microsoft binaries or wall-clock API.
#include "source/runtime/workload_profile.h"
#include "source/runtime/workload_report.h"
#include "source/cpu/interpreter.h"
#include "source/cpu/native_cache.h"
#include "source/win32/host_functions.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <cstring>

#define CHECK(x) do {if (!(x)) {std::fprintf(stderr,"profile CHECK line %d: %s\n",__LINE__,#x);std::exit(1);}} while(0)

static void Run(rt::WorkloadProfile* profile) {
    rt::WorkloadProfileScope scope(profile);
    cpu::FlatGuestMemory mem(0x1000,0x8000);
    // mov ecx,1000; xor eax,eax; loop: add eax,3; dec ecx; jnz loop;
    // operand-size NOP; 0F NOP; HLT. Prefixes are not extra instructions.
    const uint8_t code[]={0xb9,0xe8,3,0,0,0x31,0xc0,0x83,0xc0,3,0x49,0x75,0xfa,
                          0x66,0x90,0x0f,0x1f,0,0xf4};
    mem.LoadBytes(0x1000,code,sizeof(code));
    cpu::CpuState c; c.eip=0x1000;c.ESP()=0x8000;
    cpu::Interpreter interp(mem);
    unsigned steps=0;
    for (;;) {
        auto result=interp.Step(c); ++steps;
        if (result==cpu::StepResult::Halted) break;
        CHECK(result==cpu::StepResult::Continue); CHECK(steps<3010);
    }
    CHECK(steps==3005);CHECK(c.regs[0]==3000);CHECK(c.regs[1]==0);
    CHECK(c.eip==0x1000+sizeof(code));CHECK(c.ZF());
    // Calls enter the real HLE dispatcher; a failed address lookup is not a call.
    const int set=win32::FindHostFunction("KERNEL32.dll","lstrlenA");
    const int get=win32::FindHostFunction("KERNEL32.dll","lstrlenW");
    CHECK(set>=0 && get>=0);
    mem.Write8(0x4000,65);mem.Write8(0x4001,66);mem.Write8(0x4002,0);
    mem.Write16(0x4100,65);mem.Write16(0x4102,66);mem.Write16(0x4104,0);
    for (unsigned j=0;j<16;++j) {
        c.eip=win32::IndexToHostAddress(set);c.ESP()=0x8000;
        mem.Write32(0x8000,0x1234);mem.Write32(0x8004,0x4000);
        win32::HostCallOutcome out;
        CHECK(win32::TryHandleHostCall(c,mem,out));CHECK(c.eip==0x1234);CHECK(c.ESP()==0x8008);CHECK(c.regs[0]==2);
        c.eip=win32::IndexToHostAddress(get);c.ESP()=0x8000;
        mem.Write32(0x8004,0x4100);
        CHECK(win32::TryHandleHostCall(c,mem,out));CHECK(c.regs[0]==2);
        CHECK(c.eip==0x1234);CHECK(c.ESP()==0x8008);
        CHECK(!win32::TryHandleHostCall(c,mem,out));
    }
    // REP is one Step attempt, not four element operations.
    mem.Write8(0x1000,0xf3);mem.Write8(0x1001,0xab);c.eip=0x1000;
    c.regs[0]=0x55cc77aa;c.regs[1]=4;c.regs[7]=0x2000;
    CHECK(interp.Step(c)==cpu::StepResult::Continue);
    CHECK(c.regs[1]==0 && c.regs[7]==0x2010);
    for (unsigned j=0;j<4;++j) CHECK(mem.Read32(0x2000+4*j)==0x55cc77aa);
    // UD2 counts an attempted instruction and retains the normal fault contract.
    mem.Write8(0x1000,0x0f);mem.Write8(0x1001,0x0b);c.eip=0x1000;
    CHECK(interp.Step(c)==cpu::StepResult::UnknownOpcode);
    CHECK(c.eip==0x1000);CHECK(interp.LastException().code==cpu::EXC_ILLEGAL_INSTRUCTION);
    // Missing first opcode and missing second byte of the 0F map.
    c.eip=0xa000;CHECK(interp.Step(c)==cpu::StepResult::Exception);
    mem.Write8(0x8fff,0x0f);c.eip=0x8fff;
    CHECK(interp.Step(c)==cpu::StepResult::Exception);
}

int main() {
    CHECK(rt::g_workloadProfile==nullptr);
    Run(nullptr);
    rt::WorkloadProfile a,b;
    Run(&a);Run(&b);
    CHECK(rt::g_workloadProfile==nullptr);
    CHECK(a.opcodes==b.opcodes && a.hosts==b.hosts);
    CHECK(a.instructionCount==3009 && a.hostCount==32);
    CHECK(a.opcodes[0x83]==1000 && a.opcodes[0x49]==1000 && a.opcodes[0x75]==1000);
    CHECK(a.opcodes[0x90]==1 && a.opcodes[0x11f]==1 && a.opcodes[0x10b]==1);
    CHECK(a.opcodes[0xab]==1);
    CHECK(a.opcodes[512]==2 && !a.instructionsTruncated && !a.hostsTruncated);
    const int set=win32::FindHostFunction("KERNEL32.dll","lstrlenA");
    const int get=win32::FindHostFunction("KERNEL32.dll","lstrlenW");
    CHECK(a.hosts[size_t(set)]==16 && a.hosts[size_t(get)]==16 && a.otherHosts==0);
    rt::WorkloadProfile limited;limited.instructionLimit=7;limited.hostLimit=3;
    Run(&limited); CHECK(limited.instructionCount==7 && limited.hostCount==3);
    CHECK(limited.instructionsTruncated && limited.hostsTruncated);
    rt::WorkloadProfile zero;zero.instructionLimit=zero.hostLimit=0;
    Run(&zero);CHECK(zero.instructionCount==0 && zero.hostCount==0);
    CHECK(zero.instructionsTruncated && zero.hostsTruncated);
    {
        rt::WorkloadProfileScope outer(&a);
        {rt::WorkloadProfileScope disabled(nullptr); CHECK(!rt::g_workloadProfile);}
        CHECK(rt::g_workloadProfile==&a);
        {rt::WorkloadProfileScope inner(&b);CHECK(rt::g_workloadProfile==&b);}
        CHECK(rt::g_workloadProfile==&a);
    }
    CHECK(!rt::g_workloadProfile);
    rt::WorkloadProfile bounds;bounds.Host(rt::WorkloadProfile::HostSlots);
    bounds.Host(std::numeric_limits<size_t>::max());
    CHECK(bounds.hostCount==2 && bounds.otherHosts==2);
    bounds.instructionLimit=std::numeric_limits<uint64_t>::max();
    bounds.instructionCount=bounds.instructionLimit;
    bounds.Instruction(0);CHECK(bounds.instructionCount==bounds.instructionLimit && bounds.instructionsTruncated);
    rt::WorkloadProfile classified;rt::EngineCensus engines;
    {
        rt::EngineCensusScope bind(classified,engines);Run(&classified);
        CHECK(classified.engines==&engines);
        rt::EngineCensus nested;
        {rt::EngineCensusScope inner(classified,nested);CHECK(classified.engines==&nested);}
        CHECK(classified.engines==&engines);
        FILE* file=std::tmpfile();CHECK(file);rt::WriteWorkloadReport(file,classified);CHECK(!std::ferror(file));
        std::rewind(file);char text[32768]={};CHECK(std::fread(text,1,sizeof(text)-1,file)>0);std::fclose(file);
        CHECK(std::strstr(text,"engine-census-v1 instructions=3009") && std::strstr(text,"engine fast-register total=2002"));
    }
    CHECK(!classified.engines && classified.opcodes==a.opcodes && engines.instructionCount==3009);
    CHECK(engines.totals[size_t(rt::ExecutionEngine::FastRegister)]==2002);
    CHECK(engines.totals[size_t(rt::ExecutionEngine::Reference)]==1007);
    CHECK(!engines.totals[size_t(rt::ExecutionEngine::Unclassified)]);
    for(size_t op=0;op<rt::WorkloadProfile::OpcodeSlots;++op){uint64_t sum=0;for(const auto& row:engines.opcodes)sum+=row[op];CHECK(sum==classified.opcodes[op]);}
    rt::WorkloadProfile capped;capped.instructionLimit=7;rt::EngineCensus cap;
    {rt::EngineCensusScope bind(capped,cap);Run(&capped);}
    CHECK(cap.instructionCount==7 && capped.instructionsTruncated);
    rt::WorkloadProfile empty;empty.instructionLimit=0;rt::EngineCensus none;
    {rt::EngineCensusScope bind(empty,none);Run(&empty);}
    CHECK(!none.instructionCount && empty.instructionsTruncated);
    rt::WorkloadProfile invalid;rt::EngineCensus unknown;
    {rt::EngineCensusScope bind(invalid,unknown);invalid.Instruction(std::numeric_limits<size_t>::max(),rt::ExecutionEngine(255));}
    CHECK(unknown.instructionCount==1 && unknown.opcodes[size_t(rt::ExecutionEngine::Unclassified)][512]==1);
    {
        cpu::PagedGuestMemory memory(0x10000,0x3000);memory.TrackIntegerExecution(true);
        cpu::CodeStore store(memory.Identity());cpu::NativeCache cache(memory,store);
        const uint8_t code[]={0x40,0x49,0x75,0xfc};memory.LoadBytes(0x10000,code,sizeof(code));
        rt::WorkloadProfile profile;rt::EngineCensus counts;
        {rt::EngineCensusScope bind(profile,counts);rt::WorkloadProfileScope scope(&profile);
            auto block=cpu::ir::Build(memory,0x10000);cpu::CpuState portable;portable.eip=0x10000;portable.regs[1]=1;
            CHECK(cpu::ir::Execute(block,memory,portable,3).executed==3);
            memory.EnableNativeExecution(true);memory.EnableNativeBatching(true);
            cpu::Interpreter interpreter(memory);cpu::CpuState state;state.eip=0x10000;state.regs[1]=128;
            for(unsigned n=0;n<384 && state.eip!=0x10004;++n){auto result=interpreter.Run(state,32);CHECK(result.executed && result.status==cpu::StepResult::Continue);}
            CHECK(state.eip==0x10004 && state.regs[0]==128 && !state.regs[1]);
        }
        CHECK(counts.totals[size_t(rt::ExecutionEngine::PortableIr)]==3);
        CHECK(counts.totals[size_t(rt::ExecutionEngine::Native)]==memory.IntegerStats().native);
        CHECK(bool(counts.totals[size_t(rt::ExecutionEngine::Native)])==memory.NativeArchitectureAvailable());
        CHECK(!counts.totals[size_t(rt::ExecutionEngine::Unclassified)] && counts.instructionCount==profile.instructionCount);
    }
    rt::WriteWorkloadReport(stdout,a);
    std::puts("workload profile: PASSED (disabled/repeat/fault/budget/nesting/bounds;optional per-engine opcode conservation,limits,scope lifetime,report,actual IR/native or honest unavailable)");
}
