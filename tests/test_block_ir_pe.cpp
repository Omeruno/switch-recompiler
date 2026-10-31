#include "source/cpu/block_ir.h"
#include "source/cpu/interpreter.h"
#include "source/runtime/guest_execution.h"
#include "source/runtime/guest_session.h"
#include "source/runtime/workload_profile.h"
#include "source/win32/host_functions.h"
#include "source/win32/user32.h"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
namespace fs=std::filesystem;
struct Measurement {uint64_t instructions=0,hosts=0,ir=0,quanta=0;std::array<uint64_t,513> opcodes{};std::array<uint64_t,4096> hostEntries{};uint64_t otherHosts=0;};
int main(int argc,char** argv){
    assert(argc==2);const fs::path original=fs::absolute(argv[1]);
    char pattern[]="/tmp/sr-block-ir-XXXXXX";assert(mkdtemp(pattern));const fs::path root=pattern;
    struct Cleanup {fs::path p;~Cleanup(){fs::remove_all(p);}}cleanup{root};
    fs::copy_file(original,root/"probe.exe");fs::copy_file(original.parent_path()/"old_pe_api.dll",root/"old_pe_api.dll");
    std::array<Measurement,2> baseline{};unsigned normal=0,aborted=0;
    for(unsigned mode=0;mode<6;++mode){
        const bool portable=mode>=2,abort=mode>=4,legacy=mode&1;
        rt::GuestSession session;auto& p=session.ProcessObject();auto& m=session.Memory();std::string error;
        m.EnableIntegerFastPath(false);m.TrackIntegerExecution(true);
        if(!legacy)win32::user::EnableUi(1280,720);else win32::EnableHostLayer(win32::kUiLayer,false);
        assert(p.LoadMainExe((root/"probe.exe").string(),error) && p.PrepareMainThread(p.MainThread().cpu,error));
        const auto probe=p.GetProcAddress(p.MainModule(),"OldPeProbe");assert(probe);m.Write32(probe,abort?2:0);
        rt::WorkloadProfile profile;rt::WorkloadProfileScope scope(&profile);
        bool stopped=false,parked=false,inSleep=false;uint64_t quanta=0;
        const uint32_t sleep=win32::IndexToHostAddress(win32::FindHostFunction("KERNEL32.dll","SleepEx"));
        {
            rt::GuestExecution execution(p,[&](rt::GuestThread& t){
                // Live flag proves an abort occurs inside a suspended HLE frame,
                // not merely with EIP pointing at a not-yet-entered host address.
                struct SleepScope {bool& flag;bool active;~SleepScope(){if(active)flag=false;}} entered{inSleep,t.cpu.eip==sleep};
                if(entered.active)inSleep=true;
                win32::HostCallOutcome call;if(win32::TryHandleHostCall(t.cpu,m,call))return !call.requestExit;
                if(portable){
                    const auto block=cpu::ir::Build(m,t.cpu.eip);
                    const auto result=cpu::ir::Execute(block,m,t.cpu,1);
                    if(result.executed)return true;
                }
                cpu::Interpreter interpreter(m,t.teb,t.teb);const auto result=interpreter.Step(t.cpu);
                assert(result==cpu::StepResult::Continue || result==cpu::StepResult::Halted);return true;
            },rt::GuestExecution::Startup::InitializeModules);
            for(;quanta<100000;++quanta){const auto result=execution.RunOne();assert(result!=rt::GuestExecution::Result::AllocationFailed);
                if(abort && inSleep && m.Read32(probe+4)){parked=true;break;}
                if(result==rt::GuestExecution::Result::Stopped){stopped=true;++quanta;break;}}
        }
        assert(!inSleep && m.Read32(probe+4)==1);
        if(abort){assert(parked && !stopped);++aborted;}
        else {if(p.MainThread().exitCode)fprintf(stderr,"IR PE mode%u failed source line%u\n",mode,p.MainThread().exitCode);assert(stopped && !p.MainThread().exitCode);++normal;}
        assert(!profile.instructionsTruncated && !profile.hostsTruncated);
        assert(profile.instructionCount==m.IntegerStats().reference+m.IntegerStats().ir);
        assert(!m.IntegerStats().fast && (portable?m.IntegerStats().ir>0:m.IntegerStats().ir==0));
        if(mode<2)baseline[legacy]={profile.instructionCount,profile.hostCount,0,quanta,profile.opcodes,profile.hosts,profile.otherHosts};
        else if(!abort){const auto& b=baseline[legacy];assert(profile.instructionCount==b.instructions && profile.hostCount==b.hosts && profile.opcodes==b.opcodes && quanta==b.quanta && profile.hosts==b.hostEntries && profile.otherHosts==b.otherHosts);}
        std::ifstream file(root/"old-pe-public.txt",std::ios::binary);const std::string payload{std::istreambuf_iterator<char>(file),{}};
        assert(payload=="old PE named and ordinal imports\r\n");
    }
    assert(normal==4 && aborted==2);
    std::puts("Block IR PE PASS (4 normal new/legacy x86 sessions+2 genuinely parked HLE aborts;real PE named+ordinal DLL/API/file bytes,portable IR+reference fallback,exact opcode/HLE/instruction/quantum parity,drained native frames;not production batching/ARM64 dynarec/FPS)");
}
