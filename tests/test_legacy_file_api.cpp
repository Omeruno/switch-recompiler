#include "source/runtime/guest_session.h"
#include "source/runtime/guest_execution.h"
#include "source/cpu/interpreter.h"
#include "source/win32/host_functions.h"
#include "source/runtime/files.h"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <cassert>
#include <cstdio>
namespace fs=std::filesystem;
static std::string Bytes(const fs::path& path){std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char** argv){
    assert(argc==2);char pattern[]="/tmp/sr-legacy-api-XXXXXX";assert(mkdtemp(pattern));const fs::path root=pattern;
    struct Cleanup{fs::path root;~Cleanup(){fs::remove_all(root);}}cleanup{root};
    fs::create_directories(root/"app");fs::create_directory(root/"cwd");fs::create_directory(root/"env");fs::create_directory(root/"rom");
    fs::create_directories(root/"WINDOWS/system32");
    const auto executable=root/"app/probe.exe";fs::copy_file(argv[1],executable);
    for(const auto& pair:{std::pair<const char*,const char*>{"app/priority.dat","A"},{"cwd/priority.dat","B"},{"WINDOWS/system32/system.dat","S"},{"env/environment.dat","P"},{"rom/readonly.dat","R"}}){std::ofstream f(root/pair.first);f<<pair.second;assert(f.good());}
    for(unsigned mode:{0u,1u,2u,3u,1u}){
        std::vector<std::weak_ptr<rt::KernelObject>> remaining;{
        rt::GuestSession session;auto& p=session.ProcessObject();auto& m=session.Memory();std::string error;
        assert(rt::FileSystem().ConfigurePersistent(root.string(),"",(root/"rom").string(),error));
        assert(p.LoadMainExe(executable.string(),error) && p.PrepareMainThread(p.MainThread().cpu,error));
        assert(rt::FileSystem().SetCurrentDirectory("C:\\cwd"));
        const auto probe=p.GetProcAddress(p.MainModule(),"LegacyFileProbe");assert(probe);m.Write32(probe,mode);
        bool stopped=false,parked=false;
        {
            rt::GuestExecution execution(p,[&](rt::GuestThread& thread){
                win32::HostCallOutcome call;if(win32::TryHandleHostCall(thread.cpu,m,call))return !call.requestExit;
                cpu::Interpreter interpreter(m,thread.teb,thread.teb);const auto result=interpreter.Step(thread.cpu);
                assert(result==cpu::StepResult::Continue || result==cpu::StepResult::Halted);return true;
            },rt::GuestExecution::Startup::InitializeModules);
            for(unsigned n=0;n<2000000;++n){
                const auto result=execution.RunOne();assert(result!=rt::GuestExecution::Result::AllocationFailed);
                if(result==rt::GuestExecution::Result::Stopped){stopped=true;break;}
                if(mode==3 && m.Read32(probe+12) && p.MainThread().status!=rt::GuestThreadStatus::Runnable){parked=true;break;}
            }
            // Destructor drains the genuinely parked native continuation.
        }
        if(mode!=3 && p.MainThread().exitCode)fprintf(stderr,"Legacy fixture mode%u failed source line %u\n",mode,p.MainThread().exitCode);
        assert(m.Read32(probe+4)==1);
        assert(mode==3?(parked && !stopped):(stopped && !p.MainThread().exitCode));
        const auto handle=m.Read32(probe+8);
        assert(bool(p.Handles().Get(handle))==(mode==2 || mode==3));
        for(const auto& object:p.Handles().Objects())remaining.push_back(object);
        const auto data=Bytes(root/"legacy.bin");assert(data.size()==20001);
        for(size_t n=0;n<19001;++n)assert(data[n]==char(n*17+3));
        for(size_t n=19001;n<data.size();++n)assert(data[n]==0);
        assert(Bytes(root/u8"café.dat")=="cp1252");
        assert(Bytes(root/"rom/readonly.dat")=="R" && !fs::exists(root/"delete.dat") && !fs::exists(root/"bad.dat") && !fs::exists(root/"cwd/missing.dat"));
        }for(const auto& weak:remaining)assert(weak.expired());
    }
    puts("Legacy file APIs PASS (4 normal x86 sessions + parked abort;9 stdcall exports;real >8KiB bytes,EOF/truncate/extend,OFSTRUCT/reopen/exist/delete/parse,application/CWD/system/PATH search,CP1252,readonly,alias/cleanup/errors;no game acceptance)");
}
