#include "source/runtime/guest_session.h"
#include "source/runtime/guest_execution.h"
#include "source/cpu/interpreter.h"
#include "source/win32/host_functions.h"

#include <cassert>
#include <cstdio>
#include "source/runtime/files.h"
#include <fstream>
#include <iterator>
#include <unistd.h>
namespace w=win32;
static std::string Bytes(const std::string& path){std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char** argv){
    assert(argc==2);char dir[]="/tmp/sr-profile-api-XXXXXX";assert(mkdtemp(dir));std::string error;
    const std::string executable=std::string(dir)+"/probe.exe";
    {std::ofstream f(executable,std::ios::binary);const auto bytes=Bytes(argv[1]);assert(!bytes.empty());f<<bytes;assert(f.good());}
    {std::ofstream f(std::string(dir)+"/settings.ini");f<<"; public fixture\n[Game]\nText='hello'\n[Other]\nKeep=yes\n";}
    {std::ofstream f(std::string(dir)+"/unicode.ini",std::ios::binary);f.put(char(255));f.put(char(254));for(char16_t c:std::u16string(u"[Unicode]\r\nText=\u0416\r\n")){f.put(char(c));f.put(char(c>>8));}}
    for(unsigned mode=0;mode<4;++mode){
        rt::GuestSession session;auto& process=session.ProcessObject();auto& memory=session.Memory();
        assert(rt::FileSystem().ConfigurePersistent(dir,"",dir,error));
        assert(process.LoadMainExe(executable,error) && process.PrepareMainThread(process.MainThread().cpu,error));
        const auto probe=process.GetProcAddress(process.MainModule(),"ProfileProbe");assert(probe);memory.Write32(probe,mode);bool stopped=false;
        {
            rt::GuestExecution execution(process,[&](rt::GuestThread& t){
                w::HostCallOutcome call;if(w::TryHandleHostCall(t.cpu,memory,call))return !call.requestExit;
                cpu::Interpreter cpu(memory,t.teb,t.teb);const auto r=cpu.Step(t.cpu);assert(r==cpu::StepResult::Continue || r==cpu::StepResult::Halted);return true;
            },rt::GuestExecution::Startup::InitializeModules);
            for(unsigned n=0;n<500000;++n){const auto r=execution.RunOne();assert(r!=rt::GuestExecution::Result::AllocationFailed);if(r==rt::GuestExecution::Result::Stopped){stopped=true;break;}}
        }
        if(process.MainThread().exitCode)fprintf(stderr,"Profile fixture mode%u failed source line %u\n",mode,process.MainThread().exitCode);
        assert(stopped && !process.MainThread().exitCode && memory.Read32(probe+4)==1);
        const auto bytes=Bytes(std::string(dir)+"/settings.ini");assert(bytes.find("; public fixture")!=std::string::npos && bytes.find("Keep=yes")!=std::string::npos);
        assert(bytes.find(mode>=2?"Score=73":"Score=0x2a")!=std::string::npos);
        assert(Bytes(std::string(dir)+"/WINDOWS/bare.ini").find("Value=real")!=std::string::npos);
        assert(Bytes(std::string(dir)+"/WINDOWS/win.ini").find("Text=saved")!=std::string::npos);
        assert(Bytes(std::string(dir)+"/bare.ini").empty());
    }
    // Explicit bounded files, then directories created by the persistent VFS.
    for(const char* path:{"probe.exe","settings.ini","unicode.ini","WINDOWS/bare.ini","WINDOWS/win.ini"})assert(unlink((std::string(dir)+"/"+path).c_str())==0);
    puts("Profile APIs PASS (4 normal x86 writer/reader sessions,16 A/W exports, real disk bytes, Windows-directory paths, defaults/integers/enumeration/truncation/delete/Unicode/errors/readonly)");
}
