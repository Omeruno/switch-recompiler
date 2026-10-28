#include "source/runtime/guest_session.h"
#include "source/runtime/guest_execution.h"
#include "source/cpu/interpreter.h"
#include "source/win32/host_functions.h"
#include "source/win32/virtual_registry.h"
#include <cassert>
#include <cstdio>
namespace w=win32;
static void Budget() {
    w::VirtualRegistry registry;bool existed=false;const auto first=registry.CreateOrOpenKey(w::HKEY_CURRENT_USER,"Budget",existed);assert(first);
    for(size_t n=1;n<registry.MaxHandles;++n){bool found=false;assert(registry.OpenExistingKey(w::HKEY_CURRENT_USER,"Budget",found) && found);}
    assert(!registry.CreateOrOpenKey(w::HKEY_CURRENT_USER,"Unaccepted",existed));
    bool found=false;assert(!registry.OpenExistingKey(w::HKEY_CURRENT_USER,"Unaccepted",found) && !found);
    assert(registry.CloseKey(first) && !registry.ValidKey(first));
    assert(!registry.SaveToFile("/dev/full"));
}
static void Nested() {
    bool existed=false;const auto outer=w::g_registry.CreateOrOpenKey(w::HKEY_CURRENT_USER,"PublicRegistryOuter",existed);
    assert(outer && w::g_registry.SetValue(outer,"value",4,{7,0,0,0}));
    {
        rt::GuestSession inner;assert(!w::g_registry.ValidKey(outer));bool found=false;
        const auto read=w::g_registry.OpenExistingKey(w::HKEY_CURRENT_USER,"PublicRegistryOuter",found,1);
        assert(found && read && !w::g_registry.SetValue(read,"value",4,{8,0,0,0}));
        assert(w::g_registry.CloseKey(read) && !w::g_registry.ValidKey(read));
    }
    w::RegValue value;assert(w::g_registry.GetValue(outer,"value",value) && value.data==std::vector<uint8_t>({7,0,0,0}));
    assert(w::g_registry.CanAccess(outer,2) && w::g_registry.CloseKey(outer));
}
int main(int argc,char** argv) {
    assert(argc==2);Budget();Nested();uint32_t stale=0;
    for(uint32_t mode=0;mode<4;++mode){
        {
            rt::GuestSession session;auto& process=session.ProcessObject();auto& memory=session.Memory();std::string error;
            assert(process.LoadMainExe(argv[1],error) && process.PrepareMainThread(process.MainThread().cpu,error));
            const auto probe=process.GetProcAddress(process.MainModule(),"RegistryProbe");assert(probe);
            memory.Write32(probe,mode);memory.Write32(probe+4,stale);bool stopped=false;
            {
                rt::GuestExecution execution(process,[&](rt::GuestThread& t){
                    w::HostCallOutcome call;if(w::TryHandleHostCall(t.cpu,memory,call))return !call.requestExit;
                    cpu::Interpreter cpu(memory,t.teb,t.teb);const auto r=cpu.Step(t.cpu);assert(r==cpu::StepResult::Continue || r==cpu::StepResult::Halted);return true;
                },rt::GuestExecution::Startup::InitializeModules);
                for(unsigned n=0;n<500000;++n){
                    const auto r=execution.RunOne();assert(r!=rt::GuestExecution::Result::AllocationFailed);
                    if(r==rt::GuestExecution::Result::Stopped){stopped=true;break;}
                }
            }
            if(process.MainThread().exitCode)fprintf(stderr,"Registry fixture mode%u failed source line %u\n",mode,process.MainThread().exitCode);
            assert(stopped && process.MainThread().exitCode==0 && memory.Read32(probe+12)==1);stale=memory.Read32(probe+8);
        }
        bool found=false;const auto key=w::g_registry.OpenExistingKey(w::HKEY_CURRENT_USER,"Software\\SwitchRecompiler\\PublicRegistry",found);
        w::RegValue value;assert(found && key && w::g_registry.GetValue(key,"Number",value) && value.type==4 && value.data.size()==4);
        const uint32_t actual=value.data[0]|uint32_t(value.data[1])<<8|uint32_t(value.data[2])<<16|uint32_t(value.data[3])<<24;
        assert(actual==0x12345678u+(mode&2));
        assert(w::g_registry.GetValue(key,"Text",value) && value.type==1 && value.data==std::vector<uint8_t>({'h',0,'e',0,'l',0,'l',0,'o',0,0,0}));
        assert(w::g_registry.CloseKey(key));
    }
    puts("Registry keys PASS (4 normal x86 sessions, real A/W creation/open/values/rights/close, parent paths, checked memory/errors, stored bytes and suspended-session grants)");
}
