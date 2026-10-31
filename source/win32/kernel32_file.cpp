// kernel32_file.cpp — KERNEL32: файлы, каталоги, пути (поверх rt::Vfs и
// rt::Files). Хендлы консоли (stdin/stdout/stderr) — тоже здесь.

#include <cctype>
#include <array>
#include <cerrno>
#include <map>
#include <vector>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "handle_util.h"
#include "../runtime/files.h"

namespace win32 {

bool HleConsoleWrite(uint32_t handle, const char* data, size_t len); // kernel32_module.cpp
rt::GuestThread* HleThreadFromHandle(uint32_t handle); // kernel32_module.cpp

namespace {

using namespace hle;

constexpr uint32_t INVALID_HANDLE = 0xFFFFFFFFu;
constexpr uint32_t GENERIC_READ = 0x80000000u, GENERIC_WRITE = 0x40000000u, GENERIC_ALL = 0x10000000u;
constexpr uint32_t FILE_ATTRIBUTE_READONLY = 0x01, FILE_ATTRIBUTE_DIRECTORY = 0x10, FILE_ATTRIBUTE_ARCHIVE = 0x20,
                   FILE_ATTRIBUTE_NORMAL = 0x80;
constexpr uint32_t ERROR_HANDLE_EOF = 38, ERROR_FILE_EXISTS = 80, ERROR_DIR_NOT_EMPTY = 145;

uint32_t ErrnoToWin() {
    switch (errno) {
        case ENOENT: return ERROR_FILE_NOT_FOUND;
        case ENODEV: return 15;
        case EINVAL: return 123;
        case EROFS: return 19;
        case ENOTDIR: return ERROR_PATH_NOT_FOUND;
        case EXDEV: return 17; // ERROR_NOT_SAME_DEVICE
        case ENOSPC: return 112; // ERROR_DISK_FULL
        case EBADF: return 6;
        case EIO: return 1117; // ERROR_IO_DEVICE
        case EBUSY: return 32; // ERROR_SHARING_VIOLATION
        case EACCES: case EPERM: return ERROR_ACCESS_DENIED;
        case EEXIST: return ERROR_ALREADY_EXISTS;
        case ENOTEMPTY: return ERROR_DIR_NOT_EMPTY;
        case ENOMEM: return ERROR_NOT_ENOUGH_MEMORY;
        default: return ERROR_ACCESS_DENIED;
    }
}

bool StatHost(const std::string& p, struct stat& st) { return stat(p.c_str(), &st) == 0; }

uint64_t ToFileTime(time_t t) { return static_cast<uint64_t>(t) * 10000000ull + 116444736000000000ull; }
void Write64(cpu::IGuestMemory& m, uint32_t p, uint64_t v) { m.Write32(p, static_cast<uint32_t>(v)); m.Write32(p + 4, static_cast<uint32_t>(v >> 32)); }

uint32_t AttributesOf(const struct stat& st) {
    uint32_t a = S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_ARCHIVE;
    if (!(st.st_mode & S_IWUSR)) a |= FILE_ATTRIBUTE_READONLY;
    return a;
}

std::string ReadPath(cpu::IGuestMemory& m, uint32_t p, bool wide) { return wide ? WideToUtf8(ReadWide(m, p)) : ReadAnsi(m, p); }

bool IsSpecialName(const std::string& n, const char* what) {
    std::string base = n;
    if (base.compare(0, 4, "\\\\.\\") == 0) base = base.substr(4);
    return strcasecmp(base.c_str(), what) == 0;
}

// HANDLE CreateFile(name, access, share, sa, disposition, flags, template)
HostCallOutcome CreateFileCommon(cpu::CpuState& c, cpu::IGuestMemory& m, bool wide) {
    uint32_t handleFlags=0;
    if(!InheritAttribute(m,Arg(c,m,3),handleFlags)) return Ret(INVALID_HANDLE);
    std::string name = ReadPath(m, Arg(c, m, 0), wide);
    uint32_t access = Arg(c, m, 1), disp = Arg(c, m, 4), flags = Arg(c, m, 5);
    access=rt::MapObjectAccess(rt::ObjectKind::File,access);
    if(access & ~rt::AllObjectAccess(rt::ObjectKind::File)) { SetLastError(m,5);return Ret(INVALID_HANDLE); }
    if (IsSpecialName(name, "CONOUT$") || IsSpecialName(name,"CONIN$")) {
        auto console=std::make_shared<rt::OpenFile>();
        console->kind=IsSpecialName(name,"CONIN$") ? rt::OpenFile::ConsoleIn : rt::OpenFile::ConsoleOut;
        console->canRead=(access&1)!=0;console->canWrite=(access&6)!=0;
        return Ret(rt::Handles().Add(console,access,handleFlags));
    }
    auto f = std::make_shared<rt::OpenFile>();
    if (IsSpecialName(name, "NUL")) {
        f->kind = rt::OpenFile::Null;
        f->canRead = f->canWrite = true;
        return Ret(rt::Handles().Add(f,access,handleFlags));
    }
    const bool mutating=(access&6)!=0 || disp==1 || disp==2 || disp==4 || disp==5 || (flags&0x04000000u)!=0;
    std::string host = rt::FileSystem().ToHost(name,mutating);
    if(host.empty()){SetLastError(m,ErrnoToWin());return Ret(INVALID_HANDLE);}
    struct stat st;
    bool exists = StatHost(host, st);
    if (exists && S_ISDIR(st.st_mode)) {
        f->kind = rt::OpenFile::Directory;
        f->hostPath = host;
        return Ret(rt::Handles().Add(f,access,handleFlags));
    }
    bool wantWrite = (access & 6) != 0;
    bool wantRead = (access & 1) != 0;
    switch (disp) {
        case 1: if (exists) { SetLastError(m, ERROR_FILE_EXISTS); return Ret(INVALID_HANDLE); } break; // CREATE_NEW
        case 3: case 5: if (!exists) { SetLastError(m, ERROR_FILE_NOT_FOUND); return Ret(INVALID_HANDLE); } break;
        default: break;
    }
    const char* mode;
    bool truncate = disp == 2 || disp == 5 || (disp == 1);
    if (!wantWrite) mode = "rb";
    else if (truncate || !exists) mode = "w+b";
    else mode = "r+b";
    f->f = std::fopen(host.c_str(), mode);
    if (!f->f && !wantWrite) f->f = nullptr;
    if (!f->f) { SetLastError(m, ErrnoToWin()); return Ret(INVALID_HANDLE); }
    f->hostPath = host;
    f->canRead = wantRead;
    f->canWrite = wantWrite;
    f->deleteOnClose = (flags & 0x04000000u) != 0; // FILE_FLAG_DELETE_ON_CLOSE
    SetLastError(m, (disp == 4 || disp == 2) && exists ? ERROR_ALREADY_EXISTS : ERROR_SUCCESS);
    return Ret(rt::Handles().Add(f,access,handleFlags));
}
HostCallOutcome K32_CreateFileA(cpu::CpuState& c, cpu::IGuestMemory& m) { return CreateFileCommon(c, m, false); }
HostCallOutcome K32_CreateFileW(cpu::CpuState& c, cpu::IGuestMemory& m) { return CreateFileCommon(c, m, true); }

// Synchronous bounded data transfer. No guest-sized temporary allocation and no
// silent OVERLAPPED emulation. CPU page permissions remain a separate D gate.
bool FileBuffer(cpu::IGuestMemory& m,uint32_t buffer,uint32_t count,uint32_t result,uint32_t overlapped) {
    if(result && !m.IsRangeMapped(result,4)){SetLastError(m,998);return false;}
    if(result)m.Write32(result,0);
    if(overlapped){SetLastError(m,50);return false;}
    if(count && (!buffer || uint64_t(buffer)+count>0x100000000ull || !m.IsRangeMapped(buffer,count))){SetLastError(m,998);return false;}
    return true;
}
bool ReadBytes(cpu::IGuestMemory& m,uint32_t h,uint32_t buf,uint32_t n,uint32_t& total) {
    total=0;
    auto f=rt::Files().Get(h);
    if(!f){SetLastError(m,ERROR_INVALID_HANDLE);return false;}
    if(!HandleAccess(m,h,1) || !f->canRead){SetLastError(m,ERROR_ACCESS_DENIED);return false;}
    if(f->kind==rt::OpenFile::Null)return true;
    if(f->kind!=rt::OpenFile::Disk && f->kind!=rt::OpenFile::ConsoleIn){SetLastError(m,6);return false;}
    FILE* stream=f->kind==rt::OpenFile::ConsoleIn?(cpu::g_consoleAvailable?stdin:nullptr):f->f;
    if(!stream){if(f->kind==rt::OpenFile::ConsoleIn)return true;SetLastError(m,6);return false;}
    std::clearerr(stream);std::array<uint8_t,8192> bytes;int error=0;
    while(total<n) {
        const size_t want=std::min<size_t>(bytes.size(),n-total);size_t got=0;errno=0;
        if(f->kind==rt::OpenFile::Disk)got=std::fread(bytes.data(),1,want,stream);
        else {
            while(got<want){const int ch=std::fgetc(stream);if(ch==EOF)break;if(ch=='\n' && got+1<want)bytes[got++]='\r';bytes[got++]=uint8_t(ch);if(ch=='\n')break;}
        }
        if(got)m.LoadBytes(buf+total,bytes.data(),got);
        total+=uint32_t(got);
        if(std::ferror(stream)){error=errno?errno:EIO;break;}
        if(got<want || f->kind==rt::OpenFile::ConsoleIn)break;
    }
    if(error){errno=error;SetLastError(m,ErrnoToWin());return false;}
    return true;
}
bool WriteBytes(cpu::IGuestMemory& m,uint32_t h,uint32_t buf,uint32_t n,uint32_t& total) {
    total=0;
    const auto* entry=rt::Handles().Entry(h);auto f=rt::Files().Get(h);
    if(!entry || !f){SetLastError(m,6);return false;}
    if(!(entry->access&6) || !f->canWrite){SetLastError(m,5);return false;}
    if(f->kind==rt::OpenFile::Null){total=n;return true;}
    if(f->kind!=rt::OpenFile::Disk && f->kind!=rt::OpenFile::ConsoleOut && f->kind!=rt::OpenFile::ConsoleErr){SetLastError(m,6);return false;}
    if(f->kind==rt::OpenFile::Disk) {
        if(!f->f){SetLastError(m,6);return false;}
        if((f->append || !(entry->access&2)) && fseeko(f->f,0,SEEK_END)!=0){SetLastError(m,ErrnoToWin());return false;}
        std::clearerr(f->f);
    }
    std::array<char,8192> bytes;int error=0;
    while(total<n) {
        const size_t want=std::min<size_t>(bytes.size(),n-total);m.ReadBytes(buf+total,reinterpret_cast<uint8_t*>(bytes.data()),want);errno=0;
        size_t put=0;
        if(f->kind==rt::OpenFile::Disk)put=std::fwrite(bytes.data(),1,want,f->f);
        else if(HleConsoleWrite(h,bytes.data(),want))put=want;
        else if(f->f)put=std::fwrite(bytes.data(),1,want,f->f);
        else {error=EBADF;break;}
        total+=uint32_t(put);if(put!=want){error=errno?errno:EIO;break;}
    }
    if(error){errno=error;SetLastError(m,ErrnoToWin());return false;}
    return true;
}

HostCallOutcome Transfer(cpu::CpuState& c,cpu::IGuestMemory& m,bool write,bool legacy=false,bool signedCount=false) {
    const auto h=Arg(c,m,0),buf=Arg(c,m,1),n=Arg(c,m,2),out=legacy?0:Arg(c,m,3);
    if(signedCount && int32_t(n)<0){SetLastError(m,87);return Ret(INVALID_HANDLE);}
    if(!FileBuffer(m,buf,n,out,legacy?0:Arg(c,m,4)))return Ret(legacy?INVALID_HANDLE:0);
    uint32_t total=0;const bool ok=write?WriteBytes(m,h,buf,n,total):ReadBytes(m,h,buf,n,total);
    if(out)m.Write32(out,total);
    return Ret(legacy?(ok?total:INVALID_HANDLE):uint32_t(ok));
}
HostCallOutcome K32_ReadFile(cpu::CpuState& c,cpu::IGuestMemory& m){return Transfer(c,m,false);}
HostCallOutcome K32_WriteFile(cpu::CpuState& c,cpu::IGuestMemory& m){return Transfer(c,m,true);}

// DWORD SetFilePointer(HANDLE, LONG lo, PLONG hi, DWORD method)
HostCallOutcome K32_SetFilePointer(cpu::CpuState& c, cpu::IGuestMemory& m) {
    auto f = rt::Files().Get(Arg(c, m, 0));
    uint32_t hiPtr = Arg(c, m, 2), method = Arg(c, m, 3);
    int64_t off = static_cast<int32_t>(Arg(c, m, 1));
    if (hiPtr) off = static_cast<int64_t>((static_cast<uint64_t>(m.Read32(hiPtr)) << 32) | Arg(c, m, 1));
    if (!f || !f->f) { SetLastError(m, ERROR_INVALID_HANDLE); return Ret(0xFFFFFFFFu); }
    int whence = method == 1 ? SEEK_CUR : method == 2 ? SEEK_END : SEEK_SET;
    if (fseeko(f->f, static_cast<off_t>(off), whence) != 0) { SetLastError(m, 131); return Ret(0xFFFFFFFFu); } // ERROR_NEGATIVE_SEEK
    int64_t pos = static_cast<int64_t>(ftello(f->f));
    if (hiPtr) m.Write32(hiPtr, static_cast<uint32_t>(static_cast<uint64_t>(pos) >> 32));
    SetLastError(m, ERROR_SUCCESS);
    return Ret(static_cast<uint32_t>(pos));
}
// BOOL SetFilePointerEx(HANDLE, LARGE_INTEGER dist, PLARGE_INTEGER newPos, DWORD method)
HostCallOutcome K32_SetFilePointerEx(cpu::CpuState& c, cpu::IGuestMemory& m) {
    auto f = rt::Files().Get(Arg(c, m, 0));
    int64_t off = static_cast<int64_t>(Arg(c, m, 1) | (static_cast<uint64_t>(Arg(c, m, 2)) << 32));
    uint32_t outPtr = Arg(c, m, 3), method = Arg(c, m, 4);
    if (!f || !f->f) { SetLastError(m, ERROR_INVALID_HANDLE); return Ret(0); }
    int whence = method == 1 ? SEEK_CUR : method == 2 ? SEEK_END : SEEK_SET;
    if (fseeko(f->f, static_cast<off_t>(off), whence) != 0) { SetLastError(m, 131); return Ret(0); }
    if (outPtr) Write64(m, outPtr, static_cast<uint64_t>(ftello(f->f)));
    return Ret(1);
}
bool FileSize(rt::OpenFile& file,int64_t& size) {
    if(file.kind!=rt::OpenFile::Disk || !file.f){errno=EBADF;return false;}
    if(file.canWrite && std::fflush(file.f)!=0)return false;
    struct stat st{};if(fstat(fileno(file.f),&st)!=0)return false;
    size=static_cast<int64_t>(st.st_size);return true;
}
HostCallOutcome K32_GetFileSize(cpu::CpuState& c, cpu::IGuestMemory& m) {
    auto f=rt::Files().Get(Arg(c,m,0));const auto high=Arg(c,m,1);
    if(high && !m.IsRangeMapped(high,4)){SetLastError(m,998);return Ret(0xffffffffu);}
    if(!f){SetLastError(m,6);return Ret(0xffffffffu);}
    int64_t size=0;if(!FileSize(*f,size)){SetLastError(m,errno==EBADF?6:ErrnoToWin());return Ret(0xffffffffu);}
    if(high)m.Write32(high,uint32_t(uint64_t(size)>>32));
    SetLastError(m,0);return Ret(uint32_t(size));
}
HostCallOutcome K32_GetFileSizeEx(cpu::CpuState& c, cpu::IGuestMemory& m) {
    auto f=rt::Files().Get(Arg(c,m,0));const auto out=Arg(c,m,1);
    if(!out || !m.IsRangeMapped(out,8)){SetLastError(m,998);return Ret(0);}
    if(!f){SetLastError(m,6);return Ret(0);}
    int64_t size=0;if(!FileSize(*f,size)){SetLastError(m,errno==EBADF?6:ErrnoToWin());return Ret(0);}
    Write64(m,out,uint64_t(size));return Ret(1);
}
bool TruncateFile(cpu::IGuestMemory& m,uint32_t h) {auto f=rt::Files().Get(h);
    if(!f || f->kind!=rt::OpenFile::Disk || !f->f){SetLastError(m,6);return false;}
    if(!HandleAccess(m,h,2) || !f->canWrite){SetLastError(m,5);return false;}
    if(std::fflush(f->f)!=0){SetLastError(m,ErrnoToWin());return false;}
    const auto position=ftello(f->f);
    if(position<0 || ftruncate(fileno(f->f),position)!=0){SetLastError(m,ErrnoToWin());return false;}
    return true;
}
HostCallOutcome K32_SetEndOfFile(cpu::CpuState& c,cpu::IGuestMemory& m){return Ret(TruncateFile(m,Arg(c,m,0)));}
HostCallOutcome K32_FlushFileBuffers(cpu::CpuState& c, cpu::IGuestMemory& m) {
    const auto h=Arg(c,m,0);auto f=rt::Files().Get(h);
    if(!f){SetLastError(m,6);return Ret(0);}
    if(!HandleAccess(m,h,2) || !f->canWrite){SetLastError(m,5);return Ret(0);}
    if(f->kind!=rt::OpenFile::Disk || !f->f){SetLastError(m,50);return Ret(0);}
    if(std::fflush(f->f)!=0){SetLastError(m,ErrnoToWin());return Ret(0);}
    if(fsync(fileno(f->f))!=0){SetLastError(m,errno==EINVAL?50:ErrnoToWin());return Ret(0);}
    return Ret(1);
}
HostCallOutcome K32_GetFileType(cpu::CpuState& c, cpu::IGuestMemory& m) {
    auto f = rt::Files().Get(Arg(c, m, 0));
    if (!f) { SetLastError(m, ERROR_INVALID_HANDLE); return Ret(0); }
    switch (f->kind) {
        case rt::OpenFile::ConsoleIn: case rt::OpenFile::ConsoleOut: case rt::OpenFile::ConsoleErr: case rt::OpenFile::Null:
            return Ret(2); // FILE_TYPE_CHAR
        default: return Ret(1); // FILE_TYPE_DISK
    }
}
HostCallOutcome K32_CloseHandle(cpu::CpuState& c, cpu::IGuestMemory& m) {
    const auto* entry=rt::Handles().Entry(Arg(c,m,0));
    if(entry && (entry->flags&rt::kHandleProtectFromClose)) { SetLastError(m,5);return Ret(0); }
    if (rt::CloseGuestHandle(Arg(c,m,0))) return Ret(1);
    SetLastError(m,ERROR_INVALID_HANDLE);
    return Ret(0);
}

// Legacy Win32 file I/O uses the same live HANDLE objects and checked transfers.
// OFSTRUCT is exactly 136 bytes, with an ANSI path of at most 127 + NUL.
bool LegacyName(cpu::IGuestMemory& m,uint32_t p,std::string& name,size_t limit=4096) {
    name.clear();
    if(!p){SetLastError(m,998);return false;}
    for(size_t n=0;n<limit;++n){
        const uint64_t at=uint64_t(p)+n;
        if(at>0xffffffffu || !m.IsRangeMapped(uint32_t(at),1)){SetLastError(m,998);return false;}
        const auto ch=m.Read8(uint32_t(at));if(!ch)return true;
        name.push_back(char(ch));
    }
    SetLastError(m,206);return false;
}
bool LegacyStyle(cpu::IGuestMemory& m,uint32_t style,bool openFile) {
    const uint32_t allowed=openFile?0xdb73u:0x73u; // CANCEL ignored; VERIFY/SEARCH/PROMPT unsupported
    if(style&~allowed){SetLastError(m,50);return false;}
    if((style&3)==3 || (style&0x70)>0x40){SetLastError(m,87);return false;}
    // Denying another opener requires shared CreateFile/CRT lock enforcement.
    // Until that common mechanism exists, never pretend these locks succeeded.
    if((style&0x70)!=0 && (style&0x70)!=0x40){SetLastError(m,50);return false;}
    if((style&0x300)==0x300 || ((style&0x100) && (style&0x5000)) || ((style&0x200) && (style&0x1000))){SetLastError(m,87);return false;}
    return true;
}
bool LegacyPath(cpu::IGuestMemory& m,const std::string& ansi,std::string& path) {
    if(ansi.empty() || ansi.find_first_of("*?\"<>|")!=std::string::npos){SetLastError(m,123);return false;}
    for(unsigned char c:ansi)if(c<32){SetLastError(m,123);return false;}
    path=rt::FileSystem().FullGuestPath(AnsiPathToUtf8(ansi));
    if(path.empty()){SetLastError(m,ErrnoToWin());return false;}return true;
}
uint32_t LegacyOpen(cpu::IGuestMemory& m,const std::string& path,uint32_t access,bool create) {
    const bool write=create || access!=0;const auto host=rt::FileSystem().ToHost(path,write);
    if(host.empty()){SetLastError(m,ErrnoToWin());return INVALID_HANDLE;}
    struct stat st{};const bool exists=stat(host.c_str(),&st)==0;
    if(exists && !S_ISREG(st.st_mode)){SetLastError(m,5);return INVALID_HANDLE;}
    // CREATE may only truncate regular files; no device, pipe or directory opens.
    if(!exists && errno!=ENOENT){SetLastError(m,ErrnoToWin());return INVALID_HANDLE;}
    if(!exists && !create){SetLastError(m,2);return INVALID_HANDLE;}
    auto file=std::make_shared<rt::OpenFile>();
    file->f=std::fopen(host.c_str(),create?"w+b":write?"r+b":"rb");
    if(!file->f){SetLastError(m,ErrnoToWin());return INVALID_HANDLE;}
    file->hostPath=host;file->canRead=create || access!=1;file->canWrite=write;
    const uint32_t rights=(file->canRead?GENERIC_READ:0)|(write?GENERIC_WRITE:0);
    const auto h=rt::Handles().Add(file,rights);
    if(!h){SetLastError(m,4);return INVALID_HANDLE;}
    return h;
}
HostCallOutcome K32_OpenFile(cpu::CpuState& c,cpu::IGuestMemory& m) {
    const auto ofs=Arg(c,m,1),style=Arg(c,m,2);
    if(!ofs || uint64_t(ofs)+136>0x100000000ull || !m.IsRangeMapped(ofs,136)){SetLastError(m,998);return Ret(INVALID_HANDLE);}
    // Read the input first: OF_REOPEN uses szPathName in this same output buffer.
    std::string name;const bool read=LegacyName(m,(style&0x8000)?ofs+8:Arg(c,m,0),name,(style&0x8000)?128:4096);
    const auto readError=GetLastError(m);
    m.Write8(ofs,136);m.Write16(ofs+2,0);
    auto fail=[&](uint32_t error){SetLastError(m,error);m.Write16(ofs+2,uint16_t(error));return Ret(INVALID_HANDLE);};
    if(!read)return fail(readError);
    if(!LegacyStyle(m,style,true))return fail(GetLastError(m));
    std::string path;if(!LegacyPath(m,name,path))return fail(GetLastError(m));
    const bool create=(style&0x1000)!=0,parse=(style&0x100)!=0;
    if(!create && !parse && name.find_first_of("\\/:")==std::string::npos && !(style&0x8000)){
        std::vector<std::string> directories;
        if(rt::g_process && rt::g_process->MainModule()){
            auto app=rt::FileSystem().ToGuest(rt::g_process->MainModule()->path);
            const auto slash=app.find_last_of('\\');if(slash!=std::string::npos)directories.push_back(app.substr(0,slash));
        }
        directories.push_back(rt::FileSystem().CurrentDirectory());
        directories.insert(directories.end(),{"C:\\WINDOWS\\system32","C:\\WINDOWS\\system","C:\\WINDOWS"});
        const auto env=AnsiPathToUtf8(GuestEnvironmentValue("PATH"));
        if(env.size()>32768)return fail(206);
        for(size_t start=0;start<env.size();){const auto end=env.find(';',start);directories.push_back(env.substr(start,end==std::string::npos?end:end-start));if(end==std::string::npos)break;start=end+1;}
        for(const auto& directory:directories){
            const auto candidate=rt::FileSystem().FullGuestPath(directory+"\\"+AnsiPathToUtf8(name));
            rt::FileProperties properties;
            if(!candidate.empty() && rt::FileSystem().QueryProperties(candidate,properties) && properties.kind==rt::FileProperties::Kind::File){path=candidate;break;}
        }
    }
    std::string encoded;if(!Utf8PathToAnsi(path,encoded))return fail(1113);
    if(encoded.size()>=128)return fail(206);
    m.Write8(ofs+1,rt::FileSystem().DriveType(path)!=2);m.Write32(ofs+4,0);
    WriteAnsi(m,ofs+8,encoded,128);
    if(parse)return Ret(0);
    if(style&0x200){if(!rt::FileSystem().DeleteFile(path))return fail(ErrnoToWin());return Ret(1);}
    const auto h=LegacyOpen(m,path,style&3,create);if(h==INVALID_HANDLE)return fail(GetLastError(m));
    if(style&0x4000){rt::CloseGuestHandle(h);return Ret(1);}
    return Ret(h);
}
HostCallOutcome K32_lopen(cpu::CpuState& c,cpu::IGuestMemory& m) {
    const auto style=Arg(c,m,1);if(!LegacyStyle(m,style,false))return Ret(INVALID_HANDLE);
    std::string name,path;if(!LegacyName(m,Arg(c,m,0),name) || !LegacyPath(m,name,path))return Ret(INVALID_HANDLE);
    return Ret(LegacyOpen(m,path,style&3,false));
}
HostCallOutcome K32_lcreat(cpu::CpuState& c,cpu::IGuestMemory& m) {
    if(Arg(c,m,1)){SetLastError(m,50);return Ret(INVALID_HANDLE);}
    std::string name,path;if(!LegacyName(m,Arg(c,m,0),name) || !LegacyPath(m,name,path))return Ret(INVALID_HANDLE);
    return Ret(LegacyOpen(m,path,2,true));
}
HostCallOutcome K32_lclose(cpu::CpuState& c,cpu::IGuestMemory& m) {
    const auto h=Arg(c,m,0);
    if(!rt::Files().Get(h)){SetLastError(m,6);return Ret(INVALID_HANDLE);}
    if(!rt::CloseGuestHandle(h)){SetLastError(m,5);return Ret(INVALID_HANDLE);}return Ret(0);
}
HostCallOutcome K32_lread(cpu::CpuState& c,cpu::IGuestMemory& m){return Transfer(c,m,false,true);}
HostCallOutcome K32_hread(cpu::CpuState& c,cpu::IGuestMemory& m){return Transfer(c,m,false,true,true);}
HostCallOutcome LegacyWrite(cpu::CpuState& c,cpu::IGuestMemory& m,bool signedCount) {
    // Historical zero-byte writes set EOF at the current cursor, unlike WriteFile.
    if(!Arg(c,m,2))return Ret(TruncateFile(m,Arg(c,m,0))?0:INVALID_HANDLE);
    return Transfer(c,m,true,true,signedCount);
}
HostCallOutcome K32_lwrite(cpu::CpuState& c,cpu::IGuestMemory& m){return LegacyWrite(c,m,false);}
HostCallOutcome K32_hwrite(cpu::CpuState& c,cpu::IGuestMemory& m){return LegacyWrite(c,m,true);}
HostCallOutcome K32_llseek(cpu::CpuState& c,cpu::IGuestMemory& m) {
    const auto h=Arg(c,m,0),method=Arg(c,m,2);auto file=rt::Files().Get(h);
    if(!file || !file->f || file->kind!=rt::OpenFile::Disk){SetLastError(m,6);return Ret(INVALID_HANDLE);}
    if(method>2){SetLastError(m,87);return Ret(INVALID_HANDLE);}
    int64_t base=0;
    if(method==1){base=ftello(file->f);if(base<0){SetLastError(m,ErrnoToWin());return Ret(INVALID_HANDLE);}}
    if(method==2 && !FileSize(*file,base)){SetLastError(m,ErrnoToWin());return Ret(INVALID_HANDLE);}
    const int64_t delta=int32_t(Arg(c,m,1));
    if(base<0 || base>0x7fffffff-delta || base+delta<0){SetLastError(m,131);return Ret(INVALID_HANDLE);}
    const int64_t target=base+delta;
    if(fseeko(file->f,off_t(target),SEEK_SET)!=0){SetLastError(m,ErrnoToWin());return Ret(INVALID_HANDLE);}
    return Ret(uint32_t(target));
}
// Current-process duplication; the object survives until its last reference.
// Token/DACL checks and cross-process handles remain unsupported.
HostCallOutcome K32_DuplicateHandle(cpu::CpuState& c, cpu::IGuestMemory& m) {
    const uint32_t sourceProcess=Arg(c,m,0), src=Arg(c,m,1);
    const uint32_t targetProcess=Arg(c,m,2), dst=Arg(c,m,3);
    const uint32_t options=Arg(c,m,6);
    if (sourceProcess!=0xffffffffu) {
        SetLastError(m,ERROR_INVALID_HANDLE); return Ret(0);
    }
    auto object=rt::Handles().Get(src);
    const auto* sourceEntry=rt::Handles().Entry(src);
    const uint32_t sourceAccess=sourceEntry ? sourceEntry->access : rt::AllObjectAccess(rt::ObjectKind::Thread);
    if (src==0xfffffffeu) {
        if (auto* thread=HleThreadFromHandle(src)) {
            // All handles to one thread share its canonical object identity.
            for (const auto& candidate : rt::Handles().Objects())
                if (candidate->type==rt::ObjectKind::Thread &&
                    static_cast<rt::ThreadObject&>(*candidate).threadId==thread->id)
                    object=candidate;
            if (!object) object=std::make_shared<rt::ThreadObject>(thread->id);
        }
    }
    // Pin the object before DUPLICATE_CLOSE_SOURCE, even on error paths.
    if ((options & 1u) && src!=0xfffffffeu) {
        // A protected source remains open. No duplicate is published on error.
        if(sourceEntry && (sourceEntry->flags&rt::kHandleProtectFromClose)) { SetLastError(m,5);return Ret(0); }
        rt::CloseGuestHandle(src);
    }
    if (!object) { SetLastError(m,ERROR_INVALID_HANDLE); return Ret(0); }
    if ((options & ~3u) || (dst && targetProcess && !m.IsRangeMapped(dst,4))) {
        SetLastError(m,ERROR_INVALID_PARAMETER); return Ret(0);
    }
    if (targetProcess==0 && (options & 1u)) return Ret(1);
    if (targetProcess!=0xffffffffu) {
        SetLastError(m,ERROR_INVALID_HANDLE); return Ret(0);
    }
    const uint32_t access=(options&2) ? sourceAccess : rt::MapObjectAccess(object->type,Arg(c,m,4));
    if((access & ~rt::AllObjectAccess(object->type)) ||
       (object->type==rt::ObjectKind::File && (access & ~sourceAccess))) {
        SetLastError(m,5);return Ret(0);
    }
    const uint32_t duplicated=rt::Handles().Add(object,access,Arg(c,m,5) ? rt::kHandleInherit : 0);
    if (!duplicated) { SetLastError(m,ERROR_NOT_ENOUGH_MEMORY); return Ret(0); }
    if (dst) m.Write32(dst,duplicated); // NULL output is legacy Win32 behavior.
    return Ret(1);
}
HostCallOutcome K32_GetFileTime(cpu::CpuState& c, cpu::IGuestMemory& m) {
    auto f = rt::Files().Get(Arg(c, m, 0));
    if (!f || !f->f) { SetLastError(m, ERROR_INVALID_HANDLE); return Ret(0); }
    struct stat st;
    fstat(fileno(f->f), &st);
    for (int i = 1; i <= 3; ++i) if (Arg(c, m, i)) Write64(m, Arg(c, m, i), ToFileTime(st.st_mtime));
    return Ret(1);
}
HostCallOutcome K32_SetFileTime(cpu::CpuState&, cpu::IGuestMemory&) { return Ret(1); }

// --- Операции с именами ---
HostCallOutcome PathOp(cpu::IGuestMemory& m, int r) {
    if (r != 0) { SetLastError(m, ErrnoToWin()); return Ret(0); }
    return Ret(1);
}
enum class PathMutation {Delete,Mkdir,Rmdir};
HostCallOutcome MutatePath(cpu::CpuState& c,cpu::IGuestMemory& m,bool wide,PathMutation op) {
    const auto path=ReadPath(m,Arg(c,m,0),wide);const auto& fs=rt::FileSystem();
    const bool ok=op==PathMutation::Delete?fs.DeleteFile(path):
                  op==PathMutation::Mkdir?fs.CreateDirectory(path):fs.RemoveDirectory(path);
    return PathOp(m,ok?0:-1);
}
HostCallOutcome K32_DeleteFileA(cpu::CpuState& c,cpu::IGuestMemory& m){return MutatePath(c,m,false,PathMutation::Delete);}
HostCallOutcome K32_DeleteFileW(cpu::CpuState& c,cpu::IGuestMemory& m){return MutatePath(c,m,true,PathMutation::Delete);}
HostCallOutcome MoveCommon(cpu::CpuState& c, cpu::IGuestMemory& m, bool wide, bool replace) {
    return PathOp(m,rt::FileSystem().Move(ReadPath(m,Arg(c,m,0),wide),
                                        ReadPath(m,Arg(c,m,1),wide),replace)?0:-1);
}
HostCallOutcome K32_MoveFileA(cpu::CpuState& c, cpu::IGuestMemory& m) { return MoveCommon(c, m, false, false); }
HostCallOutcome K32_MoveFileW(cpu::CpuState& c, cpu::IGuestMemory& m) { return MoveCommon(c, m, true, false); }
HostCallOutcome K32_MoveFileExA(cpu::CpuState& c, cpu::IGuestMemory& m) { return MoveCommon(c, m, false, (Arg(c, m, 2) & 1) != 0); }
HostCallOutcome K32_MoveFileExW(cpu::CpuState& c, cpu::IGuestMemory& m) { return MoveCommon(c, m, true, (Arg(c, m, 2) & 1) != 0); }
HostCallOutcome CopyCommon(cpu::CpuState& c, cpu::IGuestMemory& m, bool wide) {
    if(rt::FileSystem().CopyFile(ReadPath(m,Arg(c,m,0),wide),ReadPath(m,Arg(c,m,1),wide),Arg(c,m,2)!=0))return Ret(1);
    SetLastError(m,errno==EEXIST?ERROR_FILE_EXISTS:ErrnoToWin());return Ret(0);
}
HostCallOutcome K32_CopyFileA(cpu::CpuState& c, cpu::IGuestMemory& m) { return CopyCommon(c, m, false); }
HostCallOutcome K32_CopyFileW(cpu::CpuState& c, cpu::IGuestMemory& m) { return CopyCommon(c, m, true); }
HostCallOutcome K32_CreateDirectoryA(cpu::CpuState& c,cpu::IGuestMemory& m){return MutatePath(c,m,false,PathMutation::Mkdir);}
HostCallOutcome K32_CreateDirectoryW(cpu::CpuState& c,cpu::IGuestMemory& m){return MutatePath(c,m,true,PathMutation::Mkdir);}
HostCallOutcome K32_RemoveDirectoryA(cpu::CpuState& c,cpu::IGuestMemory& m){return MutatePath(c,m,false,PathMutation::Rmdir);}
HostCallOutcome K32_RemoveDirectoryW(cpu::CpuState& c,cpu::IGuestMemory& m){return MutatePath(c,m,true,PathMutation::Rmdir);}

// Attribute queries require a nonempty, terminated guest path. In particular,
// an empty name must never be normalized into the VFS current directory.
bool AttributeName(cpu::IGuestMemory& m,uint32_t p,bool wide,std::string& name) {
    if(!p){SetLastError(m,87);return false;}
    std::string ansi;std::u16string utf16;
    const uint32_t unit=wide?2:1;
    for(uint32_t n=0;n<32768;++n) {
        const uint64_t at=uint64_t(p)+uint64_t(n)*unit;
        if(at+unit>0x100000000ull || !m.IsRangeMapped(uint32_t(at),unit)) {
            SetLastError(m,998);return false;
        }
        const uint16_t ch=wide?m.Read16(uint32_t(at)):m.Read8(uint32_t(at));
        if(!ch) {
            if(!n){SetLastError(m,ERROR_PATH_NOT_FOUND);return false;}
            name=wide?WideToUtf8(utf16):AnsiPathToUtf8(ansi);return true;
        }
        if(wide)utf16.push_back(char16_t(ch));else ansi.push_back(char(ch));
    }
    SetLastError(m,206);return false;
}
bool AttributeStat(cpu::IGuestMemory& m,uint32_t p,bool wide,struct stat& st) {
    std::string name;
    if(!AttributeName(m,p,wide,name))return false;
    const auto host=rt::FileSystem().ToHost(name);
    if(host.empty() || !StatHost(host,st)){SetLastError(m,ErrnoToWin());return false;}
    return true;
}
HostCallOutcome AttrCommon(cpu::CpuState& c, cpu::IGuestMemory& m, bool wide) {
    struct stat st;
    if(!AttributeStat(m,Arg(c,m,0),wide,st))return Ret(0xFFFFFFFFu);
    return Ret(AttributesOf(st));
}
HostCallOutcome K32_GetFileAttributesA(cpu::CpuState& c, cpu::IGuestMemory& m) { return AttrCommon(c, m, false); }
HostCallOutcome K32_GetFileAttributesW(cpu::CpuState& c, cpu::IGuestMemory& m) { return AttrCommon(c, m, true); }
// BOOL GetFileAttributesEx(name, level, WIN32_FILE_ATTRIBUTE_DATA*)
HostCallOutcome AttrExCommon(cpu::CpuState& c, cpu::IGuestMemory& m, bool wide) {
    if(Arg(c,m,1)!=0){SetLastError(m,87);return Ret(0);}
    const uint32_t p=Arg(c,m,2);
    if(!p || uint64_t(p)+36>0x100000000ull || !m.IsRangeMapped(p,36)) {
        SetLastError(m,998);return Ret(0);
    }
    struct stat st;
    if(!AttributeStat(m,Arg(c,m,0),wide,st))return Ret(0);
    m.Write32(p, AttributesOf(st));
    Write64(m, p + 4, ToFileTime(st.st_mtime));
    Write64(m, p + 12, ToFileTime(st.st_atime));
    Write64(m, p + 20, ToFileTime(st.st_mtime));
    uint64_t size = S_ISDIR(st.st_mode) ? 0 : static_cast<uint64_t>(st.st_size);
    m.Write32(p + 28, static_cast<uint32_t>(size >> 32));
    m.Write32(p + 32, static_cast<uint32_t>(size));
    return Ret(1);
}
HostCallOutcome K32_GetFileAttributesExA(cpu::CpuState& c, cpu::IGuestMemory& m) { return AttrExCommon(c, m, false); }
HostCallOutcome K32_GetFileAttributesExW(cpu::CpuState& c, cpu::IGuestMemory& m) { return AttrExCommon(c, m, true); }
HostCallOutcome K32_SetFileAttributes(cpu::CpuState&, cpu::IGuestMemory&) { return Ret(1); }

// --- Поиск файлов по маске ---
bool WildMatch(const char* pat, const char* s) {
    if (!*pat) return !*s;
    if (*pat == '*') {
        for (const char* t = s;; ++t) { if (WildMatch(pat + 1, t)) return true; if (!*t) return false; }
    }
    if (!*s) return std::strcmp(pat, ".") == 0 || std::strcmp(pat, "*") == 0;
    if (*pat == '?' || std::tolower(static_cast<unsigned char>(*pat)) == std::tolower(static_cast<unsigned char>(*s)))
        return WildMatch(pat + 1, s + 1);
    return false;
}
struct FindState {
    std::vector<std::string> names;
    std::string dir;
    size_t next = 0;
};
struct FindTable {std::map<uint32_t,FindState> finds;uint32_t next=0x00F10000u;};
std::map<uint32_t,FindState>& Finds() {return rt::LocalState<FindTable>().finds;}

void FillFindData(cpu::IGuestMemory& m, uint32_t p, const std::string& dir, const std::string& name, bool wide) {
    struct stat st{};
    StatHost(dir + "/" + name, st);
    m.Write32(p, AttributesOf(st));
    Write64(m, p + 4, ToFileTime(st.st_mtime));
    Write64(m, p + 12, ToFileTime(st.st_atime));
    Write64(m, p + 20, ToFileTime(st.st_mtime));
    uint64_t size = S_ISDIR(st.st_mode) ? 0 : static_cast<uint64_t>(st.st_size);
    m.Write32(p + 28, static_cast<uint32_t>(size >> 32));
    m.Write32(p + 32, static_cast<uint32_t>(size));
    m.Write32(p + 36, 0);
    m.Write32(p + 40, 0);
    if (wide) { WriteWide(m, p + 44, Utf8PathToWide(name), 260); WriteWide(m, p + 44 + 520, u"", 14); }
    else { WriteAnsi(m, p + 44, name, 260); WriteAnsi(m, p + 44 + 260, "", 14); }
}
HostCallOutcome FindFirstCommon(cpu::CpuState& c, cpu::IGuestMemory& m, bool wide) {
    std::string pattern = ReadPath(m, Arg(c, m, 0), wide);
    uint32_t data = Arg(c, m, 1);
    std::string full = rt::FileSystem().FullGuestPath(pattern);
    if(full.empty()){SetLastError(m,ErrnoToWin());return Ret(INVALID_HANDLE);}
    size_t slash = full.find_last_of('\\');
    std::string dirGuest = full.substr(0, slash), mask = full.substr(slash + 1);
    std::string dirHost = rt::FileSystem().ToHost(dirGuest.size() == 2 ? dirGuest + "\\" : dirGuest);
    FindState st;
    st.dir = dirHost;
    DIR* d = opendir(dirHost.c_str());
    if (!d) { SetLastError(m, ERROR_PATH_NOT_FOUND); return Ret(INVALID_HANDLE); }
    while (dirent* e = readdir(d)) {
        if (WildMatch(mask.c_str(), e->d_name)) st.names.push_back(e->d_name);
    }
    closedir(d);
    if (st.names.empty()) { SetLastError(m, ERROR_FILE_NOT_FOUND); return Ret(INVALID_HANDLE); }
    FillFindData(m, data, st.dir, st.names[0], wide);
    st.next = 1;
    uint32_t h = (rt::LocalState<FindTable>().next += 4);
    Finds()[h] = st;
    return Ret(h);
}
HostCallOutcome FindNextCommon(cpu::CpuState& c, cpu::IGuestMemory& m, bool wide) {
    auto it = Finds().find(Arg(c, m, 0));
    if (it == Finds().end()) { SetLastError(m, ERROR_INVALID_HANDLE); return Ret(0); }
    FindState& st = it->second;
    if (st.next >= st.names.size()) { SetLastError(m, ERROR_NO_MORE_FILES); return Ret(0); }
    FillFindData(m, Arg(c, m, 1), st.dir, st.names[st.next++], wide);
    return Ret(1);
}
HostCallOutcome K32_FindFirstFileA(cpu::CpuState& c, cpu::IGuestMemory& m) { return FindFirstCommon(c, m, false); }
HostCallOutcome K32_FindFirstFileW(cpu::CpuState& c, cpu::IGuestMemory& m) { return FindFirstCommon(c, m, true); }
HostCallOutcome K32_FindNextFileA(cpu::CpuState& c, cpu::IGuestMemory& m) { return FindNextCommon(c, m, false); }
HostCallOutcome K32_FindNextFileW(cpu::CpuState& c, cpu::IGuestMemory& m) { return FindNextCommon(c, m, true); }
HostCallOutcome K32_FindClose(cpu::CpuState& c, cpu::IGuestMemory& m) { Finds().erase(Arg(c, m, 0)); return Ret(1); }

// --- Пути и каталоги ---
// DWORD GetFullPathName(LPCSTR name, DWORD n, LPSTR buf, LPSTR* filePart)
HostCallOutcome FullPathCommon(cpu::CpuState& c,cpu::IGuestMemory& m,bool wide) {
    const auto full=rt::FileSystem().FullGuestPath(ReadPath(m,Arg(c,m,0),wide));
    if(full.empty()){SetLastError(m,ErrnoToWin());return Ret(0);}
    const auto w=Utf8PathToWide(full);const auto count=uint32_t(wide?w.size():full.size());
    const auto cap=Arg(c,m,1),buf=Arg(c,m,2),part=Arg(c,m,3);
    if(cap<=count)return Ret(count+1);
    if(wide)WriteWide(m,buf,w,cap);else WriteAnsi(m,buf,full,cap);
    if(part) {
        const auto slash=wide?w.find_last_of(u'\\'):full.find_last_of('\\');
        m.Write32(part,buf+uint32_t((slash+1)*(wide?2:1)));
    }
    return Ret(count);
}
HostCallOutcome K32_GetFullPathNameA(cpu::CpuState& c, cpu::IGuestMemory& m) { return FullPathCommon(c, m, false); }
HostCallOutcome K32_GetFullPathNameW(cpu::CpuState& c, cpu::IGuestMemory& m) { return FullPathCommon(c, m, true); }
HostCallOutcome CurDirCommon(cpu::CpuState& c,cpu::IGuestMemory& m,bool wide) {
    const auto d=rt::FileSystem().CurrentDirectory();const auto w=Utf8PathToWide(d);
    const auto count=uint32_t(wide?w.size():d.size()),cap=Arg(c,m,0),buf=Arg(c,m,1);
    if(cap<=count)return Ret(count+1);
    if(wide)WriteWide(m,buf,w,cap);else WriteAnsi(m,buf,d,cap);
    return Ret(count);
}
HostCallOutcome K32_GetCurrentDirectoryA(cpu::CpuState& c, cpu::IGuestMemory& m) { return CurDirCommon(c, m, false); }
HostCallOutcome K32_GetCurrentDirectoryW(cpu::CpuState& c, cpu::IGuestMemory& m) { return CurDirCommon(c, m, true); }
HostCallOutcome K32_SetCurrentDirectoryA(cpu::CpuState& c, cpu::IGuestMemory& m) {
    if (!rt::FileSystem().SetCurrentDirectory(ReadAnsi(m, Arg(c, m, 0)))) { SetLastError(m, ERROR_PATH_NOT_FOUND); return Ret(0); }
    return Ret(1);
}
HostCallOutcome K32_SetCurrentDirectoryW(cpu::CpuState& c, cpu::IGuestMemory& m) {
    if (!rt::FileSystem().SetCurrentDirectory(WideToUtf8(ReadWide(m, Arg(c, m, 0))))) { SetLastError(m, ERROR_PATH_NOT_FOUND); return Ret(0); }
    return Ret(1);
}
HostCallOutcome TempPathCommon(cpu::CpuState& c, cpu::IGuestMemory& m, bool wide) {
    // C:\TEMP\ внутри корня гостя; создаём каталог при первом обращении
    std::string host = rt::FileSystem().ToHost("C:\\TEMP",true);
    if(host.empty()){SetLastError(m,ErrnoToWin());return Ret(0);}
    mkdir(host.c_str(), 0777);
    std::string p = "C:\\TEMP\\";
    uint32_t cap = Arg(c, m, 0), buf = Arg(c, m, 1);
    if (cap <= p.size()) return Ret(static_cast<uint32_t>(p.size() + 1));
    if (wide) WriteWide(m, buf, AnsiToWide(p), cap); else WriteAnsi(m, buf, p, cap);
    return Ret(static_cast<uint32_t>(p.size()));
}
HostCallOutcome K32_GetTempPathA(cpu::CpuState& c, cpu::IGuestMemory& m) { return TempPathCommon(c, m, false); }
HostCallOutcome K32_GetTempPathW(cpu::CpuState& c, cpu::IGuestMemory& m) { return TempPathCommon(c, m, true); }
// UINT GetTempFileNameA(LPCSTR dir, LPCSTR prefix, UINT unique, LPSTR out)
HostCallOutcome K32_GetTempFileNameA(cpu::CpuState& c, cpu::IGuestMemory& m) {
    static uint32_t counter = 1;
    std::string dir = ReadAnsi(m, Arg(c, m, 0)), pre = ReadAnsi(m, Arg(c, m, 1)).substr(0, 3);
    uint32_t unique = Arg(c, m, 2);
    if (!dir.empty() && dir.back() != '\\') dir += "\\";
    for (;;) {
        uint32_t u = unique ? unique : counter++;
        char name[16];
        std::snprintf(name, sizeof(name), "%X.TMP", u & 0xFFFF);
        std::string full = dir + pre + name;
        struct stat st;
        std::string host = rt::FileSystem().ToHost(full,!unique);
        if(host.empty()){SetLastError(m,ErrnoToWin());return Ret(0);}
        if (unique || !StatHost(host, st)) {
            if (!unique) { std::FILE* f = std::fopen(host.c_str(), "wb"); if (!f){SetLastError(m,ErrnoToWin());return Ret(0);} std::fclose(f); }
            WriteAnsi(m, Arg(c, m, 3), full, 260);
            return Ret(u & 0xFFFF);
        }
    }
}
// DWORD GetLongPathName/GetShortPathName(src, dst, cch): путь не меняем
HostCallOutcome SamePathA(cpu::CpuState& c, cpu::IGuestMemory& m) {
    std::string p = ReadAnsi(m, Arg(c, m, 0));
    uint32_t cap = Arg(c, m, 2);
    if (cap <= p.size()) return Ret(static_cast<uint32_t>(p.size() + 1));
    return Ret(WriteAnsi(m, Arg(c, m, 1), p, cap));
}
HostCallOutcome SamePathW(cpu::CpuState& c, cpu::IGuestMemory& m) {
    std::u16string p = ReadWide(m, Arg(c, m, 0));
    uint32_t cap = Arg(c, m, 2);
    if (cap <= p.size()) return Ret(static_cast<uint32_t>(p.size() + 1));
    return Ret(WriteWide(m, Arg(c, m, 1), p, cap));
}
HostCallOutcome K32_SetHandleCount(cpu::CpuState& c, cpu::IGuestMemory& m) { return Ret(Arg(c, m, 0)); }

const HostFunctionEntry kEntries[] = {
    {"KERNEL32.dll", "OpenFile", 12, CallConv::Stdcall, &K32_OpenFile},
    {"KERNEL32.dll", "_lopen", 8, CallConv::Stdcall, &K32_lopen},
    {"KERNEL32.dll", "_lcreat", 8, CallConv::Stdcall, &K32_lcreat},
    {"KERNEL32.dll", "_lclose", 4, CallConv::Stdcall, &K32_lclose},
    {"KERNEL32.dll", "_lread", 12, CallConv::Stdcall, &K32_lread},
    {"KERNEL32.dll", "_lwrite", 12, CallConv::Stdcall, &K32_lwrite},
    {"KERNEL32.dll", "_hread", 12, CallConv::Stdcall, &K32_hread},
    {"KERNEL32.dll", "_hwrite", 12, CallConv::Stdcall, &K32_hwrite},
    {"KERNEL32.dll", "_llseek", 12, CallConv::Stdcall, &K32_llseek},
    {"KERNEL32.dll", "CreateFileA", 28, CallConv::Stdcall, &K32_CreateFileA},
    {"KERNEL32.dll", "CreateFileW", 28, CallConv::Stdcall, &K32_CreateFileW},
    {"KERNEL32.dll", "ReadFile", 20, CallConv::Stdcall, &K32_ReadFile},
    {"KERNEL32.dll", "WriteFile", 20, CallConv::Stdcall, &K32_WriteFile},
    {"KERNEL32.dll", "SetFilePointer", 16, CallConv::Stdcall, &K32_SetFilePointer},
    {"KERNEL32.dll", "SetFilePointerEx", 20, CallConv::Stdcall, &K32_SetFilePointerEx},
    {"KERNEL32.dll", "GetFileSize", 8, CallConv::Stdcall, &K32_GetFileSize},
    {"KERNEL32.dll", "GetFileSizeEx", 8, CallConv::Stdcall, &K32_GetFileSizeEx},
    {"KERNEL32.dll", "SetEndOfFile", 4, CallConv::Stdcall, &K32_SetEndOfFile},
    {"KERNEL32.dll", "FlushFileBuffers", 4, CallConv::Stdcall, &K32_FlushFileBuffers},
    {"KERNEL32.dll", "GetFileType", 4, CallConv::Stdcall, &K32_GetFileType},
    {"KERNEL32.dll", "CloseHandle", 4, CallConv::Stdcall, &K32_CloseHandle},
    {"KERNEL32.dll", "DuplicateHandle", 28, CallConv::Stdcall, &K32_DuplicateHandle},
    {"KERNEL32.dll", "GetFileTime", 16, CallConv::Stdcall, &K32_GetFileTime},
    {"KERNEL32.dll", "SetFileTime", 16, CallConv::Stdcall, &K32_SetFileTime},
    {"KERNEL32.dll", "DeleteFileA", 4, CallConv::Stdcall, &K32_DeleteFileA},
    {"KERNEL32.dll", "DeleteFileW", 4, CallConv::Stdcall, &K32_DeleteFileW},
    {"KERNEL32.dll", "MoveFileA", 8, CallConv::Stdcall, &K32_MoveFileA},
    {"KERNEL32.dll", "MoveFileW", 8, CallConv::Stdcall, &K32_MoveFileW},
    {"KERNEL32.dll", "MoveFileExA", 12, CallConv::Stdcall, &K32_MoveFileExA},
    {"KERNEL32.dll", "MoveFileExW", 12, CallConv::Stdcall, &K32_MoveFileExW},
    {"KERNEL32.dll", "CopyFileA", 12, CallConv::Stdcall, &K32_CopyFileA},
    {"KERNEL32.dll", "CopyFileW", 12, CallConv::Stdcall, &K32_CopyFileW},
    {"KERNEL32.dll", "CreateDirectoryA", 8, CallConv::Stdcall, &K32_CreateDirectoryA},
    {"KERNEL32.dll", "CreateDirectoryW", 8, CallConv::Stdcall, &K32_CreateDirectoryW},
    {"KERNEL32.dll", "RemoveDirectoryA", 4, CallConv::Stdcall, &K32_RemoveDirectoryA},
    {"KERNEL32.dll", "RemoveDirectoryW", 4, CallConv::Stdcall, &K32_RemoveDirectoryW},
    {"KERNEL32.dll", "GetFileAttributesA", 4, CallConv::Stdcall, &K32_GetFileAttributesA},
    {"KERNEL32.dll", "GetFileAttributesW", 4, CallConv::Stdcall, &K32_GetFileAttributesW},
    {"KERNEL32.dll", "GetFileAttributesExA", 12, CallConv::Stdcall, &K32_GetFileAttributesExA},
    {"KERNEL32.dll", "GetFileAttributesExW", 12, CallConv::Stdcall, &K32_GetFileAttributesExW},
    {"KERNEL32.dll", "SetFileAttributesA", 8, CallConv::Stdcall, &K32_SetFileAttributes},
    {"KERNEL32.dll", "SetFileAttributesW", 8, CallConv::Stdcall, &K32_SetFileAttributes},
    {"KERNEL32.dll", "FindFirstFileA", 8, CallConv::Stdcall, &K32_FindFirstFileA},
    {"KERNEL32.dll", "FindFirstFileW", 8, CallConv::Stdcall, &K32_FindFirstFileW},
    {"KERNEL32.dll", "FindNextFileA", 8, CallConv::Stdcall, &K32_FindNextFileA},
    {"KERNEL32.dll", "FindNextFileW", 8, CallConv::Stdcall, &K32_FindNextFileW},
    {"KERNEL32.dll", "FindClose", 4, CallConv::Stdcall, &K32_FindClose},
    {"KERNEL32.dll", "GetFullPathNameA", 16, CallConv::Stdcall, &K32_GetFullPathNameA},
    {"KERNEL32.dll", "GetFullPathNameW", 16, CallConv::Stdcall, &K32_GetFullPathNameW},
    {"KERNEL32.dll", "GetCurrentDirectoryA", 8, CallConv::Stdcall, &K32_GetCurrentDirectoryA},
    {"KERNEL32.dll", "GetCurrentDirectoryW", 8, CallConv::Stdcall, &K32_GetCurrentDirectoryW},
    {"KERNEL32.dll", "SetCurrentDirectoryA", 4, CallConv::Stdcall, &K32_SetCurrentDirectoryA},
    {"KERNEL32.dll", "SetCurrentDirectoryW", 4, CallConv::Stdcall, &K32_SetCurrentDirectoryW},
    {"KERNEL32.dll", "GetTempPathA", 8, CallConv::Stdcall, &K32_GetTempPathA},
    {"KERNEL32.dll", "GetTempPathW", 8, CallConv::Stdcall, &K32_GetTempPathW},
    {"KERNEL32.dll", "GetTempFileNameA", 16, CallConv::Stdcall, &K32_GetTempFileNameA},
    {"KERNEL32.dll", "GetLongPathNameA", 12, CallConv::Stdcall, &SamePathA},
    {"KERNEL32.dll", "GetShortPathNameA", 12, CallConv::Stdcall, &SamePathA},
    {"KERNEL32.dll", "GetLongPathNameW", 12, CallConv::Stdcall, &SamePathW},
    {"KERNEL32.dll", "GetShortPathNameW", 12, CallConv::Stdcall, &SamePathW},
    {"KERNEL32.dll", "SetHandleCount", 4, CallConv::Stdcall, &K32_SetHandleCount},
};
const bool kRegistered = RegisterHostFunctions(kEntries, sizeof(kEntries) / sizeof(kEntries[0]));

} // namespace
} // namespace win32
