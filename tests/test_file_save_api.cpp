#include "source/runtime/guest_session.h"
#include "source/runtime/files.h"
#include "source/win32/host_functions.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unistd.h>
using namespace rt;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"file save %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
int main() {
    GuestSession session;auto& p=session.ProcessObject();auto& m=session.Memory();CHECK(m.Map(0x10000,0x80000,cpu::kProtRW));
    auto& t=p.MainThread();t.id=4;t.teb=0x10000;t.status=GuestThreadStatus::Runnable;CHECK(p.Scheduler().Add(t));p.Scheduler().SetCurrent(&t);
    auto call=[&](const char* name,std::initializer_list<uint32_t> args) {
        auto& c=t.cpu;c.ESP()=0x8f000;m.Write32(c.ESP(),0x123456);unsigned n=0;for(auto a:args)m.Write32(c.ESP()+4+4*n++,a);
        auto id=win32::FindHostFunction("KERNEL32.dll",name);CHECK(id>=0);c.eip=win32::IndexToHostAddress(id);win32::HostCallOutcome result;
        CHECK(win32::TryHandleHostCall(c,m,result));CHECK(!result.customReturn && c.eip==0x123456);return result.returnValue;
    };
    auto error=[&](){return m.Read32(t.teb+0x34);};const uint32_t result=0x11000,buffer=0x20000;
    auto add=[&](FILE* stream,bool read,bool write,uint32_t access=0xffffffffu) {
        CHECK(stream);auto file=std::make_shared<OpenFile>();file->f=stream;file->canRead=read;file->canWrite=write;return Handles().Add(file,access);
    };
    const auto h=add(std::tmpfile(),true,true);std::string bytes(200003,'q');m.LoadBytes(buffer,reinterpret_cast<const uint8_t*>(bytes.data()),bytes.size());
    CHECK(call("WriteFile",{h,buffer,uint32_t(bytes.size()),result,0})==1 && m.Read32(result)==bytes.size());
    CHECK(call("FlushFileBuffers",{h})==1);CHECK(call("GetFileSizeEx",{h,result})==1 && m.Read32(result)==bytes.size());
    CHECK(call("SetFilePointer",{h,0,0,0})==0);CHECK(call("ReadFile",{h,buffer,uint32_t(bytes.size()),result,0})==1 && m.Read32(result)==bytes.size());
    CHECK(m.Read8(buffer+uint32_t(bytes.size())-1)=='q');CHECK(call("ReadFile",{h,buffer,3,result,0})==1 && m.Read32(result)==0);
    CHECK(call("ReadFile",{h,0,0,result,0})==1);CHECK(call("WriteFile",{h,0,0,result,0})==1);
    CHECK(!call("WriteFile",{h,buffer,0xffffffffu,result,0}) && error()==998 && m.Read32(result)==0);
    CHECK(!call("ReadFile",{h,0xeeee0000,20,result,0}) && error()==998);
    CHECK(!call("WriteFile",{h,buffer,1,result,0x12000}) && error()==50);
    CHECK(!call("FlushFileBuffers",{123}) && error()==6);
    auto file=Files().Get(h);const auto denied=Handles().Add(file,1);CHECK(!call("FlushFileBuffers",{denied}) && error()==5);
    CHECK(!call("WriteFile",{denied,buffer,1,result,0}) && error()==5);
    const auto alias=Handles().Duplicate(h);CHECK(call("CloseHandle",{h}));CHECK(call("FlushFileBuffers",{alias}));
    CHECK(call("SetFilePointer",{alias,7,0,0})==7);CHECK(call("SetEndOfFile",{alias}));
    CHECK(call("GetFileSize",{alias,result})==7 && m.Read32(result)==0);
    CHECK(call("CloseHandle",{denied}));CHECK(call("CloseHandle",{alias}));CHECK(!call("FlushFileBuffers",{alias}) && error()==6);file.reset();
    auto nul=std::make_shared<OpenFile>();nul->kind=OpenFile::Null;nul->canWrite=nul->canRead=true;const auto nullHandle=Handles().Add(nul);
    CHECK(call("ReadFile",{nullHandle,buffer,20,result,0}) && m.Read32(result)==0);
    CHECK(call("WriteFile",{nullHandle,buffer,20,result,0}) && m.Read32(result)==20);CHECK(!call("FlushFileBuffers",{nullHandle}) && error()==50);
    auto directory=std::make_shared<OpenFile>();directory->kind=OpenFile::Directory;directory->canRead=directory->canWrite=true;const auto dir=Handles().Add(directory);
    CHECK(!call("ReadFile",{dir,buffer,1,result,0}) && error()==6);CHECK(!call("GetFileSizeEx",{dir,result}) && error()==6);
    // Actual full-device errors: data buffered in stdio must fail on real flush,
    // without pretending the original byte-count meant durable storage.
    FILE* full=std::fopen("/dev/full","wb");CHECK(full);CHECK(setvbuf(full,nullptr,_IOFBF,8192)==0);const auto failing=add(full,false,true);
    CHECK(call("WriteFile",{failing,buffer,3,result,0}));CHECK(!call("FlushFileBuffers",{failing}) && error()==112);
    CHECK(call("WriteFile",{failing,buffer,3,result,0}));CHECK(!call("GetFileSizeEx",{failing,result}) && error()==112);
    CHECK(call("WriteFile",{failing,buffer,3,result,0}));CHECK(!call("SetEndOfFile",{failing}) && error()==112);
    CHECK(call("CloseHandle",{failing}));
    std::puts("PASSED: actual synchronous file save/flush/size/truncate, bounded buffers, HANDLE access/alias lifetime and real full-media errors");
}
