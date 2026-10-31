#include "source/runtime/guest_session.h"
#include "source/runtime/guest_execution.h"
#include "source/runtime/files.h"
#include "source/cpu/interpreter.h"
#include "source/win32/host_functions.h"
#include "source/win32/user32.h"
#include "source/win32/virtual_registry.h"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
namespace fs=std::filesystem;
static void Native(const fs::path& root){
    rt::GuestSession session;auto& p=session.ProcessObject();auto& m=session.Memory();
    assert(m.Map(0x10000,0x10000,cpu::kProtRW));assert(m.Map(0x30000,0x10000,cpu::kProtRW));auto& t=p.MainThread();t.id=4;t.teb=0x10000;t.status=rt::GuestThreadStatus::Runnable;
    assert(p.Scheduler().Add(t));p.Scheduler().SetCurrent(&t);rt::FileSystem().SetRoot(root.string());
    auto call=[&](const char* dll,const char* name,uint16_t ordinal,std::initializer_list<uint32_t> args){
        auto& c=t.cpu;c.ESP()=0x1e000;m.Write32(c.ESP(),0x12345678);unsigned n=0;
        for(auto a:args)m.Write32(c.ESP()+4+4*n++,a);
        const auto id=ordinal?win32::FindHostFunctionByOrdinal(dll,ordinal):win32::FindHostFunction(dll,name);assert(id>=0);
        c.eip=win32::IndexToHostAddress(id);win32::HostCallOutcome out;assert(win32::TryHandleHostCall(c,m,out));
        assert(!out.customReturn && c.eip==0x12345678 && c.ESP()==0x1e004+n*4);return out.returnValue;
    };
    auto error=[&](){return m.Read32(t.teb+0x34);};
    const uint32_t text=0x12000,result=0x14000,size=0x16000;
    for(const char* name:{"GetFileAttributesA","GetFileAttributesW"}){
        m.Write16(text,0);assert(call("KERNEL32.dll",name,0,{text})==0xffffffff && error()==3);
        assert(call("KERNEL32.dll",name,0,{0xfffffffe})==0xffffffff && error()==998);
        m.Write16(0x1fffe,0x7171);assert(call("KERNEL32.dll",name,0,{0x1fffe})==0xffffffff && error()==998);
        for(unsigned n=0;n<65536;++n)m.Write8(0x30000+n,'q'); // Entire bounded name lacks NUL.
        assert(call("KERNEL32.dll",name,0,{0x30000})==0xffffffff && error()==206);
    }
    for(const char* name:{"GetFileAttributesExA","GetFileAttributesExW"}){
        m.Write16(text,0);for(unsigned n=0;n<38;++n)m.Write8(result-1+n,0x6a);
        assert(!call("KERNEL32.dll",name,0,{text,0,result}) && error()==3);
        for(unsigned n=0;n<38;++n)assert(m.Read8(result-1+n)==0x6a);
        assert(!call("KERNEL32.dll",name,0,{text,1,result}) && error()==87);
        assert(!call("KERNEL32.dll",name,0,{text,0,0x1ffdd}) && error()==998);
    }
    for(const char* name:{"GetUserNameA","GetUserNameW"}){
        for(unsigned n=0;n<20;++n)m.Write8(result-1+n,0x5c);
        m.Write32(size,0);assert(!call("ADVAPI32.dll",name,0,{0,size}) && error()==122 && m.Read32(size)==7);
        assert(m.Read8(result)==0x5c);
        m.Write32(size,6);assert(!call("ADVAPI32.dll",name,0,{result,size}) && error()==122 && m.Read32(size)==7);
        m.Write32(size,7);assert(call("ADVAPI32.dll",name,0,{result,size}) && m.Read32(size)==7);
        const bool wide=name[11]=='W';const unsigned bytes=wide?14:7;
        assert(m.Read8(result-1)==0x5c && m.Read8(result+bytes)==0x5c);
        for(unsigned n=0;n<7;++n)assert((wide?m.Read16(result+2*n):m.Read8(result+n))==uint8_t("Player"[n]));
        m.Write32(size,100);assert(!call("ADVAPI32.dll",name,0,{0xfffffff9,size}) && error()==998 && m.Read32(size)==100);
        assert(!call("ADVAPI32.dll",name,0,{result,0xfffffffe}) && error()==998);
    }
    for(const char* name:{"RegCreateKeyA","RegCreateKeyW"}){
        m.Write16(text,'X');m.Write16(text+2,0);m.Write32(result,0x12345678);
        assert(call("ADVAPI32.dll",name,0,{0xabcdef,text,result})==6 && m.Read32(result)==0x12345678);
        assert(call("ADVAPI32.dll",name,0,{0x80000001,text,0xfffffffe})==998);
        assert(call("ADVAPI32.dll",name,0,{0x80000001,0xffffffff,result})==998 && m.Read32(result)==0x12345678);
        m.Write32(t.teb+0x34,0x76543210);
        assert(call("ADVAPI32.dll",name,0,{0x80000001,0,result})==0 && m.Read32(result)==0x80000001);
        assert(error()==0x76543210);
    }

    for(unsigned ordinal=0;ordinal<4;++ordinal){
        if(ordinal==1)continue;
        const char* name=ordinal==3?"DirectPlayEnumerateW":"DirectPlayEnumerateA";
        assert(call("DPLAY.dll",name,uint16_t(ordinal),{0xffffffff,0xfffffffe})==0x80004001u);
    }
    for(uint16_t ordinal:{uint16_t(0),uint16_t(1)}){
        m.Write32(result,0xabcdef);
        assert(call("DPLAY.dll","DirectPlayCreate",ordinal,{0xffffffff,result,0})==0x80004001u && !m.Read32(result));
        assert(call("DPLAY.dll","DirectPlayCreate",ordinal,{0,0xfffffffe,0})==0x80070057u);
    }
}
int main(int argc,char** argv){
    assert(argc==2);char pattern[]="/tmp/sr-launcher-XXXXXX";assert(mkdtemp(pattern));const fs::path root=pattern;
    struct Cleanup{fs::path root;~Cleanup(){fs::remove_all(root);}}cleanup{root};
    const auto executable=root/"probe.exe";fs::copy_file(argv[1],executable);
    {std::ofstream f(root/"file.dat");f<<"public!!";assert(f.good());}
    {std::ofstream f(root/u8"café.dat");f<<"authored";assert(f.good());}
    Native(root);unsigned normal=0,aborted=0;
    for(unsigned mode=0;mode<6;++mode){
        rt::GuestSession session;auto& p=session.ProcessObject();auto& m=session.Memory();std::string error;
        if(!(mode&1))win32::user::EnableUi(1280,720);else win32::EnableHostLayer(win32::kUiLayer,false);
        assert(p.LoadMainExe(executable.string(),error) && p.PrepareMainThread(p.MainThread().cpu,error));
        const auto probe=p.GetProcAddress(p.MainModule(),"LauncherProbe");assert(probe);m.Write32(probe,mode>=4);
        bool stopped=false,parked=false;
        {
            rt::GuestExecution execution(p,[&](rt::GuestThread& t){
                win32::HostCallOutcome call;if(win32::TryHandleHostCall(t.cpu,m,call))return !call.requestExit;
                cpu::Interpreter cpu(m,t.teb,t.teb);const auto result=cpu.Step(t.cpu);
                assert(result==cpu::StepResult::Continue || result==cpu::StepResult::Halted);return true;
            },rt::GuestExecution::Startup::InitializeModules);
            for(unsigned n=0;n<300000;++n){const auto result=execution.RunOne();assert(result!=rt::GuestExecution::Result::AllocationFailed);
                if(result==rt::GuestExecution::Result::Stopped){stopped=true;break;}
                if(mode>=4 && m.Read32(probe+4) && p.MainThread().status!=rt::GuestThreadStatus::Runnable){parked=true;break;}}
        }
        if(mode>=4){assert(parked && !stopped);++aborted;}
        else {if(p.MainThread().exitCode)fprintf(stderr,"Launcher mode%u failed guest line%u\n",mode,p.MainThread().exitCode);assert(stopped && !p.MainThread().exitCode);++normal;}
        assert(m.Read32(probe+4)==1);
        bool exists=false;
        const auto key=win32::g_registry.OpenExistingKey(0x80000001,"Software\\PublicLegacyCreate\\Nested",exists);
        assert(key && exists);win32::RegValue value;
        assert(win32::g_registry.GetValue(key,"Value",value) && value.type==4 && value.data==std::vector<uint8_t>({0x52,0x46,0x37,0x19}));
        assert(win32::g_registry.CloseKey(key));
    }
    assert(normal==4 && aborted==2);
    puts("Launcher contract PASS (4 normal new/legacy x86 sessions+2 parked aborts;empty-path refusal/real candidate bytes,ANSI+UTF16 attributes,virtual user size/guards/fresh environment,DirectPlay named+ordinal unsupported HRESULT/ABI/no objects;legacy RegCreateKey A/W real keys/values/close/guards;not DirectX/game acceptance)");
}
