#include "user32.h"
#include "../runtime/clock.h"
namespace win32 {
HostCallOutcome RegisteredMessageCall(cpu::CpuState& c,cpu::IGuestMemory& m,bool wide){
    using namespace hle;const auto pointer=Arg(c,m,0);std::u16string text;std::string bytes;
    if(!pointer){SetLastError(m,87);return Ret(0);}
    for(unsigned n=0;n<256;++n){
        const uint64_t at=uint64_t(pointer)+n*(wide?2:1),size=wide?2:1;
        if(at+size>0x100000000ull || !m.IsRangeMapped(uint32_t(at),size)){SetLastError(m,998);return Ret(0);}
        const auto ch=wide?m.Read16(uint32_t(at)):m.Read8(uint32_t(at));
        if(!ch){
            if(!wide)text=Utf8PathToWide(AnsiPathToUtf8(bytes));
            if(text.empty()){SetLastError(m,87);return Ret(0);}
            const auto id=user::RegisterMessage(std::move(text));if(!id)SetLastError(m,8);return Ret(id);
        }
        if(wide)text.push_back(char16_t(ch));else bytes.push_back(char(ch));
    }
    SetLastError(m,206);return Ret(0);
}
namespace {
HostCallOutcome RegisterWindowMessageA(cpu::CpuState& c,cpu::IGuestMemory& m){return RegisteredMessageCall(c,m,false);}
HostCallOutcome RegisterWindowMessageW(cpu::CpuState& c,cpu::IGuestMemory& m){return RegisteredMessageCall(c,m,true);}
HostCallOutcome timeGetTime(cpu::CpuState&,cpu::IGuestMemory&){return hle::Ret(g_getTickCountImpl?g_getTickCountImpl():rt::Clock::NowMs());}
const HostFunctionEntry entries[]={
    {"USER32.dll","RegisterWindowMessageA",4,CallConv::Stdcall,&RegisterWindowMessageA},
    {"USER32.dll","RegisterWindowMessageW",4,CallConv::Stdcall,&RegisterWindowMessageW},
    {"WINMM.dll","timeGetTime",0,CallConv::Stdcall,&timeGetTime}
};
const bool registered=RegisterHostFunctions(entries,sizeof(entries)/sizeof(entries[0]));
}
}
