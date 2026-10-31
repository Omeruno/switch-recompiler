#include "source/loader/pe_parser.h"
#include "source/runtime/guest_session.h"
#include "source/runtime/guest_execution.h"
#include "source/cpu/interpreter.h"
#include "source/win32/host_functions.h"
#include "source/win32/user32.h"
#include <cassert>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <cstdio>
namespace fs=std::filesystem;
using Bytes=std::vector<uint8_t>;
static Bytes Read(const fs::path& p){std::ifstream f(p,std::ios::binary);assert(f);return {std::istreambuf_iterator<char>(f),{}};}
static void Write(const fs::path& p,const Bytes& b){std::ofstream f(p,std::ios::binary);f.write(reinterpret_cast<const char*>(b.data()),std::streamsize(b.size()));assert(f.good());}
static uint32_t U32(const Bytes& b,size_t at){uint32_t n;assert(at+4<=b.size());std::memcpy(&n,b.data()+at,4);return n;}
static uint16_t U16(const Bytes& b,size_t at){uint16_t n;assert(at+2<=b.size());std::memcpy(&n,b.data()+at,2);return n;}
static void Put(Bytes& b,size_t at,uint32_t n){assert(at+4<=b.size());std::memcpy(b.data()+at,&n,4);}
int main(int argc,char**argv){
    assert(argc==2 || (argc==3 && !std::strcmp(argv[2],"--parse-only")));const fs::path original=fs::absolute(argv[1]);const auto bytes=Read(original);
    char pattern[]="/tmp/sr-old-pe-XXXXXX";assert(mkdtemp(pattern));const fs::path temp=pattern;
    struct Cleanup{fs::path p;~Cleanup(){fs::remove_all(p);}}cleanup{temp};
    pe::PEParser parser;assert(parser.LoadFromFile(original.string())==pe::LoadResult::Ok);
    assert(parser.GetImports().size()==3);
    bool named=false,ordinal=false;for(const auto& module:parser.GetImports())for(const auto& f:module.functions){named|=f.name=="ExitProcess";ordinal|=f.name.empty() && f.ordinal==7;}
    assert(named && ordinal);bool bss=false;
    Bytes image(size_t(parser.GetSizeOfImage())+32,0xa5);parser.MapImageToMemory(image.data()+16,parser.GetSizeOfImage());
    for(size_t n=0;n<16;++n)assert(image[n]==0xa5 && image[image.size()-1-n]==0xa5);
    for(const auto& section:parser.GetSections()){
        assert(section.virtualSize==0);
        if(!section.rawDataPointer && (section.characteristics&0x80)){
            bss=true;assert(section.rawDataSize>0 && parser.RvaToFileOffset(section.virtualAddress)==-1);
            for(size_t n=0;n<section.rawDataSize;++n)assert(image[16+section.virtualAddress+n]==0);
        }else if(section.rawDataSize){
            assert(parser.RvaToFileOffset(section.virtualAddress)==section.rawDataPointer);
            assert(parser.RvaToFileOffset(section.virtualAddress+section.rawDataSize-1)==section.rawDataPointer+section.rawDataSize-1);
            assert(parser.RvaToFileOffset(section.virtualAddress+section.rawDataSize-1,2)==-1);
        }
    }
    assert(bss && parser.RvaToFileOffset(0,2)==0 && parser.RvaToFileOffset(0,SIZE_MAX)==-1 && parser.RvaToFileOffset(UINT32_MAX)==-1);
    const auto pe=U32(bytes,60),opt=pe+24,table=opt+U16(bytes,pe+20);const auto dir=parser.GetDataDirectory(1);
    const auto descriptor=parser.RvaToFileOffset(dir.virtualAddress,20);assert(descriptor>=0);
    auto raw=[&](uint32_t rva){auto at=parser.RvaToFileOffset(rva,4);assert(at>=0);return size_t(at);};
    const auto fallback=temp/"fallback.exe",header=temp/"headers.exe",bad=temp/"bad.bin";
    {auto b=bytes;for(size_t n=0;n<parser.GetImports().size();++n){const auto at=size_t(descriptor)+n*20;const auto ilt=U32(b,at),iat=U32(b,at+16);assert(ilt && iat);
        for(size_t item=0;;++item){const auto v=U32(b,raw(ilt+uint32_t(item)*4));Put(b,raw(iat+uint32_t(item)*4),v);if(!v)break;}Put(b,at,0);}
        Write(fallback,b);pe::PEParser p;assert(p.LoadFromFile(fallback.string())==pe::LoadResult::Ok && p.GetImports().size()==3);
    }
    {auto b=bytes;const auto count=(parser.GetImports().size()+1)*20,start=U32(b,opt+60)-count;
        assert(start>=table+U16(b,pe+6)*40);std::memcpy(b.data()+start,b.data()+descriptor,count);
        Put(b,opt+104,uint32_t(start));Put(b,opt+108,uint32_t(count));
        const auto& name=parser.GetImports()[0].dllName;assert(name.size()<28);std::memcpy(b.data()+32,name.c_str(),name.size()+1);Put(b,start+12,32);
        Write(header,b);pe::PEParser p;assert(p.LoadFromFile(header.string())==pe::LoadResult::Ok && p.GetImports().size()==3 && p.GetImports()[0].dllName==name);
    }
    auto reject=[&](Bytes b){Write(bad,b);pe::PEParser p;assert(p.LoadFromFile(original.string())==pe::LoadResult::Ok);assert(p.LoadFromFile(bad.string())==pe::LoadResult::NotAValidPeHeader && p.GetImports().empty());};
    {auto b=bytes;Put(b,60,0xfffffff0);reject(b);}
    {auto b=bytes;Put(b,60,0xfffffffe);reject(b);}
    {auto b=bytes;b[pe+6]=0xff;b[pe+7]=0xff;reject(b);}
    {auto b=bytes;Put(b,opt+104,0xfffffff0);Put(b,opt+108,40);reject(b);}
    {auto b=bytes;Put(b,opt+108,19);reject(b);}
    {auto b=bytes;Put(b,size_t(descriptor)+12,parser.GetSizeOfImage()-1);reject(b);}
    {auto b=bytes;Put(b,size_t(descriptor)+16,0xfffffff0);reject(b);}
    {auto b=bytes;Put(b,opt+108,uint32_t(parser.GetImports().size()*20));reject(b);}
    bool checkedSection=false;
    for(unsigned i=0;i<U16(bytes,pe+6);++i){const auto at=table+i*40;
        if(U32(bytes,at+12)!=dir.virtualAddress)continue;
        checkedSection=true;
        {auto b=bytes;Put(b,at+20,0xfffffff0);reject(b);}
        {auto b=bytes;const auto size=U32(b,at+16);Put(b,size_t(descriptor)+12,dir.virtualAddress+size-1);b[U32(b,at+20)+size-1]='A';reject(b);}
        {auto b=bytes;const auto size=U32(b,at+16);Put(b,size_t(descriptor),dir.virtualAddress+size-4);Put(b,U32(b,at+20)+size-4,0x80000007);reject(b);}
        {auto b=bytes;const auto rawSize=U32(b,at+16);Put(b,at+8,rawSize+4096);Write(bad,b);pe::PEParser p;assert(p.LoadFromFile(bad.string())==pe::LoadResult::Ok);assert(p.RvaToFileOffset(dir.virtualAddress+rawSize)==-1);}
    }
    assert(checkedSection);
    {auto b=bytes;Put(b,table+12,0xfffffff0);Write(bad,b);pe::PEParser p;
        assert(p.LoadFromFile(bad.string())==pe::LoadResult::Ok);Bytes storage(size_t(p.GetSizeOfImage())+32,0x39);
        p.MapImageToMemory(storage.data()+16,p.GetSizeOfImage());for(size_t n=0;n<16;++n)assert(storage[n]==0x39 && storage[storage.size()-1-n]==0x39);
    }
    assert(parser.LoadFromFile((temp/"missing.exe").string())==pe::LoadResult::FileNotFound && parser.GetImports().empty() && parser.GetSections().empty());
    uint32_t seed=0x3195;
    for(unsigned n=0;n<96;++n){auto b=bytes;if(n%3==0)b.resize(b.size()/2);
        for(unsigned k=0;k<8;++k){seed=seed*1664525u+1013904223u;const size_t at=seed%std::min(b.size(),size_t(1024));seed=seed*1664525u+1013904223u;b[at]^=uint8_t(seed>>24);}
        Write(bad,b);pe::PEParser p;if(p.LoadFromFile(bad.string())==pe::LoadResult::Ok)assert(p.GetImports().size()<=1024);
    }
    if(argc==3){puts("Old PE parser spans PASS (raw/header/zero-size/BSS/EOF/wrap/terminators and96 bounded malformed probes)");return 0;}
    unsigned normal=0,aborted=0;
    for(unsigned mode=0;mode<10;++mode){
        const auto exe=mode<6?original:mode<8?fallback:header;const bool abort=mode==4 || mode==5;
        rt::GuestSession session;auto& p=session.ProcessObject();auto& m=session.Memory();std::string error;
        p.AddSearchPath(original.parent_path().string());
        if(!(mode&1))win32::user::EnableUi(1280,720);else win32::EnableHostLayer(win32::kUiLayer,false);
        assert(p.LoadMainExe(exe.string(),error) && p.PrepareMainThread(p.MainThread().cpu,error));
        const auto probe=p.GetProcAddress(p.MainModule(),"OldPeProbe");assert(probe);m.Write32(probe,abort);
        auto* dll=p.FindByName("old_pe_api.dll");assert(dll && !dll->hle && dll->base!=0x400000);
        bool stopped=false,parked=false;
        {
            rt::GuestExecution execution(p,[&](rt::GuestThread& t){win32::HostCallOutcome call;if(win32::TryHandleHostCall(t.cpu,m,call))return !call.requestExit;
                cpu::Interpreter cpu(m,t.teb,t.teb);const auto result=cpu.Step(t.cpu);assert(result==cpu::StepResult::Continue || result==cpu::StepResult::Halted);return true;
            },rt::GuestExecution::Startup::InitializeModules);
            for(unsigned n=0;n<500000;++n){auto result=execution.RunOne();assert(result!=rt::GuestExecution::Result::AllocationFailed);
                if(abort && m.Read32(probe+4)){parked=true;break;}
                if(result==rt::GuestExecution::Result::Stopped){stopped=true;break;}}
        }
        if(abort){assert(parked && !stopped);++aborted;}
        else {if(p.MainThread().exitCode)fprintf(stderr,"Old PE mode%u failed source line%u\n",mode,p.MainThread().exitCode);assert(stopped && !p.MainThread().exitCode);++normal;}
        assert(m.Read32(probe+4)==1);
        const auto payload=Read(exe.parent_path()/"old-pe-public.txt");assert(std::string(payload.begin(),payload.end())=="old PE named and ordinal imports\r\n");
    }
    assert(normal==8 && aborted==2);
    puts("Old PE imports PASS (8 normal new/legacy x86 sessions +2 parked aborts;zero VirtualSize/raw-backed named+ordinal IAT,relocated DLL/BSS init,header directory/names,OFT fallback,real file bytes,raw-tail/EOF/wrap/unterminated bounds;not DirectX/game acceptance)");
}
