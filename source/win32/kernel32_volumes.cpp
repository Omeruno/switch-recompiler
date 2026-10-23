// One mount/space provider for ANSI and Unicode drive APIs. No synthetic disk sizes.
#include "hle_util.h"
#include "../runtime/files.h"
#include <algorithm>
#include <cerrno>
#include <limits>

namespace win32 {namespace {
using namespace hle;
uint32_t VolumeError() {return errno==ENODEV?15:errno==EINVAL?123:errno==EROFS?19:3;}
std::string Path(cpu::CpuState& c,cpu::IGuestMemory& m,bool wide) {
    const auto p=Arg(c,m,0);
    return p?(wide?WideToUtf8(ReadWide(m,p)):ReadAnsi(m,p)):rt::FileSystem().CurrentDirectory();
}
void Store64(cpu::IGuestMemory& m,uint32_t p,uint64_t v) {
    if(p){m.Write32(p,uint32_t(v));m.Write32(p+4,uint32_t(v>>32));}
}
HostCallOutcome FreeSpace(cpu::CpuState& c,cpu::IGuestMemory& m,bool wide,bool extended) {
    rt::VolumeInfo v;
    if(!rt::FileSystem().QueryVolume(Path(c,m,wide),v)){SetLastError(m,VolumeError());return Ret(0);}
    if(extended) {
        Store64(m,Arg(c,m,1),v.availableBytes);Store64(m,Arg(c,m,2),v.totalBytes);Store64(m,Arg(c,m,3),v.freeBytes);
    } else {
        // Reporting geometry, not a claim about physical SD sectors. Keep the
        // legacy DWORD cluster counters representable; Ex retains exact bytes.
        uint64_t cluster=std::max<uint64_t>(4096,uint64_t(v.bytesPerSector)*v.sectorsPerCluster);
        while(v.totalBytes/cluster>std::numeric_limits<uint32_t>::max())cluster*=2;
        const uint32_t values[]={uint32_t(cluster/512),512,uint32_t(v.availableBytes/cluster),uint32_t(v.totalBytes/cluster)};
        for(unsigned i=0;i<4;++i)if(Arg(c,m,i+1))m.Write32(Arg(c,m,i+1),values[i]);
    }
    return Ret(1);
}
HostCallOutcome Information(cpu::CpuState& c,cpu::IGuestMemory& m,bool wide) {
    rt::VolumeInfo v;
    if(!rt::FileSystem().QueryVolume(Path(c,m,wide),v)){SetLastError(m,VolumeError());return Ret(0);}
    if((Arg(c,m,1)&&Arg(c,m,2)<=v.label.size()) || (Arg(c,m,6)&&Arg(c,m,7)<=v.fileSystem.size())) {
        SetLastError(m,234);return Ret(0);
    }
    if(wide){WriteWide(m,Arg(c,m,1),Utf8PathToWide(v.label),Arg(c,m,2));WriteWide(m,Arg(c,m,6),Utf8PathToWide(v.fileSystem),Arg(c,m,7));}
    else {WriteAnsi(m,Arg(c,m,1),v.label,Arg(c,m,2));WriteAnsi(m,Arg(c,m,6),v.fileSystem,Arg(c,m,7));}
    if(Arg(c,m,3))m.Write32(Arg(c,m,3),0); // no fabricated physical volume serial
    if(Arg(c,m,4))m.Write32(Arg(c,m,4),v.maxComponent);
    if(Arg(c,m,5))m.Write32(Arg(c,m,5),v.readOnly?0x80000:0);
    return Ret(1);
}
HostCallOutcome Strings(cpu::CpuState& c,cpu::IGuestMemory& m,bool wide) {
    std::string list;const auto mask=rt::FileSystem().DriveMask();
    for(unsigned i=0;i<26;++i)if(mask&(1u<<i)){list+=char('A'+i);list+=":\\";list.push_back(0);}
    const auto count=uint32_t(list.size());list.push_back(0);
    if(Arg(c,m,0)<list.size())return Ret(uint32_t(list.size()));
    if(wide)WriteWide(m,Arg(c,m,1),AnsiToWide(list),uint32_t(list.size()));
    else WriteAnsi(m,Arg(c,m,1),list,uint32_t(list.size()));
    return Ret(count);
}
#define FN(name) HostCallOutcome name(cpu::CpuState& c,cpu::IGuestMemory& m)
FN(GetDiskFreeSpaceA){return FreeSpace(c,m,false,false);}
FN(GetDiskFreeSpaceW){return FreeSpace(c,m,true,false);}
FN(GetDiskFreeSpaceExA){return FreeSpace(c,m,false,true);}
FN(GetDiskFreeSpaceExW){return FreeSpace(c,m,true,true);}
FN(GetVolumeInformationA){return Information(c,m,false);}
FN(GetVolumeInformationW){return Information(c,m,true);}
FN(GetLogicalDriveStringsA){return Strings(c,m,false);}
FN(GetLogicalDriveStringsW){return Strings(c,m,true);}
FN(GetDriveTypeA){return Ret(rt::FileSystem().DriveType(Path(c,m,false)));}
FN(GetDriveTypeW){return Ret(rt::FileSystem().DriveType(Path(c,m,true)));}
HostCallOutcome GetLogicalDrives(cpu::CpuState&,cpu::IGuestMemory&){return Ret(rt::FileSystem().DriveMask());}
#define E(name,bytes) {"KERNEL32.dll",#name,bytes,CallConv::Stdcall,&name}
const HostFunctionEntry entries[]={
    E(GetDiskFreeSpaceA,20),E(GetDiskFreeSpaceW,20),E(GetDiskFreeSpaceExA,16),E(GetDiskFreeSpaceExW,16),
    E(GetVolumeInformationA,32),E(GetVolumeInformationW,32),E(GetLogicalDriveStringsA,8),E(GetLogicalDriveStringsW,8),
    E(GetDriveTypeA,4),E(GetDriveTypeW,4),E(GetLogicalDrives,0)
};
const bool registered=RegisterHostFunctions(entries,sizeof(entries)/sizeof(entries[0]));
}}
