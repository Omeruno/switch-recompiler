// files.cpp — файловая система гостя (см. files.h)

#include "files.h"
#include "process.h"

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <limits.h>
#include <unistd.h>
#include <sys/statvfs.h>
#include <cstring>
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>

namespace rt {

namespace {

bool HostExists(const std::string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0;
}

// Ищет в каталоге dir элемент с именем name без учёта регистра.
std::string FindCaseInsensitive(const std::string& dir,const std::string& name,size_t* budget,bool& exhausted) {
    std::string direct = dir + "/" + name;
    if (HostExists(direct)) return name;
    DIR* d = opendir(dir.c_str());
    if (!d) return "";
    std::string found;
    for(;;) {
        if(budget && !*budget){exhausted=true;break;}
        dirent* e=readdir(d);if(!e)break;
        if(budget)--*budget;
        if(strcasecmp(e->d_name,name.c_str())==0){found=e->d_name;break;}
    }
    closedir(d);
    return found;
}

std::vector<std::string> SplitPath(const std::string& p) {
    std::vector<std::string> parts;
    std::string cur;
    for (char ch : p) {
        if (ch == '\\' || ch == '/') {
            if (!cur.empty()) parts.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(ch);
        }
    }
    if (!cur.empty()) parts.push_back(cur);
    return parts;
}

} // namespace

Vfs& FileSystem() {
    return LocalState<Vfs>();
}

FileTable& Files() {
    static FileTable table;
    return table;
}


namespace {
bool Sep(char c) {return c=='/' || c=='\\';}
int Drive(char c) {c=static_cast<char>(std::toupper(static_cast<unsigned char>(c)));return c>='A'&&c<='Z'?c-'A':-1;}
std::string NativePath(const std::string& path) {
    if(path.empty())return {};
#ifdef __SWITCH__
    return path;
#else
    // Canonicalize the existing prefix, including configured root symlinks.
    char resolved[PATH_MAX];
    if(realpath(path.c_str(),resolved))return resolved;
    if(path[0]=='/')return path;
    char cwd[PATH_MAX];if(!getcwd(cwd,sizeof(cwd)))return {};
    return std::string(cwd)+"/"+path;
#endif
}
bool Within(const std::string& path,const std::string& root) {
    return path==root || (path.size()>root.size() && path.compare(0,root.size(),root)==0 &&
                         (root.back()=='/' || path[root.size()]=='/'));
}
bool Directory(const std::string& path) {struct stat st{};return !path.empty() && stat(path.c_str(),&st)==0 && S_ISDIR(st.st_mode);}
bool EnsureDirectory(const std::string& path) {
    if(Directory(path))return true;
    if(path.empty())return false;
    const auto slash=path.find_last_of('/');
    if(slash!=std::string::npos && slash>0 && path[slash-1]!=':' && !EnsureDirectory(path.substr(0,slash)))return false;
    return mkdir(path.c_str(),0777)==0 || Directory(path);
}
}

Vfs::Vfs() {SetRoot(".");}
void Vfs::SetRoot(const std::string& hostDir) {
    mounts_={};persistent_=false;cwd_="C:\\";
    auto& m=mounts_[2];m.root=NativePath(hostDir.empty()?".":hostDir);
    while(m.root.size()>1 && m.root.back()=='/' && m.root[m.root.size()-2]!=':')m.root.pop_back();
    m.cwd=cwd_;m.type=3;
}
bool Vfs::Mount(char drive,const std::string& hostDir,bool readOnly,uint32_t type) {
    const int d=Drive(drive);const std::string root=NativePath(hostDir);
    if(d<0 || !Directory(root)){errno=ENODEV;return false;}
    auto& m=mounts_[size_t(d)];m.root=root;
    while(m.root.size()>1 && m.root.back()=='/' && m.root[m.root.size()-2]!=':')m.root.pop_back();
    m.cwd=std::string(1,char('A'+d))+":\\";m.readOnly=readOnly;m.type=type;
    return true;
}
bool Vfs::ConfigurePersistent(const std::string& systemRoot,const std::string& sdRoot,
                              const std::string& romRoot,std::string& error) {
    if(systemRoot.empty() || !EnsureDirectory(systemRoot)){error="cannot create system root";return false;}
    Vfs next;
    if(!next.Mount('C',systemRoot) || (!sdRoot.empty()&&!next.Mount('S',sdRoot,false,2)) ||
       (!romRoot.empty()&&!next.Mount('R',romRoot,true,5))) {error="cannot mount filesystem roots";return false;}
    // Discard the default standalone mount's cwd; C is the initial current drive.
    next.cwd_="C:\\";next.persistent_=true;
    for(const char* dir:{"C:\\Programs","C:\\WINDOWS\\system32","C:\\TEMP",
        "C:\\Documents and Settings\\Player\\Desktop",
        "C:\\Documents and Settings\\Player\\My Documents\\My Pictures",
        "C:\\Documents and Settings\\Player\\Application Data",
        "C:\\Documents and Settings\\Player\\Local Settings\\Application Data",
        "C:\\Shared Documents"}) {
        const auto host=next.ToHost(dir,true);
        if(host.empty() || !EnsureDirectory(host)){error="cannot initialize system directories";return false;}
    }
    *this=std::move(next);return true;
}
uint32_t Vfs::DriveMask() const {
    uint32_t mask=0;for(size_t i=0;i<mounts_.size();++i)if(!mounts_[i].root.empty())mask|=1u<<i;
    return mask;
}
std::string Vfs::FullGuestPath(const std::string& guestPath) const {
    if(guestPath.size()>4096 || guestPath.find('\0')!=std::string::npos ||
       (guestPath.size()>1 && Sep(guestPath[0])&&Sep(guestPath[1]))) {errno=EINVAL;return {};}
    std::string p=guestPath;int drive=Drive(cwd_[0]);
    if(p.size()>=2 && p[1]==':') {drive=Drive(p[0]);p=p.substr(2);}
    if(drive<0 || mounts_[size_t(drive)].root.empty()){errno=ENODEV;return {};}
    const auto& mount=mounts_[size_t(drive)];
    std::vector<std::string> parts;
    if(p.empty() || !Sep(p[0]))parts=SplitPath(mount.cwd.substr(2));
    for(const auto& part:SplitPath(p)) {
        if(part==".")continue;
        if(part=="..") {if(!parts.empty())parts.pop_back();continue;}
        if(part.find(':')!=std::string::npos){errno=EINVAL;return {};}
        parts.push_back(part);
    }
    std::string out=std::string(1,char('A'+drive))+":";
    for(const auto& part:parts)out+="\\"+part;
    if(parts.empty())out+="\\";
    return out;
}
std::string Vfs::ToHost(const std::string& guestPath,bool write,size_t* caseBudget) const {
    const auto full=FullGuestPath(guestPath);if(full.empty())return {};
    const auto& mount=mounts_[size_t(Drive(full[0]))];
    if(write && mount.readOnly){errno=EROFS;return {};}
    std::string cur=mount.root;
    for(const auto& part:SplitPath(full.substr(2))) {
        bool exhausted=false;auto name=FindCaseInsensitive(cur,part,caseBudget,exhausted);
        if(exhausted){errno=EOVERFLOW;return {};}
        if(name.empty())name=part;
        if(cur.back()!='/')cur+='/';
        cur+=name;
#ifndef __SWITCH__
        // FAT/ROMFS have no symlinks. Host fixtures reject linked components;
        // this is not a race-proof sandbox against a hostile host process.
        struct stat st{};if(lstat(cur.c_str(),&st)==0 && S_ISLNK(st.st_mode)){errno=EACCES;return {};}
#endif
    }
    return cur;
}
bool Vfs::SetCurrentDirectory(const std::string& path,size_t* caseBudget) {
    const auto full=FullGuestPath(path);if(full.empty())return false;
    const auto host=ToHost(full,false,caseBudget);if(!Directory(host))return false;
    cwd_=full;mounts_[size_t(Drive(full[0]))].cwd=full;return true;
}
std::string Vfs::ToGuest(const std::string& hostPath,size_t* caseBudget) const {
    const auto native=NativePath(hostPath);size_t best=0;int drive=-1;
    for(size_t i=0;i<mounts_.size();++i) {
        const auto& root=mounts_[i].root;
        if(!root.empty() && Within(native,root) && root.size()>best){best=root.size();drive=int(i);}
    }
    if(drive<0){errno=ENODEV;return {};}
    std::string out=std::string(1,char('A'+drive))+":";
    for(const auto& part:SplitPath(native.substr(best)))out+="\\"+part;
    if(out.size()==2)out+="\\";
    // Reject host symlinks outside/inside another mount just like guest lookup.
    return ToHost(out,false,caseBudget).empty()?std::string{}:out;
}
std::string Vfs::ProfileDirectory() const {
    return persistent_?"C:\\Documents and Settings\\Player":"C:\\";
}
uint32_t Vfs::DriveType(const std::string& path) const {
    const auto full=FullGuestPath(path);return full.empty()?1:mounts_[size_t(Drive(full[0]))].type;
}
bool Vfs::QueryVolume(const std::string& path,VolumeInfo& out) const {
    const auto full=FullGuestPath(path);if(full.empty())return false;
    const auto& mount=mounts_[size_t(Drive(full[0]))];
    const auto host=ToHost(full);if(host.empty() || !Directory(host))return false;
    out={};out.readOnly=mount.readOnly;out.label=mount.type==2?"SD":mount.type==5?"ROMFS":"SYSTEM";
    out.fileSystem=mount.type==5?"ROMFS":mount.root.compare(0,6,"sdmc:/")==0?"SDMC":"HOST";
    // ROMFS is immutable; its device need not expose free-space syscalls.
    if(mount.type==5)return true;
    struct statvfs st{};if(statvfs(host.c_str(),&st)!=0)return false;
    const uint64_t block=st.f_frsize?st.f_frsize:st.f_bsize;
    if(!block){errno=EIO;return false;}
    out.totalBytes=block*st.f_blocks;out.freeBytes=block*st.f_bfree;out.availableBytes=block*st.f_bavail;
    // libnx exposes byte granularity; legacy cluster API uses a truthful floor.
    out.bytesPerSector=block>=512?512:1;
    out.sectorsPerCluster=uint32_t(block/out.bytesPerSector);
    if(!out.sectorsPerCluster)out.sectorsPerCluster=1;
    out.maxComponent=st.f_namemax?uint32_t(st.f_namemax):255;
    out.readOnly=out.readOnly || (st.f_flag&ST_RDONLY)!=0;return true;
}

OpenFile::~OpenFile() {
    if (f) std::fclose(f);
    if (deleteOnClose && !hostPath.empty()) std::remove(hostPath.c_str());
}

FileTable::FileTable() = default;
uint32_t FileTable::Add(std::shared_ptr<OpenFile> f) {
    return Handles().Add(std::move(f));
}
std::shared_ptr<OpenFile> FileTable::Get(uint32_t handle) const {
    auto object = Handles().Get(handle);
    if (!object || object->type != ObjectKind::File) return nullptr;
    return std::static_pointer_cast<OpenFile>(object);
}
bool FileTable::Close(uint32_t handle) {
    return Get(handle) && CloseGuestHandle(handle);
}
uint32_t FileTable::Duplicate(uint32_t handle) {
    return Get(handle) ? Handles().Duplicate(handle) : 0;
}

} // namespace rt
