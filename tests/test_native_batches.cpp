#include "source/runtime/guest_execution.h"
#include "source/runtime/guest_session.h"
#include "source/runtime/workload_profile.h"
#include "source/cpu/native_cache.h"
#include <cassert>
#include <cstdio>
#include <vector>
struct Point {uint32_t main=0,peer=0,eip=0,eax=0,edx=0,flags=0;};
std::vector<Point> Trace(bool native,uint32_t cap,uint32_t stop){
    rt::GuestThread peer;rt::GuestSession session;auto& p=session.ProcessObject();auto& m=session.Memory();
    assert(m.Map(0x10000,0x20000));auto& main=p.MainThread();
    main.id=1;main.teb=0x10000;main.status=rt::GuestThreadStatus::Runnable;main.cpu.ESP()=0x19000;main.cpu.eip=0x20000;
    peer.id=2;peer.teb=0x11000;peer.status=rt::GuestThreadStatus::Runnable;peer.cpu.ESP()=0x1f000;peer.cpu.eip=0x20000;peer.cpu.regs[0]=1234;
    assert(p.Scheduler().Add(main) && p.Scheduler().Add(peer));
    for(unsigned i=0;i<32;++i)m.Write8(0x20000+i,i&1?0x4a:0x40);
    m.Write8(0x20020,0xeb);m.Write8(0x20021,0xde);
    m.EnableNativeExecution(native);m.EnableNativeBatching(native);m.TrackIntegerExecution(true);
    // Exercise a maximal32-node prefix instead of waiting for32 cold addresses
    // to fit in a16-slot admission table. Reset architectural state afterwards.
    if(native){cpu::Interpreter warm(m);for(unsigned n=0;n<cpu::NativeCache::HotVisits;++n){cpu::CpuState c;c.eip=0x20000;assert(warm.Step(c)==cpu::StepResult::Continue);}}
    uint32_t a=0,b=0;std::vector<Point> trace;
    {
        rt::GuestExecution execution(p,[&](rt::GuestThread& t){auto& count=t.id==1?a:b;
            if(a>=stop)return false;
            cpu::Interpreter interpreter(m,t.teb,t.teb);
            const auto run=rt::GuestExecution::ExecuteCpu(interpreter,t.cpu,std::min(cap,stop-a));
            assert(run.status==cpu::StepResult::Continue && run.executed>0 && run.executed<=cap);
            count+=run.executed;return true;
        });
        for(unsigned q=0;q<10000;++q){const auto result=execution.RunOne();
            assert(result!=rt::GuestExecution::Result::AllocationFailed);
            trace.push_back({a,b,main.cpu.eip,main.cpu.regs[0],peer.cpu.regs[0],main.cpu.GetEflags()});
            if(result==rt::GuestExecution::Result::Stopped)break;
        }
    }
    assert(a==stop && !trace.empty());
    if(native && m.NativeArchitectureAvailable() && cap>1 && stop>1)assert(m.NativeStats().batchCalls>0 && m.NativeStats().maxBatch<=32);
    return trace;
}
int main(){
    unsigned cases=0;
    for(uint32_t cap:{1u,2u,3u,7u,31u,32u,33u,255u,256u,10000u})for(uint32_t stop:{1u,2u,31u,32u,255u,256u,257u,4096u}){
        const auto ref=Trace(false,cap,stop),batch=Trace(true,cap,stop);assert(ref.size()==batch.size());
        for(size_t n=0;n<ref.size();++n){const auto& a=ref[n];const auto& b=batch[n];assert(a.main==b.main && a.peer==b.peer && a.eip==b.eip && a.eax==b.eax && a.edx==b.edx && a.flags==b.flags);}
        assert(!cpu::GlobalCodeStats().images);++cases;
    }
    // No active/approved executor,copy CPUs and debug output cannot borrow a
    // bulk quantum. Limit0 executes nothing and preserves a pending fault.
    {
        rt::GuestSession s;auto& m=s.Memory();assert(m.Map(0x10000,4096));for(unsigned i=0;i<32;++i)m.Write8(0x10000+i,0x40);
        m.EnableNativeExecution(true);m.EnableNativeBatching(true);cpu::Interpreter interpreter(m);
        for(unsigned i=0;i<16;++i){cpu::CpuState c;c.eip=0x10000;auto r=rt::GuestExecution::ExecuteCpu(interpreter,c,32);assert(r.executed==1 && c.regs[0]==1);}
        cpu::CpuState c;c.eip=0x10000;m.Fault()={true,true,0x1234};auto r=interpreter.Run(c,0);assert(!r.executed && c.eip==0x10000 && m.Fault().pending);
        cpu::Instruction debug;r=interpreter.Run(c,32,&debug);assert(r.executed==1 && debug.length==1);
    }
    std::printf("Native batches PASS (%u reference/native per-quantum traces;1/2/31/32/255/256/257/max budgets,peer fairness,exact stop/EIP/flags,zero/debug/unapproved guards and storage release;architecture %s)\n",cases,cpu::PagedGuestMemory::NativeArchitectureAvailable()?"ARM64 executed":"host fallback only");
}
