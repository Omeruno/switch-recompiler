#include "source/runtime/files.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sys/statvfs.h>
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"VFS line %d: %s (errno %d)\n",__LINE__,#x,errno);std::exit(1);}}while(0)
int main() {
    char pattern[]="/tmp/sr-vfs-XXXXXX";const char* temp=mkdtemp(pattern);CHECK(temp);
    const fs::path base=temp,sd=base/"sd",rom=base/"rom",system=sd/"switch-recompiler/drive_c";
    struct Cleanup {fs::path path;~Cleanup(){fs::remove_all(path);}} cleanup{base};
    fs::create_directories(sd/"Apps/One");fs::create_directories(sd/"Apps/Two");fs::create_directory(rom);
    std::ofstream(rom/"read.txt")<<"rom bytes";
    rt::Vfs v;std::string error;CHECK(v.ConfigurePersistent(system.string(),sd.string(),rom.string(),error));
    CHECK(v.Persistent() && v.DriveMask()==((1u<<2)|(1u<<17)|(1u<<18)));
    CHECK(v.CurrentDirectory()=="C:\\");
    CHECK(fs::is_directory(system/"Documents and Settings/Player/My Documents/My Pictures"));
    CHECK(v.ProfileDirectory()=="C:\\Documents and Settings\\Player");
    CHECK(v.ToHost("C:\\Programs")==std::string(system/"Programs"));
    CHECK(v.ToHost("S:\\Apps\\One")==std::string(sd/"Apps/One"));
    CHECK(v.ToHost("R:\\read.txt")==std::string(rom/"read.txt"));
    CHECK(v.ToGuest(std::string(system/"Programs"))=="C:\\Programs");
    CHECK(v.ToGuest(std::string(sd/"Apps/One"))=="S:\\Apps\\One");
    CHECK(v.ToGuest(std::string(rom/"read.txt"))=="R:\\read.txt");
    CHECK(v.SetCurrentDirectory("S:\\Apps\\One"));
    CHECK(v.FullGuestPath("relative.txt")=="S:\\Apps\\One\\relative.txt");
    CHECK(v.FullGuestPath("\\root.txt")=="S:\\root.txt");
    CHECK(v.FullGuestPath("C:relative.txt")=="C:\\relative.txt");
    CHECK(v.SetCurrentDirectory("C:\\Programs"));
    CHECK(v.FullGuestPath("S:relative.txt")=="S:\\Apps\\One\\relative.txt");
    CHECK(v.FullGuestPath("C:\\..\\..\\Programs\\.\\x")=="C:\\Programs\\x");
    CHECK(!v.SetCurrentDirectory("C:\\missing"));CHECK(v.CurrentDirectory()=="C:\\Programs");
    CHECK(v.FullGuestPath("D:\\x").empty() && errno==ENODEV);
    CHECK(v.ToHost("\\\\server\\share").empty() && errno==EINVAL);
    CHECK(v.ToHost("C:\\file:stream").empty() && errno==EINVAL);
    CHECK(v.ToHost(std::string(4097,'x')).empty());
    CHECK(v.ToHost("R:\\read.txt",true).empty() && errno==EROFS);
    CHECK(v.ToHost("R:\\missing.txt",true).empty() && errno==EROFS);
    fs::create_directory(system/"Programs/MiXeD");
    std::ofstream(system/"Programs/MiXeD/данные.txt")<<"unicode";
    const auto unicode=v.ToHost("c:\\programs\\mixed\\данные.txt");
    CHECK(unicode==std::string(system/"Programs/MiXeD/данные.txt"));
    CHECK(v.ToGuest(unicode)=="C:\\Programs\\MiXeD\\данные.txt");
    std::ofstream(base/"outside.txt")<<"outside";
    fs::create_symlink(base/"outside.txt",system/"linked.txt");
    CHECK(v.ToHost("C:\\linked.txt").empty() && errno==EACCES);
    CHECK(v.ToHost("C:\\linked.txt",true).empty());
    rt::Vfs onlyC;onlyC.SetRoot(system.string());
    CHECK(onlyC.ToGuest(system.string()+"-sibling/file").empty());
    CHECK(onlyC.ToGuest(std::string(base/"outside.txt")).empty());
    rt::VolumeInfo info;CHECK(v.QueryVolume("C:\\",info));
    struct statvfs physical{};CHECK(statvfs(system.c_str(),&physical)==0);
    CHECK(info.totalBytes==uint64_t(physical.f_frsize)*physical.f_blocks);
    CHECK(info.availableBytes<=info.freeBytes && info.freeBytes<=info.totalBytes);
    CHECK(info.fileSystem!="NTFS" && !info.readOnly && info.totalBytes>0);
    CHECK(v.QueryVolume("R:\\",info) && info.readOnly && info.fileSystem=="ROMFS");
    CHECK(v.DriveType("R:\\")==5 && v.DriveType("S:\\")==2 && v.DriveType("D:\\")==1);
    // Repeat setup preserves user bytes; failed configuration preserves bindings.
    std::ofstream(system/"keep.txt")<<"keep";
    rt::Vfs repeat;CHECK(repeat.ConfigurePersistent(system.string(),sd.string(),rom.string(),error));
    std::string keep;std::ifstream(system/"keep.txt")>>keep;CHECK(keep=="keep");
    const auto before=repeat.Root();
    CHECK(!repeat.ConfigurePersistent(system.string(),std::string(base/"absent"),rom.string(),error));
    CHECK(repeat.Root()==before && repeat.Persistent());
    CHECK(!repeat.ConfigurePersistent(std::string(system/"keep.txt"),sd.string(),rom.string(),error));
    rt::Vfs standalone;standalone.SetRoot(std::string(sd/"Apps/One"));
    CHECK(!standalone.Persistent() && standalone.DriveMask()==(1u<<2));
    CHECK(standalone.ToHost("relative.txt")==std::string(sd/"Apps/One/relative.txt"));
    std::puts("PASSED: persistent C/S/R mounts, per-drive cwd, Unicode/case, readonly/invalid/linked paths, real space and non-destructive repeated setup");
}
