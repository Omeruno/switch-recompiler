#include "source/runtime/guest_session.h"
#include "source/runtime/guest_execution.h"
#include "source/runtime/workload_profile.h"
#include "source/runtime/clock.h"
#include "source/win32/host_functions.h"
#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
using Wall=std::chrono::steady_clock;
constexpr const char* Workloads[]={"register","memory","stack","calls","mixed"};
struct Sample {
    rt::WorkloadProfile profile;rt::EngineCensus engines;
    cpu::PagedGuestMemory::IntegerExecutionStats steps;cpu::NativeDispatchStats cache;
    uint64_t total=0,first=0,hot=0,quanta=0;
};
uint32_t Answer(unsigned kind,unsigned count,std::array<uint32_t,1024>& data){uint32_t a=0x12345678;
    for(unsigned i=0;i<count;++i){auto& cell=data[i&1023];
        if(kind==1){a+=cell;cell^=a;}
        else if(kind==4){const auto sum=a+cell;a=(sum*3+7)^sum;cell=a;}
        else a=a*3+7;
    }return a;
}
void Conserve(const Sample& s){
    assert(s.engines.instructionCount==s.profile.instructionCount);
    for(size_t op=0;op<rt::EngineCensus::OpcodeSlots;++op){uint64_t sum=0;for(const auto& row:s.engines.opcodes)sum+=row[op];assert(sum==s.profile.opcodes[op]);}
    for(size_t e=0;e<rt::EngineCensus::EngineSlots;++e){uint64_t sum=0;for(auto n:s.engines.opcodes[e])sum+=n;assert(sum==s.engines.totals[e]);}
    assert(!s.engines.totals[size_t(rt::ExecutionEngine::Unclassified)]);
    assert(s.engines.totals[size_t(rt::ExecutionEngine::FastRegister)]==s.steps.fast-s.steps.mapped);
    assert(s.engines.totals[size_t(rt::ExecutionEngine::FastMapped)]==s.steps.mapped);
    assert(s.engines.totals[size_t(rt::ExecutionEngine::Reference)]==s.steps.reference);
    assert(s.engines.totals[size_t(rt::ExecutionEngine::PortableIr)]==s.steps.ir);
    assert(s.engines.totals[size_t(rt::ExecutionEngine::Native)]==s.steps.native);
}
Sample Run(const char* path,unsigned workload,unsigned mode,bool census){
    const auto begin=Wall::now();Sample s;
    {
        rt::GuestSession session;auto& p=session.ProcessObject();auto& m=session.Memory();std::string error;
        m.EnableIntegerFastPath(mode!=0);m.EnableNativeExecution(mode>=2);m.EnableNativeBatching(mode==3);m.TrackIntegerExecution(true);
        assert(p.LoadMainExe(path,error) && p.PrepareMainThread(p.MainThread().cpu,error));
        const auto probe=p.GetProcAddress(p.MainModule(),"NativeMixedProbe"),dataAddress=p.GetProcAddress(p.MainModule(),"NativeMixedData");
        assert(probe && dataAddress);m.Write32(probe+4,workload);
        unsigned phase=0;bool stopped=false;Wall::time_point first,hot;
        {
            rt::WorkloadProfileScope scope(&s.profile);rt::EngineCensusScope bind(s.profile,census?&s.engines:nullptr);
            rt::GuestExecution execution(p,[&](rt::GuestThread& t){win32::HostCallOutcome call;
                if(win32::TryHandleHostCall(t.cpu,m,call))return !call.requestExit;
                cpu::Interpreter interpreter(m,t.teb,t.teb);const auto r=rt::GuestExecution::ExecuteCpu(interpreter,t.cpu);
                if(!r.executed || r.status!=cpu::StepResult::Continue)std::fprintf(stderr,"mixed fault kind=%u mode=%u EIP=%08x code=%08x\n",workload,mode,t.cpu.eip,interpreter.LastException().code);
                assert(r.executed && r.status==cpu::StepResult::Continue);return true;
            },rt::GuestExecution::Startup::InitializeModules);
            for(;s.quanta<10000;++s.quanta){const auto result=execution.RunOne();assert(result!=rt::GuestExecution::Result::AllocationFailed);
                const auto next=m.Read32(probe);const auto now=Wall::now();
                if(next!=phase){if(next==1)first=now;if(next==2){s.first=std::chrono::duration_cast<std::chrono::nanoseconds>(now-first).count();hot=now;}
                    if(next==3)s.hot=std::chrono::duration_cast<std::chrono::nanoseconds>(now-hot).count();
                    phase=next;if(phase>=1 && phase<=3)m.Write32(probe+15*4,phase);}
                if(result==rt::GuestExecution::Result::Stopped){stopped=true;++s.quanta;break;}
                if(result==rt::GuestExecution::Result::Idle)rt::Clock::Sleep(1);
            }
        }
        assert(stopped && phase==3 && !p.MainThread().exitCode && s.first && s.hot && !s.profile.engines);
        std::array<uint32_t,1024> expected;for(unsigned i=0;i<expected.size();++i)expected[i]=(i*0x10203u)^0x87654321u;
        assert(m.Read32(probe+8)==Answer(workload,4096,expected));
        assert(m.Read32(probe+12)==Answer(workload,12288,expected));
        for(unsigned i=0;i<expected.size();++i)assert(m.Read32(dataAddress+i*4)==expected[i]);
        if(workload>=2)assert(m.Read32(probe+6*4) && m.Read32(probe+6*4)==m.Read32(probe+7*4));
        s.steps=m.IntegerStats();s.cache=m.NativeStats();
        assert(!s.profile.instructionsTruncated && !s.profile.hostsTruncated && !s.steps.ir);
        assert(s.profile.instructionCount==s.steps.fast+s.steps.reference+s.steps.native);
        if(census)Conserve(s);else assert(!s.engines.instructionCount);
        if(mode>=2 && m.NativeArchitectureAvailable()){
            assert(s.steps.native && s.cache.compiles && !s.cache.disabled);
            if(mode==3)assert(s.cache.batchCalls && s.cache.maxBatch>1 && s.cache.maxBatch<=32);
        }else assert(!s.steps.native && !s.cache.compiles);
    }
    assert(!cpu::GlobalCodeStats().images);s.total=std::chrono::duration_cast<std::chrono::nanoseconds>(Wall::now()-begin).count();return s;
}
void Same(const Sample& a,const Sample& b){assert(a.quanta==b.quanta && a.profile.instructionCount==b.profile.instructionCount && a.profile.opcodes==b.profile.opcodes && a.profile.hostCount==b.profile.hostCount && a.profile.hosts==b.profile.hosts && a.profile.otherHosts==b.profile.otherHosts);}
void Engines(const rt::EngineCensus& census){std::printf("{");
    for(size_t e=0;e<rt::EngineCensus::EngineSlots;++e){std::printf("%s\"%s\":{\"total\":%llu,\"opcodes\":{",e?",":"",rt::EngineCensus::Name(e),(unsigned long long)census.totals[e]);
        bool comma=false;for(size_t op=0;op<rt::EngineCensus::OpcodeSlots;++op)if(census.opcodes[e][op]){std::printf("%s\"%03x\":%llu",comma?",":"",unsigned(op),(unsigned long long)census.opcodes[e][op]);comma=true;}
        std::printf("}}");
    }std::printf("}");
}
int main(int argc,char** argv){
    assert(argc==2 || (argc==3 && !std::strcmp(argv[2],"--measure")));rt::Clock::SetFastForward(true);
    if(argc==2){for(unsigned workload=0;workload<5;++workload){const auto reference=Run(argv[1],workload,0,true);
        for(unsigned mode=0;mode<4;++mode)for(bool census:{false,true}){const auto value=Run(argv[1],workload,mode,census);Same(reference,value);}
        std::printf("Mixed evidence workload=%s instructions=%llu quanta=%llu PASS\n",Workloads[workload],(unsigned long long)reference.profile.instructionCount,(unsigned long long)reference.quanta);
    }std::printf("Native mixed PE PASS (5 workloads,40 reference/G5/native-step/native-batch census-on/off sessions plus5 baselines;independent recurrence/1024 actual cells/balanced stack,full opcode/HLE/quanta and engine conservation,code release;architecture %s;not physical FPS)\n",cpu::PagedGuestMemory::NativeArchitectureAvailable()?"ARM64 executed":"host fallback only");
    }else for(unsigned workload=0;workload<5;++workload)for(unsigned pair=0;pair<8;++pair)for(unsigned order=0;order<2;++order){
        const bool census=bool((order+pair)%2);Sample fast,native;
        if((pair+workload+unsigned(census))&1){native=Run(argv[1],workload,3,census);fast=Run(argv[1],workload,1,census);}
        else{fast=Run(argv[1],workload,1,census);native=Run(argv[1],workload,3,census);}Same(fast,native);
        std::printf("{\"workload\":\"%s\",\"pair\":%u,\"warmup\":%s,\"engine_census\":%s,\"native_architecture\":%s,\"instructions\":%llu,\"quanta\":%llu,\"native_steps\":%llu,\"max_batch\":%llu,\"batch_calls\":%llu,\"fast_total_ns\":%llu,\"native_total_ns\":%llu,\"fast_first_loop_ns\":%llu,\"native_first_loop_ns\":%llu,\"fast_hot_loop_ns\":%llu,\"native_hot_loop_ns\":%llu,\"fast_engines\":",Workloads[workload],pair,pair?"false":"true",census?"true":"false",cpu::PagedGuestMemory::NativeArchitectureAvailable()?"true":"false",(unsigned long long)native.profile.instructionCount,(unsigned long long)native.quanta,(unsigned long long)native.steps.native,(unsigned long long)native.cache.maxBatch,(unsigned long long)native.cache.batchCalls,(unsigned long long)fast.total,(unsigned long long)native.total,(unsigned long long)fast.first,(unsigned long long)native.first,(unsigned long long)fast.hot,(unsigned long long)native.hot);
        Engines(fast.engines);std::printf(",\"native_engines\":");Engines(native.engines);std::printf("}\n");
    }
    rt::Clock::SetFastForward(false);
}
