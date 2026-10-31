// Virtual guest account identity. No host account/token or impersonation support.
#include "hle_util.h"
namespace win32 {
namespace {
using namespace hle;
HostCallOutcome UserName(cpu::CpuState& c,cpu::IGuestMemory& m,bool wide) {
    const uint32_t buffer=Arg(c,m,0),size=Arg(c,m,1);
    constexpr uint32_t required=sizeof(kGuestUserName); // Includes terminating NUL.
    if(!size || uint64_t(size)+4>0x100000000ull || !m.IsRangeMapped(size,4)) {
        SetLastError(m,998);return Ret(0);
    }
    const uint32_t capacity=m.Read32(size);
    if(capacity<required){m.Write32(size,required);SetLastError(m,122);return Ret(0);}
    const uint32_t bytes=required*(wide?2:1);
    if(!buffer || uint64_t(buffer)+bytes>0x100000000ull || !m.IsRangeMapped(buffer,bytes)) {
        SetLastError(m,998);return Ret(0);
    }
    for(uint32_t i=0;i<required;++i) {
        if(wide)m.Write16(buffer+2*i,uint8_t(kGuestUserName[i]));
        else m.Write8(buffer+i,uint8_t(kGuestUserName[i]));
    }
    m.Write32(size,required);return Ret(1);
}
HostCallOutcome UserNameA(cpu::CpuState& c,cpu::IGuestMemory& m){return UserName(c,m,false);}
HostCallOutcome UserNameW(cpu::CpuState& c,cpu::IGuestMemory& m){return UserName(c,m,true);}
const HostFunctionEntry entries[]={
    {"ADVAPI32.dll","GetUserNameA",8,CallConv::Stdcall,&UserNameA},
    {"ADVAPI32.dll","GetUserNameW",8,CallConv::Stdcall,&UserNameW},
};
const bool registered=RegisterHostFunctions(entries,sizeof(entries)/sizeof(entries[0]));
} // namespace
} // namespace win32
