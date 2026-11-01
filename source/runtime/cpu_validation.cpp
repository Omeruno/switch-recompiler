#include "cpu_validation.h"
#include "guest_session.h"
#include "guest_execution.h"
#include "clock.h"
#include "workload_profile.h"
#include "../win32/host_functions.h"
#include "../win32/user32.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <string>
namespace rt {
namespace {
using Time=std::chrono::steady_clock;
struct Sample {
    WorkloadProfile profile;
    cpu::PagedGuestMemory::IntegerExecutionStats instructions;
    cpu::NativeDispatchStats cache;
    uint64_t quanta=0,totalNs=0,loopNs=0,dll=0,window=0,apc=0,worker=0;
    const char* error=nullptr;uint32_t exitCode=0,stage=0,storageError=0;
    bool available=true;
};
uint32_t Answer(unsigned count,bool second=false){uint32_t a=0x12345678,b=0x87654321;
    for(unsigned n=0;n<count;++n){const uint64_t add=uint64_t(a)+0x10203;a=uint32_t(add);b+=a+uint32_t(add>>32);a^=b;}
    return second?b:a^count;
}
bool FileHash(const std::string& path,uint64_t& hash,uint64_t& bytes){
    FILE* f=std::fopen(path.c_str(),"rb");if(!f)return false;
    hash=14695981039346656037ull;bytes=0;unsigned char buffer[4096];size_t n;
    while((n=std::fread(buffer,1,sizeof(buffer),f))){bytes+=n;for(size_t i=0;i<n;++i){hash^=buffer[i];hash*=1099511628211ull;}}
    const bool ok=!std::ferror(f);std::fclose(f);return ok;
}
bool Same(const Sample& a,const Sample& b){return a.profile.instructionCount==b.profile.instructionCount && a.profile.hostCount==b.profile.hostCount && a.profile.opcodes==b.profile.opcodes && a.profile.hosts==b.profile.hosts && a.profile.otherHosts==b.profile.otherHosts && a.quanta==b.quanta;}
const char* Mode(unsigned kind){const char* names[]={"reference","G5","native-step","native-batch"};return names[kind];}
void Run(const char* path,unsigned kind,unsigned sampleNumber,CpuValidationPump pump,void* context,Sample& s){
    const auto start=Time::now();
    {
        GuestSession session;auto& p=session.ProcessObject();auto& m=session.Memory();std::string error;
        m.EnableIntegerFastPath(kind!=0);m.EnableNativeExecution(kind>=2);m.EnableNativeBatching(kind==3);m.TrackIntegerExecution(true);
        win32::user::EnableUi(800,600);
        if(!p.LoadMainExe(path,error) || !p.PrepareMainThread(p.MainThread().cpu,error)){s.error="PE/DLL load or main thread preparation failed";return;}
        const auto probe=p.GetProcAddress(p.MainModule(),"NativeDispatchProbe");if(!probe){s.error="public probe export missing";return;}
        m.Write32(probe+31*4,0);
        const auto load=win32::IndexToHostAddress(win32::FindHostFunction("KERNEL32.dll","LoadLibraryA"));
        const auto send=win32::IndexToHostAddress(win32::FindHostFunction("USER32.dll","SendMessageW"));
        const auto sleep=win32::IndexToHostAddress(win32::FindHostFunction("KERNEL32.dll","SleepEx"));
        bool stopped=false,failed=false;unsigned stage=0;Time::time_point loop;
        WorkloadProfileScope scope(&s.profile);
        {
            GuestExecution execution(p,[&](GuestThread& t){
                const auto address=t.cpu.eip;const auto before=m.IntegerStats().native;
                win32::HostCallOutcome call;
                if(win32::TryHandleHostCall(t.cpu,m,call)){
                    const auto delta=m.IntegerStats().native-before;
                    if(address==load)s.dll+=delta;
                    if(address==send)s.window+=delta;
                    if(address==sleep)s.apc+=delta;
                    return !call.requestExit;
                }
                cpu::Interpreter interpreter(m,t.teb,t.teb);
                const auto result=GuestExecution::ExecuteCpu(interpreter,t.cpu);
                if(&t!=&p.MainThread())s.worker+=m.IntegerStats().native-before;
                if(!result.executed || (result.status!=cpu::StepResult::Continue && result.status!=cpu::StepResult::Halted)){failed=true;return false;}
                return true;
            },GuestExecution::Startup::InitializeModules);
            for(;s.quanta<10000;++s.quanta){
                if(pump && !pump(sampleNumber,s.quanta,context)){s.error="cancelled";break;}
                const auto result=execution.RunOne();
                if(result==GuestExecution::Result::AllocationFailed){s.error="continuation allocation failed";break;}
                const auto next=m.Read32(probe);const auto now=Time::now();
                if(next!=stage){if(next==2)loop=now;if(next==3)s.loopNs=std::chrono::duration_cast<std::chrono::nanoseconds>(now-loop).count();stage=next;
                    if(next>=1 && next<=3)m.Write32(probe+30*4,next);}
                if(result==GuestExecution::Result::Stopped){stopped=true;++s.quanta;break;}
                if(result==GuestExecution::Result::Idle)Clock::Sleep(1);
            }
        }
        s.stage=stage;s.exitCode=p.MainThread().exitCode;s.instructions=m.IntegerStats();s.cache=m.NativeStats();s.storageError=uint32_t(session.Code().LastError());
        if(!s.error && (!stopped || failed || s.exitCode || stage!=4 || !s.loopNs))s.error="guest execution did not complete correctly";
        if(!s.error){
            const bool values=m.Read32(probe+4)==Answer(4096) && m.Read32(probe+8)==Answer(4096,true) &&
                m.Read32(probe+12)==Answer(12288) && m.Read32(probe+16)==Answer(12288,true) &&
                m.Read32(probe+24)==(Answer(512)^Answer(512,true)) &&
                m.Read32(probe+28)==(Answer(256)^Answer(256,true)) && m.Read32(probe+36)==m.Read32(probe+28) &&
                m.Read32(probe+48)==(Answer(512)^Answer(512,true)) &&
                m.Read32(probe+32)==m.Read32(probe+60) && m.Read32(probe+40)==m.Read32(probe+60) && m.Read32(probe+52)!=m.Read32(probe+60);
            if(!values)s.error="independent kernel/DLL/callback/thread checksum mismatch";
        }
        if(!s.error && (s.profile.instructionsTruncated || s.profile.hostsTruncated || s.profile.instructionCount!=s.instructions.fast+s.instructions.reference+s.instructions.native || s.instructions.ir))s.error="instruction/census accounting mismatch";
        if(!s.error && kind>=2 && (!m.NativeArchitectureAvailable() || !s.instructions.native || s.cache.disabled || !s.dll || !s.window || !s.apc || !s.worker)){s.available=false;s.error="native execution unavailable";}
        if(!s.error && kind==3 && (!s.cache.batchCalls || s.cache.maxBatch<=1 || s.cache.maxBatch>32))s.error="no real bounded native batches";
        if(!s.error){
            const std::string name=std::string(path).substr(0,std::string(path).find_last_of("/\\")+1)+"native-dispatch-public.txt";
            FILE* f=std::fopen(name.c_str(),"rb");char bytes[64]={};const size_t n=f?std::fread(bytes,1,sizeof(bytes),f):0;if(f)std::fclose(f);
            const char expected[]="native dispatch public file\r\n";
            if(n!=sizeof(expected)-1 || std::memcmp(bytes,expected,n))s.error="actual file bytes mismatch";
        }
    }
    if(cpu::GlobalCodeStats().images && !s.error)s.error="native code retained after session teardown";
    s.totalNs=std::chrono::duration_cast<std::chrono::nanoseconds>(Time::now()-start).count();
}
}
bool RunCpuValidation(const char* executable,const char* report,const char* build,CpuValidationPump pump,void* context,const char* environment){
    FILE* out=std::fopen(report,"w");if(!out)return false;
    struct Output {FILE* f;~Output(){std::fclose(f);}} output{out};
#ifdef __SWITCH__
    const char* platform="SWITCH_HARDWARE";
#else
    const char* platform="HOST_OR_QEMU_NOT_SWITCH";
#endif
    std::fprintf(out,"switch-recompiler CPU batch validation v1\nBuild: %s\nPlatform: %s\nFixture: %s\nTiming: real steady wall;acknowledged loop;complete session teardown\n",build,platform,executable);std::fprintf(out,"Environment: %s\n",environment?environment:"unspecified");std::fflush(out);
    const std::string path=executable;const std::string dll=path.substr(0,path.find_last_of("/\\")+1)+"native_dispatch.dll";
    uint64_t exeHash=0,exeSize=0,dllHash=0,dllSize=0;
    if(!FileHash(path,exeHash,exeSize) || !FileHash(dll,dllHash,dllSize)){std::fputs("Result: FAIL missing public fixture\n",out);return false;}
    std::fprintf(out,"EXE bytes/FNV1a64: %llu/%016llx\nDLL bytes/FNV1a64: %llu/%016llx\n",(unsigned long long)exeSize,(unsigned long long)exeHash,(unsigned long long)dllSize,(unsigned long long)dllHash);std::fflush(out);
    const bool previous=Clock::IsFastForward();Clock::SetFastForward(true);
    struct Restore {bool previous;~Restore(){Clock::SetFastForward(previous);}} restore{previous};
    auto baseline=std::unique_ptr<Sample>(new(std::nothrow) Sample);
    auto value=std::unique_ptr<Sample>(new(std::nothrow) Sample);
    if(!baseline || !value){std::fputs("Result: FAIL validation metadata allocation\n",out);return false;}
    unsigned sample=0;
    // One reference +one warmup triplet +five rotating G5/step/batch triplets.
    // All arms use exactly the same fixture and require full census/quanta parity.
    for(unsigned round=0;round<7;++round){
        const unsigned count=round?3:1;
        for(unsigned arm=0;arm<count;++arm){const unsigned kind=round?1+(arm+(round-1)%3)%3:0;
            *value=Sample{};
            std::fprintf(out,"sample_start: %u round=%u mode=%s\n",sample,round,Mode(kind));std::fflush(out);
            Run(executable,kind,sample++,pump,context,*value);
            if(!round)*baseline=*value;
            if(!value->error && !Same(*baseline,*value))value->error="full opcode/HLE/instruction/quantum mismatch";
            std::fprintf(out,"sample: %u mode=%s warmup=%u status=%s instructions=%llu quanta=%llu native=%llu batch_calls=%llu batch_instructions=%llu max_batch=%llu DLL=%llu WndProc=%llu APC=%llu worker=%llu total_ns=%llu loop_ns=%llu exit=%u stage=%u storage_error=%u\n",sample-1,Mode(kind),unsigned(round<=1),value->error?value->error:"PASS",(unsigned long long)value->profile.instructionCount,(unsigned long long)value->quanta,(unsigned long long)value->instructions.native,(unsigned long long)value->cache.batchCalls,(unsigned long long)value->cache.batchInstructions,(unsigned long long)value->cache.maxBatch,(unsigned long long)value->dll,(unsigned long long)value->window,(unsigned long long)value->apc,(unsigned long long)value->worker,(unsigned long long)value->totalNs,(unsigned long long)value->loopNs,value->exitCode,value->stage,value->storageError);std::fflush(out);
            if(value->error){std::fprintf(out,"Result: %s\n",value->available?"FAIL":"UNAVAILABLE");return false;}
        }
    }
    std::fputs("Result: PASS_CPU_BATCH_VALIDATION\nScope: CPU/public PE only;not Paint/GPU/game FPS acceptance\n",out);return true;
}
} // namespace rt
