#include "hle_util.h"
#include "../runtime/ini_profile.h"
#include <algorithm>
namespace win32 {namespace {
using namespace hle;using Text=std::u16string;
bool Range(cpu::IGuestMemory& m,uint32_t p,size_t bytes){return p && uint64_t(p)+bytes<=0x100000000ull && m.IsRangeMapped(p,bytes);}
uint32_t Input(cpu::IGuestMemory& m,uint32_t p,bool wide,Text& text,size_t maximum=rt::IniProfile::MaxLine){
    text.clear();if(!p)return 0;const unsigned step=wide?2:1;
    for(size_t i=0;i<=maximum;++i){const uint64_t at=uint64_t(p)+i*step;if(at>0xffffffff || !Range(m,uint32_t(at),step))return 998;const auto c=wide?m.Read16(uint32_t(at)):m.Read8(uint32_t(at));if(!c)return rt::IniProfile::ValidUtf16(text)?0:1113;if(!wide && c>127)return 1113;if(i==maximum)return 206;text.push_back(char16_t(c));}return 206;
}
uint32_t Path(cpu::IGuestMemory& m,uint32_t p,bool wide,std::string& result){
    Text path;if(!p)path=u"C:\\WINDOWS\\win.ini";else{const auto error=Input(m,p,wide,path,4096);if(error)return error;if(path.empty())return 87;}
    if(path.find_first_of(u"\\/:")==Text::npos)path=u"C:\\WINDOWS\\"+path;
    for(char16_t c:path)if(c<32)return 123;
    result=WideToUtf8(path);return result.size()>4096?206:0;
}
HostCallOutcome Fail(cpu::IGuestMemory& m,uint32_t error){SetLastError(m,error);return Ret(0);}
HostCallOutcome Output(cpu::IGuestMemory& m,uint32_t p,uint32_t capacity,bool wide,Text text,bool multi){
    if(!capacity)return Ret(0);
    uint32_t result=0;
    if(multi){if(text.empty())text.push_back(0);const auto required=text.size()+1;
        if(required>=capacity){text.resize(capacity>=2?capacity-2:0);result=capacity>=2?capacity-2:0;text.push_back(0);if(capacity>=2)text.push_back(0);}
        else{result=text.size()==1 && !text[0]?0:uint32_t(text.size());text.push_back(0);}
    }else{if(text.size()>=capacity)text.resize(capacity-1);result=uint32_t(text.size());text.push_back(0);}
    if(!wide)for(auto ch:text)if(ch>127)return Fail(m,1113);
    if(!Range(m,p,text.size()*(wide?2:1)))return Fail(m,p?998:87);
    for(size_t i=0;i<text.size();++i){if(wide)m.Write16(p+uint32_t(i*2),text[i]);else m.Write8(p+uint32_t(i),uint8_t(text[i]));}return Ret(result);
}
// modes: string, integer, section names, section key=value entries.
HostCallOutcome Read(cpu::CpuState& c,cpu::IGuestMemory& m,bool wide,bool profile,unsigned mode){
    const bool integer=mode==1;const uint32_t output=integer?0:Arg(c,m,mode==2?0:mode==3?1:3),capacity=integer?0:Arg(c,m,mode==2?1:mode==3?2:4);
    if(!integer && !capacity)return Ret(0);
    const auto sectionPtr=mode==2?0:Arg(c,m,0),keyPtr=mode>=2?0:Arg(c,m,1);
    Text section,key,fallback;uint32_t error=Input(m,sectionPtr,wide,section);if(!error && sectionPtr && mode<2)error=Input(m,keyPtr,wide,key);
    if(!error && !integer && mode==0 && sectionPtr && keyPtr)error=Input(m,Arg(c,m,2),wide,fallback);
    std::string path;const uint32_t file=profile?0:Arg(c,m,mode==2?2:mode==3?3:integer?3:5);if(!error)error=Path(m,file,wide,path);if(error)return Fail(m,error);
    rt::IniProfile ini;error=ini.Load(rt::FileSystem(),path);if(error)return Fail(m,error);
    if(integer){if(!sectionPtr || !keyPtr)return Ret(Arg(c,m,2));const auto value=ini.Read(section,key,{});return Ret(value.empty()?Arg(c,m,2):rt::IniProfile::Integer(value));}
    const bool multi=mode>=2 || !sectionPtr || !keyPtr;Text text;
    if(multi){const auto values=ini.List(sectionPtr?&section:nullptr,mode==3);for(const auto& value:values){text+=value;text.push_back(0);}}
    else text=ini.Read(section,key,fallback);
    return Output(m,output,capacity,wide,std::move(text),multi);
}
HostCallOutcome Write(cpu::CpuState& c,cpu::IGuestMemory& m,bool wide,bool profile){
    const auto sectionPtr=Arg(c,m,0),keyPtr=Arg(c,m,1),valuePtr=Arg(c,m,2);Text section,key,value;std::string path;
    auto error=Path(m,profile?0:Arg(c,m,3),wide,path);if(error)return Fail(m,error);
    if(!sectionPtr){if(keyPtr || valuePtr)return Fail(m,87);rt::IniProfile ini;error=ini.Load(rt::FileSystem(),path);return error?Fail(m,error):Ret(1);}
    error=Input(m,sectionPtr,wide,section);if(!error && keyPtr)error=Input(m,keyPtr,wide,key);if(!error && keyPtr && valuePtr)error=Input(m,valuePtr,wide,value);if(error)return Fail(m,error);
    rt::IniProfile ini;error=ini.Write(rt::FileSystem(),path,section,keyPtr?&key:nullptr,keyPtr && valuePtr?&value:nullptr);return error?Fail(m,error):Ret(1);
}
#define FN(name) HostCallOutcome name(cpu::CpuState& c,cpu::IGuestMemory& m)
#define READ_PAIR(name,profile,mode) FN(name##A){return Read(c,m,false,profile,mode);} FN(name##W){return Read(c,m,true,profile,mode);}
#define WRITE_PAIR(name,profile) FN(name##A){return Write(c,m,false,profile);} FN(name##W){return Write(c,m,true,profile);}
READ_PAIR(GetPrivateProfileString,false,0) READ_PAIR(GetPrivateProfileInt,false,1)
READ_PAIR(GetProfileString,true,0) READ_PAIR(GetProfileInt,true,1)
READ_PAIR(GetPrivateProfileSectionNames,false,2) READ_PAIR(GetPrivateProfileSection,false,3)
WRITE_PAIR(WritePrivateProfileString,false) WRITE_PAIR(WriteProfileString,true)
#define E(name,bytes) {"KERNEL32.dll",#name,bytes,CallConv::Stdcall,&name}
#define EW(name,bytes) E(name##A,bytes),E(name##W,bytes)
const HostFunctionEntry entries[]={EW(GetPrivateProfileString,24),EW(GetPrivateProfileInt,16),EW(GetProfileString,20),EW(GetProfileInt,12),EW(GetPrivateProfileSectionNames,12),EW(GetPrivateProfileSection,16),EW(WritePrivateProfileString,16),EW(WriteProfileString,12)};
const bool registered=RegisterHostFunctions(entries,sizeof(entries)/sizeof(entries[0]));
}}
