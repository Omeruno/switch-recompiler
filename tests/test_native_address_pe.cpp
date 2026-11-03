#include "source/runtime/guest_session.h"
#include "source/runtime/guest_execution.h"
#include "source/runtime/workload_profile.h"
#include "source/runtime/clock.h"
#include "source/win32/host_functions.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
using Wall=std::chrono::steady_clock;
struct Sample {
    rt::WorkloadProfile profile;
    cpu::PagedGuestMemory::IntegerExecutionStats steps;
    cpu::NativeDispatchStats cache;
    uint64_t quanta=0,total=0,first=0,hot=0;
};
uint32_t Answer(unsigned count,bool second){uint32_t a=0x12345678,b=0x87654321;
    for(unsigned i=0;i<count;++i){a=a+b*2+0x10203;b=(b+a*4-17)^a;}return second?b:a;
}
Sample Run(const char* path,unsigned kind){
    const auto begin=Wall::now();Sample sample;
    {
        rt::GuestSession session;auto& p=session.ProcessObject();auto& m=session.Memory();std::string error;
        m.EnableIntegerFastPath(kind!=0);m.EnableNativeExecution(kind>=2);m.EnableNativeBatching(kind==3);m.TrackIntegerExecution(true);
        assert(p.LoadMainExe(path,error) && p.PrepareMainThread(p.MainThread().cpu,error));
        const auto probe=p.GetProcAddress(p.MainModule(),"NativeAddressProbe");assert(probe);
        bool stopped=false;unsigned phase=0;Wall::time_point start,hot;
        rt::WorkloadProfileScope scope(&sample.profile);
        {
            rt::GuestExecution execution(p,[&](rt::GuestThread& t){
                win32::HostCallOutcome call;if(win32::TryHandleHostCall(t.cpu,m,call))return !call.requestExit;
                cpu::Interpreter interpreter(m,t.teb,t.teb);const auto run=rt::GuestExecution::ExecuteCpu(interpreter,t.cpu);
                assert(run.executed && run.status==cpu::StepResult::Continue);return true;
            },rt::GuestExecution::Startup::InitializeModules);
            for(;sample.quanta<10000;++sample.quanta){const auto result=execution.RunOne();
                assert(result!=rt::GuestExecution::Result::AllocationFailed);
                const auto next=m.Read32(probe);const auto now=Wall::now();
                if(next!=phase){if(next==1)start=now;if(next==2){sample.first=std::chrono::duration_cast<std::chrono::nanoseconds>(now-start).count();hot=now;}
                    if(next==3)sample.hot=std::chrono::duration_cast<std::chrono::nanoseconds>(now-hot).count();
                    phase=next;if(phase>=1 && phase<=3)m.Write32(probe+15*4,phase);}
                if(result==rt::GuestExecution::Result::Stopped){stopped=true;++sample.quanta;break;}
                if(result==rt::GuestExecution::Result::Idle)rt::Clock::Sleep(1);
            }
        }
        assert(stopped && phase==3 && !p.MainThread().exitCode && sample.first && sample.hot);
        for(unsigned i=0;i<2;++i){assert(m.Read32(probe+(1+i)*4)==Answer(4096,i));assert(m.Read32(probe+(3+i)*4)==Answer(12288,i));}
        sample.steps=m.IntegerStats();sample.cache=m.NativeStats();
        assert(!sample.profile.instructionsTruncated && !sample.profile.hostsTruncated && !sample.steps.ir);
        assert(sample.profile.instructionCount==sample.steps.fast+sample.steps.reference+sample.steps.native);
        assert(sample.profile.opcodes[0x8d]>=2*(4096+12288));
        if(kind>=2 && m.NativeArchitectureAvailable())assert(sample.steps.native && sample.cache.compiles && !sample.cache.disabled);
        else assert(!sample.steps.native && !sample.cache.compiles);
    }
    assert(!cpu::GlobalCodeStats().images);
    sample.total=std::chrono::duration_cast<std::chrono::nanoseconds>(Wall::now()-begin).count();return sample;
}
void Same(const Sample& a,const Sample& b){assert(a.quanta==b.quanta && a.profile.instructionCount==b.profile.instructionCount && a.profile.opcodes==b.profile.opcodes && a.profile.hostCount==b.profile.hostCount && a.profile.hosts==b.profile.hosts && a.profile.otherHosts==b.profile.otherHosts);}
int main(int argc,char** argv){
    assert(argc==2 || (argc==3 && !std::strcmp(argv[2],"--measure")));
    rt::Clock::SetFastForward(true);
    if(argc==2){const auto reference=Run(argv[1],0);for(unsigned kind=1;kind<4;++kind)Same(reference,Run(argv[1],kind));
        std::printf("Native address PE PASS (4 reference/G5/native-step/native-batch sessions;independent LEA checksums,full opcode/HLE/instruction/quanta parity,code release;architecture %s;not physical FPS)\n",cpu::PagedGuestMemory::NativeArchitectureAvailable()?"ARM64 executed":"host fallback only");
    }else for(unsigned pair=0;pair<8;++pair){Sample fast,native;
        if(pair&1){native=Run(argv[1],3);fast=Run(argv[1],1);}else{fast=Run(argv[1],1);native=Run(argv[1],3);}Same(fast,native);
        std::printf("{\"pair\":%u,\"warmup\":%s,\"native_architecture\":%s,\"fast_total_ns\":%llu,\"native_total_ns\":%llu,\"fast_first_loop_ns\":%llu,\"native_first_loop_ns\":%llu,\"fast_hot_loop_ns\":%llu,\"native_hot_loop_ns\":%llu,\"instructions\":%llu,\"lea_instructions\":%llu,\"native_steps\":%llu,\"hits\":%llu,\"compiles\":%llu,\"batch_calls\":%llu,\"batch_instructions\":%llu,\"max_batch\":%llu,\"quanta\":%llu}\n",pair,pair?"false":"true",cpu::PagedGuestMemory::NativeArchitectureAvailable()?"true":"false",(unsigned long long)fast.total,(unsigned long long)native.total,(unsigned long long)fast.first,(unsigned long long)native.first,(unsigned long long)fast.hot,(unsigned long long)native.hot,(unsigned long long)native.profile.instructionCount,(unsigned long long)native.profile.opcodes[0x8d],(unsigned long long)native.steps.native,(unsigned long long)native.cache.hits,(unsigned long long)native.cache.compiles,(unsigned long long)native.cache.batchCalls,(unsigned long long)native.cache.batchInstructions,(unsigned long long)native.cache.maxBatch,(unsigned long long)native.quanta);
    }
    rt::Clock::SetFastForward(false);
}
