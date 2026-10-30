#include "hle_util.h"
#include "../runtime/resources.h"
#include <algorithm>
namespace win32 {namespace {
using namespace hle;
bool Range(cpu::IGuestMemory& m,uint32_t p,uint64_t size){return p && uint64_t(p)+size<=0x100000000ull && m.IsRangeMapped(p,size);}
HostCallOutcome Load(cpu::CpuState& c,cpu::IGuestMemory& m,bool wide){
    const auto inst=Arg(c,m,0),id=Arg(c,m,1),out=Arg(c,m,2);const int32_t cap=int32_t(Arg(c,m,3));
    if(cap<0){SetLastError(m,87);return Ret(0);}
    if(!wide && !cap)return Ret(0); // ANSI never uses the W-only borrowed-pointer convention
    if(!out){SetLastError(m,998);return Ret(0);}
    if(!cap && !Range(m,out,4)){SetLastError(m,998);return Ret(0);}
    auto* mod=rt::g_process?(inst?rt::g_process->FindByHandle(inst):rt::g_process->MainModule()):nullptr;
    rt::ResourceData data;uint32_t offset=0;uint16_t chars=0;
    if(!rt::FindStringResourceSpan(mod,id,data,offset,chars)){
        if(cap>0){if(!Range(m,out,wide?2:1)){SetLastError(m,998);return Ret(0);}if(wide)m.Write16(out,0);else m.Write8(out,0);}
        SetLastError(m,mod?1814:6);return Ret(0);
    }
    const uint64_t address=uint64_t(mod->base)+data.rva+offset;
    if(address>0xffffffffu || (chars && !Range(m,uint32_t(address),uint64_t(chars)*2))){SetLastError(m,998);return Ret(0);}
    if(!cap){m.Write32(out,uint32_t(address));return Ret(chars);}
    std::u16string text;text.reserve(chars);
    for(unsigned n=0;n<chars;++n)text.push_back(char16_t(m.Read16(uint32_t(address)+n*2)));
    const auto ansi=wide?std::string{}:WideToGuestAnsi(text);
    const size_t count=std::min<size_t>(size_t(cap-1),wide?text.size():ansi.size());
    if(!Range(m,out,uint64_t(count+1)*(wide?2:1))){SetLastError(m,998);return Ret(0);}
    if(wide){for(size_t n=0;n<count;++n)m.Write16(out+uint32_t(n)*2,text[n]);m.Write16(out+uint32_t(count)*2,0);}
    else{for(size_t n=0;n<count;++n)m.Write8(out+uint32_t(n),uint8_t(ansi[n]));m.Write8(out+uint32_t(count),0);}
    return Ret(uint32_t(count));
}
HostCallOutcome LoadStringA(cpu::CpuState& c,cpu::IGuestMemory& m){return Load(c,m,false);}
HostCallOutcome LoadStringW(cpu::CpuState& c,cpu::IGuestMemory& m){return Load(c,m,true);}
const HostFunctionEntry entries[]={
    {"USER32.dll","LoadStringA",16,CallConv::Stdcall,&LoadStringA},
    {"USER32.dll","LoadStringW",16,CallConv::Stdcall,&LoadStringW}
};
const bool registered=RegisterHostFunctions(entries,sizeof(entries)/sizeof(entries[0]));
}}
