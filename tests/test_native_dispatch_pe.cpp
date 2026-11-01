#include "source/runtime/guest_session.h"
#include "source/runtime/guest_execution.h"
#include "source/runtime/workload_profile.h"
#include "source/runtime/clock.h"
#include "source/cpu/interpreter.h"
#include "source/win32/host_functions.h"
#include "source/win32/user32.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
using Clock=std::chrono::steady_clock;
namespace fs=std::filesystem;
uint32_t Checksum(unsigned count,bool second=false){uint32_t a=0x12345678,b=0x87654321;
    for(unsigned n=0;n<count;++n){const uint64_t add=uint64_t(a)+0x10203;a=uint32_t(add);b+=a+uint32_t(add>>32);a^=b;}
    return second?b:a^count;
}
struct Sample {
    rt::WorkloadProfile profile;cpu::PagedGuestMemory::IntegerExecutionStats instructions;cpu::NativeDispatchStats cache;
    uint64_t quanta=0,totalNs=0,warmNs=0,hotNs=0,dllNative=0,windowNative=0,apcNative=0,workerNative=0;
};
Sample Run(const fs::path& executable,unsigned kind,bool legacy,unsigned abort){
    const auto totalStart=Clock::now();Sample s;
    {rt::GuestSession session;auto& p=session.ProcessObject();auto& m=session.Memory();std::string error;
    m.EnableIntegerFastPath(kind!=0);m.EnableNativeExecution(kind==2);m.TrackIntegerExecution(true);
    if(!legacy)win32::user::EnableUi(800,600);else win32::EnableHostLayer(win32::kUiLayer,false);
    assert(p.LoadMainExe(executable.string(),error) && p.PrepareMainThread(p.MainThread().cpu,error));
    const auto probe=p.GetProcAddress(p.MainModule(),"NativeDispatchProbe");assert(probe);m.Write32(probe+31*4,abort);
    bool stopped=false,parked=false,inSleep=false,inMessage=false;unsigned phase=0;Clock::time_point warm,hot;
    const auto load=win32::IndexToHostAddress(win32::FindHostFunction("KERNEL32.dll","LoadLibraryA"));
    const auto send=win32::IndexToHostAddress(win32::FindHostFunction("USER32.dll","SendMessageW"));
    const auto sleep=win32::IndexToHostAddress(win32::FindHostFunction("KERNEL32.dll","SleepEx"));
    rt::WorkloadProfileScope scope(&s.profile);
    {
        rt::GuestExecution execution(p,[&](rt::GuestThread& t){
            struct Enter {bool& value;bool enabled;Enter(bool& v,bool e):value(v),enabled(e){if(enabled)value=true;}~Enter(){if(enabled)value=false;}} sleeping(inSleep,t.cpu.eip==sleep),message(inMessage,t.cpu.eip==send);
            const auto address=t.cpu.eip;const auto before=m.IntegerStats().native;
            win32::HostCallOutcome call;
            if(win32::TryHandleHostCall(t.cpu,m,call)){
                const auto delta=m.IntegerStats().native-before;
                if(address==load)s.dllNative+=delta;
                if(address==send)s.windowNative+=delta;
                if(address==sleep)s.apcNative+=delta;
                return !call.requestExit;
            }
            cpu::Interpreter interpreter(m,t.teb,t.teb);const auto result=interpreter.Step(t.cpu);
            if(&t!=&p.MainThread())s.workerNative+=m.IntegerStats().native-before;
            if(result!=cpu::StepResult::Continue && result!=cpu::StepResult::Halted)std::fprintf(stderr,"native PE fault EIP=%08x code=%08x\n",t.cpu.eip,interpreter.LastException().code);
            assert(result==cpu::StepResult::Continue || result==cpu::StepResult::Halted);return true;
        },rt::GuestExecution::Startup::InitializeModules);
        for(;s.quanta<100000;++s.quanta){const auto result=execution.RunOne();assert(result!=rt::GuestExecution::Result::AllocationFailed);
            const auto next=m.Read32(probe);const auto now=Clock::now();
            if(next!=phase){
                if(next==1)warm=now;
                if(next==2){s.warmNs=std::chrono::duration_cast<std::chrono::nanoseconds>(now-warm).count();hot=now;}
                if(next==3)s.hotNs=std::chrono::duration_cast<std::chrono::nanoseconds>(now-hot).count();
                phase=next;if(next>=1 && next<=3)m.Write32(probe+30*4,next);
            }
            if(abort==1 && inSleep && phase==4 && p.MainThread().status==rt::GuestThreadStatus::Waiting){parked=true;break;}
            if(abort==2 && inMessage && phase==9 && p.MainThread().status==rt::GuestThreadStatus::Waiting){parked=true;break;}
            if(result==rt::GuestExecution::Result::Stopped){stopped=true;++s.quanta;break;}
            if(result==rt::GuestExecution::Result::Idle)rt::Clock::Sleep(1);
        }
    }
    assert(!inSleep && !inMessage);
    if(abort)assert(parked && !stopped);
    else{
        if(!stopped || p.MainThread().exitCode || !s.warmNs || !s.hotNs)std::fprintf(stderr,"native PE kind%u legacy%u exit/source-line=%u phase=%u\n",kind,legacy,p.MainThread().exitCode,phase);
        assert(stopped && !p.MainThread().exitCode && phase==4 && s.warmNs>0 && s.hotNs>0);
        assert(m.Read32(probe+4)==Checksum(4096) && m.Read32(probe+8)==Checksum(4096,true));
        assert(m.Read32(probe+12)==Checksum(12288) && m.Read32(probe+16)==Checksum(12288,true));
        assert(m.Read32(probe+24)==(Checksum(512)^Checksum(512,true)));
        assert(m.Read32(probe+28)==(Checksum(256)^Checksum(256,true)) && m.Read32(probe+36)==m.Read32(probe+28));
        assert(m.Read32(probe+48)==(Checksum(512)^Checksum(512,true)));
        assert(m.Read32(probe+32)==m.Read32(probe+60) && m.Read32(probe+40)==m.Read32(probe+60) && m.Read32(probe+52)!=m.Read32(probe+60));
        std::ifstream file(executable.parent_path()/"native-dispatch-public.txt",std::ios::binary);const std::string text{std::istreambuf_iterator<char>(file),{}};
        assert(text=="native dispatch public file\r\n");
    }
    s.instructions=m.IntegerStats();s.cache=m.NativeStats();
    assert(!s.profile.instructionsTruncated && !s.profile.hostsTruncated && !s.instructions.ir);
    assert(s.profile.instructionCount==s.instructions.fast+s.instructions.reference+s.instructions.native);
    if(kind==2 && m.NativeArchitectureAvailable()){
        assert(s.instructions.native>0 && s.cache.compiles>0 && s.dllNative>0 && s.windowNative>0);
        if(!abort || abort==1)assert(s.apcNative>0 && s.workerNative>0);
    }else assert(!s.instructions.native && !s.cache.compiles);
    } // Include complete session/code-cache teardown in the total interval.
    assert(!cpu::GlobalCodeStats().images);
    s.totalNs=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-totalStart).count();return s;
}
void Parity(const Sample& a,const Sample& b){assert(a.profile.instructionCount==b.profile.instructionCount && a.profile.opcodes==b.profile.opcodes && a.profile.hostCount==b.profile.hostCount && a.profile.hosts==b.profile.hosts && a.profile.otherHosts==b.profile.otherHosts && a.quanta==b.quanta);}
int main(int argc,char** argv){
    assert(argc==2 || (argc==3 && !std::strcmp(argv[2],"--measure")));const bool measurement=argc==3;
    char pattern[]="/tmp/sr-native-pe-XXXXXX";assert(mkdtemp(pattern));const fs::path root=pattern;
    struct Cleanup {fs::path root;~Cleanup(){fs::remove_all(root);}}cleanup{root};
    fs::copy_file(fs::absolute(argv[1]),root/"native_dispatch.exe");fs::copy_file(fs::absolute(argv[1]).parent_path()/"native_dispatch.dll",root/"native_dispatch.dll");
    rt::Clock::SetFastForward(true);
    if(measurement){
        // One excluded warmup pair +seven rotating pairs. Every arm verifies
        // independent output and full CPU/HLE/quantum parity before publication.
        for(unsigned pair=0;pair<8;++pair){Sample fast,native;
            if(pair&1){native=Run(root/"native_dispatch.exe",2,false,0);fast=Run(root/"native_dispatch.exe",1,false,0);}
            else{fast=Run(root/"native_dispatch.exe",1,false,0);native=Run(root/"native_dispatch.exe",2,false,0);}
            Parity(fast,native);
            std::printf("{\"pair\":%u,\"warmup\":%s,\"native_architecture\":%s,\"fast_total_ns\":%llu,\"native_total_ns\":%llu,\"fast_first_loop_ns\":%llu,\"native_first_loop_ns\":%llu,\"fast_hot_loop_ns\":%llu,\"native_hot_loop_ns\":%llu,\"instructions\":%llu,\"native_steps\":%llu,\"hits\":%llu,\"compiles\":%llu,\"quanta\":%llu}\n",pair,pair?"false":"true",cpu::PagedGuestMemory::NativeArchitectureAvailable()?"true":"false",(unsigned long long)fast.totalNs,(unsigned long long)native.totalNs,(unsigned long long)fast.warmNs,(unsigned long long)native.warmNs,(unsigned long long)fast.hotNs,(unsigned long long)native.hotNs,(unsigned long long)native.profile.instructionCount,(unsigned long long)native.instructions.native,(unsigned long long)native.cache.hits,(unsigned long long)native.cache.compiles,(unsigned long long)native.quanta);
        }
    }else{
        Sample baseline[2];for(unsigned kind=0;kind<3;++kind)for(unsigned legacy=0;legacy<2;++legacy){auto sample=Run(root/"native_dispatch.exe",kind,legacy,0);
            if(!kind)baseline[legacy]=sample;else Parity(baseline[legacy],sample);
            std::printf("Native PE evidence kind=%u legacy=%u instructions=%llu quanta=%llu native=%llu hits=%llu compiles=%llu DLL=%llu WndProc=%llu APC=%llu worker=%llu\n",kind,legacy,(unsigned long long)sample.profile.instructionCount,(unsigned long long)sample.quanta,(unsigned long long)sample.instructions.native,(unsigned long long)sample.cache.hits,(unsigned long long)sample.cache.compiles,(unsigned long long)sample.dllNative,(unsigned long long)sample.windowNative,(unsigned long long)sample.apcNative,(unsigned long long)sample.workerNative);}
        for(unsigned kind:{0u,2u})for(unsigned abort:{1u,2u})Run(root/"native_dispatch.exe",kind,false,abort);
        std::printf("Native dispatch PE PASS (6 normal reference/G5/native new/legacy sessions+4 genuine main-HLE/nested-WndProc aborts;independent kernel/DLL/APC/worker/file bytes,exact opcode/HLE/instruction/quanta parity,TEB identity and cleanup;native architecture %s;not batching/physical FPS)\n",cpu::PagedGuestMemory::NativeArchitectureAvailable()?"ARM64 executed including loader/WndProc/APC":"unavailable (reference fallback only)");
    }
    assert(!cpu::GlobalCodeStats().images);rt::Clock::SetFastForward(false);
}
