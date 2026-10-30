#include "window_lookup.h"
#include "window_manager.h"
#include "user32.h"
#include <algorithm>
namespace win32 {
bool ReadWindowLookupText(cpu::IGuestMemory& m,uint32_t pointer,bool wide,std::u16string& text) {
    text.clear();
    if (!pointer) return true;
    std::string bytes;
    for (unsigned n=0;n<32768;++n) {
        const uint64_t at=uint64_t(pointer)+n*(wide?2:1),size=wide?2:1;
        if (at+size>0x100000000ull || !m.IsRangeMapped(uint32_t(at),size)) {
            hle::SetLastError(m,998);return false;
        }
        const auto ch=wide?m.Read16(uint32_t(at)):m.Read8(uint32_t(at));
        if (!ch) {
            if (!wide) text=hle::Utf8PathToWide(AnsiPathToUtf8(bytes));
            return true;
        }
        if (wide) text.push_back(char16_t(ch));else bytes.push_back(char(ch));
    }
    hle::SetLastError(m,206);return false;
}
namespace {
char16_t Fold(char16_t c) {return c>=u'A' && c<=u'Z'?char16_t(c+32):c;}
bool Equal(const std::u16string& a,const std::u16string& b) {
    return a.size()==b.size() && std::equal(a.begin(),a.end(),b.begin(),
        [](char16_t x,char16_t y){return Fold(x)==Fold(y);});
}
HostCallOutcome Lookup(cpu::CpuState& c,cpu::IGuestMemory& m,bool wide,bool ex) {
    using namespace hle;
    const unsigned offset=ex?2:0;
    const auto classPtr=Arg(c,m,offset),titlePtr=Arg(c,m,offset+1);
    const uint16_t atom=classPtr && classPtr<0x10000?uint16_t(classPtr):0;
    std::u16string cls,title;
    if ((!atom && !ReadWindowLookupText(m,classPtr,wide,cls)) ||
        !ReadWindowLookupText(m,titlePtr,wide,title)) return Ret(0);
    const bool modern=IsHostLayerEnabled(kUiLayer);
    uint32_t parent=ex?Arg(c,m,0):0,after=ex?Arg(c,m,1):0;
    // Message-only windows are not implemented by either window store.
    if (parent==0xfffffffdu) {SetLastError(m,120);return Ret(0);}
    std::vector<uint32_t> children;
    if (modern) {
        user::Init();
        auto* p=parent?user::Win(parent):user::Desktop();
        if (!p) {SetLastError(m,1400);return Ret(0);}
        children=p->children;
    } else {
        if (parent && !g_windowManager.GetWindow(parent)) {SetLastError(m,1400);return Ret(0);}
        children=g_windowManager.Children(parent);
    }
    size_t first=0;
    if (after) {
        const auto found=std::find(children.begin(),children.end(),after);
        if (found==children.end()) {SetLastError(m,1400);return Ret(0);}
        first=size_t(found-children.begin())+1;
    }
    for (size_t i=first;i<children.size();++i) {
        const auto handle=children[i];
        if (modern) {
            const auto* w=user::Win(handle);
            if (!w || !w->cls) continue;
            if (classPtr && !(atom?w->cls->atom==atom:Equal(cls,w->cls->name))) continue;
            if (titlePtr && !Equal(title,w->text)) continue;
        } else {
            const auto* w=g_windowManager.GetWindow(handle);
            const auto* wc=g_windowManager.GetWindowClass(handle);
            if (!w || !wc) continue;
            if (classPtr && !(atom?wc->atom==atom:Equal(cls,Utf8PathToWide(w->className)))) continue;
            if (titlePtr && !Equal(title,w->title)) continue;
        }
        return Ret(handle);
    }
    return Ret(0); // A miss preserves LastError, as does a hit.
}
HostCallOutcome FindWindowA(cpu::CpuState& c,cpu::IGuestMemory& m){return Lookup(c,m,false,false);}
HostCallOutcome FindWindowW(cpu::CpuState& c,cpu::IGuestMemory& m){return Lookup(c,m,true,false);}
HostCallOutcome FindWindowExA(cpu::CpuState& c,cpu::IGuestMemory& m){return Lookup(c,m,false,true);}
HostCallOutcome FindWindowExW(cpu::CpuState& c,cpu::IGuestMemory& m){return Lookup(c,m,true,true);}
const HostFunctionEntry entries[]={
    {"USER32.dll","FindWindowA",8,CallConv::Stdcall,&FindWindowA},
    {"USER32.dll","FindWindowW",8,CallConv::Stdcall,&FindWindowW},
    {"USER32.dll","FindWindowExA",16,CallConv::Stdcall,&FindWindowExA},
    {"USER32.dll","FindWindowExW",16,CallConv::Stdcall,&FindWindowExW}
};
const bool registered=RegisterHostFunctions(entries,sizeof(entries)/sizeof(entries[0]));
}
}
