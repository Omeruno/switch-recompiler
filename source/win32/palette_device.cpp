#include "user32.h"
#include "gdi.h"
namespace win32 {namespace {
using namespace hle;
bool Valid(uint32_t hdc){return IsHostLayerEnabled(kUiLayer)?ui::g_gdi.Dc(hdc)!=nullptr:gdi::g_gdi.HasDC(hdc);}
HostCallOutcome Unsupported(cpu::CpuState& c,cpu::IGuestMemory& m){
    SetLastError(m,Valid(Arg(c,m,0))?50:6);return Ret(0);
}
HostCallOutcome SetSystemPaletteUse(cpu::CpuState& c,cpu::IGuestMemory& m){
    if(!Valid(Arg(c,m,0))){SetLastError(m,6);return Ret(0);}
    const auto use=Arg(c,m,1);if(use<1 || use>3){SetLastError(m,87);return Ret(0);}
    // Both real display backends are TrueColor (RC_PALETTE clear). A DIB's
    // private indexed color table does not turn its DC into a palettized device.
    return Unsupported(c,m);
}
const HostFunctionEntry entries[]={
    {"GDI32.dll","SetSystemPaletteUse",8,CallConv::Stdcall,&SetSystemPaletteUse},
    {"GDI32.dll","GetSystemPaletteUse",4,CallConv::Stdcall,&Unsupported},
    {"GDI32.dll","GetSystemPaletteEntries",16,CallConv::Stdcall,&Unsupported}
};
const bool registered=RegisterHostFunctions(entries,sizeof(entries)/sizeof(entries[0]));
}}
