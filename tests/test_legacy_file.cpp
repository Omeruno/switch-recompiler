#include "source/runtime/guest_session.h"
#include "source/runtime/files.h"
#include "source/win32/host_functions.h"
#include <filesystem>
#include <fstream>
#include <cassert>
#include <cstdio>
namespace fs=std::filesystem;
int main(){
    char pattern[]="/tmp/sr-legacy-native-XXXXXX";assert(mkdtemp(pattern));const fs::path root=pattern;
    struct Cleanup{fs::path p;~Cleanup(){fs::remove_all(p);}}cleanup{root};
    rt::GuestSession session;auto& p=session.ProcessObject();auto& m=session.Memory();
    assert(m.Map(0x10000,0x10000,cpu::kProtRW));auto& t=p.MainThread();t.id=4;t.teb=0x10000;t.status=rt::GuestThreadStatus::Runnable;
    assert(p.Scheduler().Add(t));p.Scheduler().SetCurrent(&t);rt::FileSystem().SetRoot(root.string());
    auto call=[&](const char* name,std::initializer_list<uint32_t> args){
        auto& c=t.cpu;c.ESP()=0x1e000;m.Write32(c.ESP(),0x12345678);unsigned n=0;
        for(auto a:args)m.Write32(c.ESP()+4+4*n++,a);
        const auto id=win32::FindHostFunction("KERNEL32.dll",name);assert(id>=0);c.eip=win32::IndexToHostAddress(id);
        win32::HostCallOutcome out;assert(win32::TryHandleHostCall(c,m,out));
        assert(!out.customReturn && c.eip==0x12345678 && c.ESP()==0x1e004+n*4);return out.returnValue;
    };
    const uint32_t name=0x12000,ofs=0x13000,buffer=0x14000;auto error=[&](){return m.Read32(t.teb+0x34);};
    auto text=[&](const std::string& value){m.LoadBytes(name,reinterpret_cast<const uint8_t*>(value.c_str()),value.size()+1);};
    text("C:\\safe.dat");m.Write8(0x1ffff,0x77);
    assert(call("OpenFile",{name,0x20000-135,0x1000})==0xffffffff && error()==998 && m.Read8(0x1ffff)==0x77 && !fs::exists(root/"safe.dat"));
    m.Write8(ofs-1,0x61);m.Write8(ofs+136,0x62);
    auto h=call("OpenFile",{name,ofs,0x1000});assert(h!=0xffffffff && m.Read8(ofs)==136 && !m.Read16(ofs+2));
    assert(m.Read8(ofs-1)==0x61 && m.Read8(ofs+136)==0x62);
    assert(call("_lwrite",{h,buffer,0xffffffff})==0xffffffff && error()==998 && call("_llseek",{h,0,1})==0);
    assert(call("_lread",{h,0xfffffffe,8})==0xffffffff && error()==998);
    assert(call("_lwrite",{h,0,0})==0 && call("_lclose",{h})==0);
    text("C:\\"+std::string(124,'a'));assert(call("OpenFile",{name,ofs,0x100})==0);
    text("C:\\"+std::string(125,'a'));assert(call("OpenFile",{name,ofs,0x1000})==0xffffffff && error()==206 && !fs::exists(root/std::string(125,'a')));
    for(unsigned i=0;i<128;++i)m.Write8(ofs+8+i,'x');
    assert(call("OpenFile",{0,ofs,0x8000})==0xffffffff && error()==206 && m.Read16(ofs+2)==206);
    text("C:\\\x80.dat");h=call("OpenFile",{name,ofs,0x1000});assert(h!=0xffffffff && fs::exists(root/u8"€.dat"));assert(call("_lclose",{h})==0);
    fs::create_symlink("/etc/passwd",root/"link");text("C:\\link");assert(call("OpenFile",{name,ofs,0})==0xffffffff && error()==5);
    assert(call("OpenFile",{name,ofs,0x1000})==0xffffffff && error()==5);
    // A real failing stream, not a fabricated status bit or fake successful write.
    auto full=std::make_shared<rt::OpenFile>();full->f=std::fopen("/dev/full","wb");assert(full->f);
    assert(setvbuf(full->f,nullptr,_IONBF,0)==0);full->canWrite=true;h=rt::Handles().Add(full,0x40000000);
    m.Write8(buffer,'q');assert(call("_hwrite",{h,buffer,1})==0xffffffff && error()==112);
    assert(call("_lclose",{h})==0);full.reset();
    auto event=std::make_shared<rt::SyncObject>(rt::ObjectKind::Event);const auto e=rt::Handles().Add(event);
    assert(call("_lclose",{e})==0xffffffff && error()==6 && rt::Handles().Get(e)==event);
    text("C:\\safe.dat");h=call("_lopen",{name,0});assert(h!=0xffffffff);
    assert(rt::Handles().SetFlags(h,rt::kHandleProtectFromClose,rt::kHandleProtectFromClose));
    assert(call("_lclose",{h})==0xffffffff && error()==5 && rt::Files().Get(h));
    assert(rt::Handles().SetFlags(h,rt::kHandleProtectFromClose,0));assert(call("_lclose",{h})==0);
    puts("Legacy file native PASS (exact stdcall cleanup,OFSTRUCT guards/path limits,overflow/no side effects,CP1252 Euro,symlink rejection,real full-media failure,type and close protection)");
}
