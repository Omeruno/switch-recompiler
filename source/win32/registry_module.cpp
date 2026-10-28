#include "hle_util.h"
#include "virtual_registry.h"
#include <algorithm>
#include <limits>
#include <utility>

namespace win32 {namespace {
using namespace hle;
constexpr uint32_t Invalid=87,NoAccess=998,NotSupported=50,InvalidHandle=6,Denied=5,NoMemory=8;
constexpr size_t MaxValueBytes=1024*1024;
bool Range(cpu::IGuestMemory& m,uint32_t p,size_t bytes) {
    return p && uint64_t(p)+bytes<=uint64_t(std::numeric_limits<uint32_t>::max())+1 && m.IsRangeMapped(p,bytes);
}
uint32_t Name(cpu::IGuestMemory& m,uint32_t p,bool wide,std::string& out) {
    out.clear();if(!p)return 0;
    const unsigned stride=wide?2:1;
    for(size_t i=0;i<=VirtualRegistry::MaxPath;++i) {
        const uint64_t at=uint64_t(p)+i*stride;if(at>0xffffffffu || !Range(m,uint32_t(at),stride))return NoAccess;
        const auto c=wide?m.Read16(uint32_t(at)):m.Read8(uint32_t(at));
        if(!c)return 0;
        // Explicit first contract: ASCII A/W names, never truncate UTF-16/ACP.
        if(c>127)return 1113;
        if(c<32 || c==127)return Invalid;
        if(i==VirtualRegistry::MaxPath)return 206;
        out.push_back(char(c));
    }
    return 206;
}
uint32_t Path(cpu::IGuestMemory& m,uint32_t p,bool wide,std::string& out) {
    const auto error=Name(m,p,wide,out);if(error)return error;
    size_t component=0;
    for(char c:out) {
        if(c=='\\'){if(!component)return Invalid;component=0;}
        else if(++component>255)return 206;
    }
    if(!out.empty() && !component)return Invalid;
    return 0;
}
uint32_t Access(uint32_t& access) {
    if(access&0x300)return NotSupported; // no separate WOW64 views
    if(access&0x80000000u){access&=~0x80000000u;access|=0x20019;}
    if(access&0x40000000u){access&=~0x40000000u;access|=0x20006;}
    if(access&0x10000000u){access&=~0x10000000u;access|=VirtualRegistry::AllAccess;}
    if(access&~VirtualRegistry::AllAccess)return Invalid;
    return 0;
}
HostCallOutcome Open(cpu::CpuState& c,cpu::IGuestMemory& m,bool wide,bool create,bool old=false) {
    const auto parent=Arg(c,m,0),sub=Arg(c,m,1),result=Arg(c,m,old?2:create?7:4);
    const auto disposition=create?Arg(c,m,8):0;
    if(!g_registry.ValidKey(parent))return Ret(InvalidHandle);
    if(!result)return Ret(Invalid);
    if(!Range(m,result,4) || (disposition && !Range(m,disposition,4)))return Ret(NoAccess);
    uint32_t access=old?VirtualRegistry::AllAccess:Arg(c,m,create?5:3);
    const auto denied=Access(access);if(denied)return Ret(denied);
    if(!old && Arg(c,m,2))return Ret(Invalid);
    if(create && (Arg(c,m,4) || Arg(c,m,6)))return Ret(NotSupported);
    std::string name;const auto error=Path(m,sub,wide,name);if(error)return Ret(error);
    if(create && Arg(c,m,3)) {
        std::string cls;const auto e=Name(m,Arg(c,m,3),wide,cls);if(e)return Ret(e);if(!cls.empty())return Ret(NotSupported);
    }
    if(!g_registry.PathFits(parent,name))return Ret(206);
    if(old && name.empty()){m.Write32(result,parent);return Ret(0);}
    bool exists=false;
    const auto handle=create?g_registry.CreateOrOpenKey(parent,name,exists,access):g_registry.OpenExistingKey(parent,name,exists,access);
    if(!handle)return Ret(!create && !exists?2:NoMemory);
    m.Write32(result,handle);if(disposition)m.Write32(disposition,exists?2:1);return Ret(0);
}
bool StringType(uint32_t type){return type==1 || type==2 || type==7;}
HostCallOutcome Set(cpu::CpuState& c,cpu::IGuestMemory& m,bool wide) {
    const auto key=Arg(c,m,0),type=Arg(c,m,3),input=Arg(c,m,4),bytes=Arg(c,m,5);
    if(!g_registry.ValidKey(key))return Ret(InvalidHandle);
    if(!g_registry.CanAccess(key,2))return Ret(Denied);
    if(Arg(c,m,2))return Ret(Invalid);
    if(type!=0 && type!=1 && type!=2 && type!=3 && type!=4 && type!=5 && type!=7 && type!=11)return Ret(NotSupported);
    if((type==4 || type==5) && bytes!=4)return Ret(13);
    if(type==11 && bytes!=8)return Ret(13);
    if(bytes>MaxValueBytes)return Ret(NoMemory);
    if(bytes && !Range(m,input,bytes))return Ret(NoAccess);
    std::string name;const auto error=Name(m,Arg(c,m,1),wide,name);if(error)return Ret(error);
    if(wide && StringType(type) && bytes%2)return Ret(13);
    std::vector<uint8_t> data;
    if(!wide && StringType(type)) {
        if(bytes>MaxValueBytes/2)return Ret(NoMemory);
        for(uint32_t i=0;i<bytes;++i)if(m.Read8(input+i)>127)return Ret(1113);
        data.resize(size_t(bytes)*2);for(uint32_t i=0;i<bytes;++i)data[size_t(i)*2]=m.Read8(input+i);
    }else{data.resize(bytes);for(uint32_t i=0;i<bytes;++i)data[i]=m.Read8(input+i);}
    if(!g_registry.SetValue(key,name,type,data))return Ret(Denied);
    g_registry.SaveToFile(kRegistryFilePath); // existing best-effort provider; not RegFlushKey
    return Ret(0);
}
HostCallOutcome Query(cpu::CpuState& c,cpu::IGuestMemory& m,bool wide) {
    const auto key=Arg(c,m,0),type=Arg(c,m,3),output=Arg(c,m,4),size=Arg(c,m,5);
    if(!g_registry.ValidKey(key))return Ret(InvalidHandle);
    if(!g_registry.CanAccess(key,1))return Ret(Denied);
    if(Arg(c,m,2) || (output && !size))return Ret(Invalid);
    if((type && !Range(m,type,4)) || (size && !Range(m,size,4)))return Ret(NoAccess);
    std::string name;const auto error=Name(m,Arg(c,m,1),wide,name);if(error)return Ret(error);
    RegValue value;if(!g_registry.GetValue(key,name,value))return Ret(2);
    if(value.data.size()>MaxValueBytes)return Ret(NoMemory);
    if(!wide && StringType(value.type)) {
        if(value.data.size()%2)return Ret(13);
        std::vector<uint8_t> ansi(value.data.size()/2);
        for(size_t i=0;i<ansi.size();++i){if(value.data[i*2]>127 || value.data[i*2+1])return Ret(1113);ansi[i]=value.data[i*2];}
        value.data=std::move(ansi);
    }
    const auto capacity=size?m.Read32(size):0;const auto needed=uint32_t(value.data.size());
    if(output && capacity>=needed && needed && !Range(m,output,needed))return Ret(NoAccess);
    if(type)m.Write32(type,value.type);
    if(size)m.Write32(size,needed);
    if(!output)return Ret(0);
    if(capacity<needed)return Ret(234);
    for(uint32_t i=0;i<needed;++i)m.Write8(output+i,value.data[i]);
    return Ret(0);
}
#define FN(name) HostCallOutcome name(cpu::CpuState& c,cpu::IGuestMemory& m)
FN(RegCreateKeyExA){return Open(c,m,false,true);} FN(RegCreateKeyExW){return Open(c,m,true,true);}
FN(RegOpenKeyExA){return Open(c,m,false,false);} FN(RegOpenKeyExW){return Open(c,m,true,false);}
FN(RegOpenKeyA){return Open(c,m,false,false,true);}
FN(RegOpenKeyW){return Open(c,m,true,false,true);}
FN(RegSetValueExA){return Set(c,m,false);} FN(RegSetValueExW){return Set(c,m,true);}
FN(RegQueryValueExA){return Query(c,m,false);} FN(RegQueryValueExW){return Query(c,m,true);}
FN(RegCloseKey){return Ret(g_registry.CloseKey(Arg(c,m,0))?0:InvalidHandle);}
#define E(name,bytes) {"ADVAPI32.dll",#name,bytes,CallConv::Stdcall,&name}
const HostFunctionEntry entries[]={E(RegCreateKeyExA,36),E(RegCreateKeyExW,36),E(RegOpenKeyExA,20),E(RegOpenKeyExW,20),E(RegOpenKeyW,12),E(RegOpenKeyA,12),
    E(RegSetValueExA,24),E(RegSetValueExW,24),E(RegQueryValueExA,24),E(RegQueryValueExW,24),E(RegCloseKey,4)};
const bool registered=RegisterHostFunctions(entries,sizeof(entries)/sizeof(entries[0]));
}}
