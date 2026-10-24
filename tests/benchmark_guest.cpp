// Opt-in host experiment: real public PE + phase clocks + G0/raster overhead.
// Not a Switch FPS test, and no subtraction purports to isolate interpreter time.
#include "source/runtime/command_line.h"
#include "source/runtime/guest_session.h"
#include "source/runtime/guest_execution.h"
#include "source/runtime/workload_profile.h"
#include "source/runtime/clock.h"
#include "source/cpu/interpreter.h"
#include "source/win32/user32.h"
#include "source/ui/performance_metrics.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"benchmark %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(int argc,char** argv) {
    CHECK(argc==4 || argc==5);
    const std::string workload=argv[2],instrument=argv[3];
    CHECK(workload=="cpu" || workload=="tiles" || workload=="strokes");
    CHECK(instrument=="baseline" || instrument=="census" || instrument=="raster" || instrument=="both" || instrument=="phase");
    const bool fast=argc==4 || std::strcmp(argv[4],"fast")==0;
    CHECK(argc==4 || fast || std::strcmp(argv[4],"reference")==0);
    const bool census=instrument=="census" || instrument=="both";
    const bool raster=instrument=="raster" || instrument=="both";
    rt::Clock::SetFastForward(true);cpu::g_consoleAvailable=false;
    rt::WorkloadProfile profile;
    rt::WorkloadProfileScope scope(census?&profile:nullptr);
    const auto begin=ui::PerformanceNowNs();
    auto session=std::make_unique<rt::GuestSession>();
    auto& p=session->ProcessObject();auto& m=session->Memory();std::string error;
    win32::user::EnableUi(800,600);ui::g_rasterMetrics.Reset(raster);
    ui::g_gdi.EnableBlitFastPath(fast);
    ui::ExecutionPhaseMetrics phases;phases.Reset(instrument=="phase");
    p.SetCommandLine(rt::QuoteCommandArgument(argv[1])+" "+workload);
    const auto loadBegin=ui::PerformanceNowNs();
    CHECK(p.LoadMainExe(argv[1],error));
    const auto loadEnd=ui::PerformanceNowNs();
    CHECK(p.PrepareMainThread(p.MainThread().cpu,error));
    const auto setupEnd=ui::PerformanceNowNs();
    const auto version=p.GetProcAddress(p.MainModule(),"WorkloadVersion");
    const auto done=p.GetProcAddress(p.MainModule(),"WorkloadDone");
    const auto checksum=p.GetProcAddress(p.MainModule(),"WorkloadChecksum");
    CHECK(version && done && checksum && m.Read32(version)==1);
    bool fault=false,finished=false;uint64_t quanta=0;
    const auto runBegin=ui::PerformanceNowNs();
    {
        rt::GuestExecution execution(p,[&](rt::GuestThread& t) {
            win32::HostCallOutcome out;
            if(win32::TryHandleHostCall(t.cpu,m,out))return !out.requestExit;
            cpu::Interpreter interpreter(m,t.teb,t.teb);
            const auto step=interpreter.Step(t.cpu);
            if(step==cpu::StepResult::Continue || step==cpu::StepResult::Halted)return true;
            fault=true;return false;
        },rt::GuestExecution::Startup::InitializeModules);
        for(;quanta<100000;++quanta) {
            rt::GuestExecution::Result result;
            {
                ui::ExecutionPhaseMetrics::Scope timing(phases,ui::ExecutionPhase::Quantum);
                result=execution.RunOne();
            }
            CHECK(result!=rt::GuestExecution::Result::AllocationFailed);
            if(result==rt::GuestExecution::Result::Stopped){finished=true;++quanta;break;}
            CHECK(result!=rt::GuestExecution::Result::Idle);
            if((quanta&255)==0)CHECK(ui::PerformanceNowNs()-runBegin<30000000000ull);
        }
    }
    const auto runEnd=ui::PerformanceNowNs();
    CHECK(finished && !fault && win32::g_guestExitRequested && win32::g_guestExitCode==0);
    CHECK(m.Read32(done)==1);
    const uint32_t result=m.Read32(checksum);
    CHECK(result==(workload=="cpu"?0x7a218cb8u:workload=="tiles"?0x204080u:0x208040u));
    uint64_t pixelHash=14695981039346656037ull;
    const auto& surface=win32::user::S().screen.s;
    CHECK(surface.width==800 && surface.height==600);
    for(int y=0;y<surface.height;++y)for(int x=0;x<surface.width;++x) {
        const uint32_t pixel=surface.Get(x,y)&0xffffff;
        if(workload!="cpu" && x<480 && y<256) {
            const uint32_t expected=workload=="tiles" ?
                (((x/16+y/16)&1)?0xc04020u:0x204080u) : ((y&1)?0xffffffu:0x208040u);
            CHECK(pixel==expected);
        }
        for(unsigned shift=0;shift<24;shift+=8){pixelHash^=(pixel>>shift)&255;pixelHash*=1099511628211ull;}
    }
    const auto rasterSnapshot=ui::g_rasterMetrics;
    ui::g_rasterMetrics.Reset(false);
    const auto teardownBegin=ui::PerformanceNowNs();session.reset();
    const auto teardownEnd=ui::PerformanceNowNs();
    CHECK(!rt::g_process && !profile.instructionsTruncated && !profile.hostsTruncated);
    std::printf("{\"schema\":1,\"workload\":\"%s\",\"instrument\":\"%s\",\"status\":\"PASS\","
        "\"create_ns\":%llu,\"load_ns\":%llu,\"prepare_ns\":%llu,\"run_ns\":%llu,\"teardown_ns\":%llu,"
        "\"quanta\":%llu,\"checksum\":%u,\"pixel_hash\":\"%016llx\",\"instructions\":%llu,\"hosts\":%llu,\"raster\":[",
        workload.c_str(),instrument.c_str(),
        (unsigned long long)(loadBegin-begin),(unsigned long long)(loadEnd-loadBegin),
        (unsigned long long)(setupEnd-loadEnd),(unsigned long long)(runEnd-runBegin),
        (unsigned long long)(teardownEnd-teardownBegin),(unsigned long long)quanta,result,
        (unsigned long long)pixelHash,(unsigned long long)profile.instructionCount,(unsigned long long)profile.hostCount);
    for(size_t i=0;i<size_t(ui::RasterStage::Count);++i) {
        const auto s=static_cast<ui::RasterStage>(i);const auto& v=rasterSnapshot.Stage(s);
        std::printf("%s{\"stage\":\"%s\",\"count\":%llu,\"ns\":%llu,\"max_ns\":%llu}",i?",":"",ui::RasterStageName(s),
            (unsigned long long)v.count,(unsigned long long)v.totalNs,(unsigned long long)v.maxNs);
    }
    const auto& quantum=phases.Phase(ui::ExecutionPhase::Quantum);
    std::printf("],\"blit_path\":\"%s\",\"quantum_count\":%llu,\"quantum_wall_ns\":%llu,\"opcodes\":[",fast?"fast":"reference",
        (unsigned long long)quantum.count,(unsigned long long)quantum.totalNs);bool comma=false;
    for(size_t i=0;i<profile.opcodes.size();++i)if(profile.opcodes[i]) {
        std::printf("%s{\"id\":%zu,\"count\":%llu}",comma?",":"",i,(unsigned long long)profile.opcodes[i]);comma=true;
    }
    std::printf("],\"host_entries\":[");comma=false;
    for(size_t i=0;i<profile.hosts.size();++i)if(profile.hosts[i]) {
        // Numeric IDs refer only to this exact executable/build, not a stable ABI.
        std::printf("%s{\"id\":%zu,\"count\":%llu}",comma?",":"",i,(unsigned long long)profile.hosts[i]);comma=true;
    }
    std::puts("]}");
}
