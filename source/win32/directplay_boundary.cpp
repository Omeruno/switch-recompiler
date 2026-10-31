// Explicit unsupported legacy DirectPlay boundary, shared by names and ordinals.
// No provider, callback, connection or COM object is fabricated.
#include "hle_util.h"
namespace win32 { namespace {
using namespace hle;
constexpr uint32_t Unsupported=0x80004001u; // DPERR_UNSUPPORTED / E_NOTIMPL
HostCallOutcome Enumerate(cpu::CpuState&,cpu::IGuestMemory&){return Ret(Unsupported);}
HostCallOutcome Create(cpu::CpuState& c,cpu::IGuestMemory& m) {
    const uint32_t result=Arg(c,m,1);
    if(!result || uint64_t(result)+4>0x100000000ull || !m.IsRangeMapped(result,4))
        return Ret(0x80070057u); // E_INVALIDARG
    m.Write32(result,0);
    return Ret(Unsupported);
}
const HostFunctionEntry entries[]={
    {"DPLAY.dll","DirectPlayCreate",12,CallConv::Stdcall,&Create},
    {"DPLAY.dll","",12,CallConv::Stdcall,&Create,1},
    {"DPLAY.dll","DirectPlayEnumerateA",8,CallConv::Stdcall,&Enumerate},
    {"DPLAY.dll","",8,CallConv::Stdcall,&Enumerate,2},
    {"DPLAY.dll","DirectPlayEnumerateW",8,CallConv::Stdcall,&Enumerate},
    {"DPLAY.dll","",8,CallConv::Stdcall,&Enumerate,3},
};
const bool registered=RegisterHostFunctions(entries,sizeof(entries)/sizeof(entries[0]));
} }
