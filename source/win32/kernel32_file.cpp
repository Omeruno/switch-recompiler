// kernel32_file.cpp — KERNEL32: файлы, каталоги, пути (поверх rt::Vfs и
// rt::Files). Хендлы консоли (stdin/stdout/stderr) — тоже здесь.

#include <cctype>
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

// BOOL ReadFile(HANDLE, LPVOID buf, DWORD n, LPDWORD read, LPOVERLAPPED)
HostCallOutcome K32_ReadFile(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t h = Arg(c, m, 0), buf = Arg(c, m, 1), n = Arg(c, m, 2), readPtr = Arg(c, m, 3);
    auto f = rt::Files().Get(h);
    if (readPtr) m.Write32(readPtr, 0);
    if (!f) { SetLastError(m, ERROR_INVALID_HANDLE); return Ret(0); }
    if (!HandleAccess(m,h,1) || !f->canRead) { SetLastError(m,5);return Ret(0); }
    std::vector<uint8_t> tmp(n);
    size_t got = 0;
    if (f->kind == rt::OpenFile::Disk && f->f) got = std::fread(tmp.data(), 1, n, f->f);
    else if (f->kind == rt::OpenFile::ConsoleIn && cpu::g_consoleAvailable) {
        // построчно, как консоль Windows
        while (got < n) {
            int ch = std::fgetc(stdin);
            if (ch == EOF) break;
            if (ch == '\n' && got + 1 < n) tmp[got++] = '\r';
            tmp[got++] = static_cast<uint8_t>(ch);
            if (ch == '\n') break;
        }
    }
    m.LoadBytes(buf, tmp.data(), got);
    if (readPtr) m.Write32(readPtr, static_cast<uint32_t>(got));
    return Ret(1);
}

HostCallOutcome K32_WriteFile(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t h = Arg(c, m, 0), buf = Arg(c, m, 1), n = Arg(c, m, 2), written = Arg(c, m, 3);
    if (written) m.Write32(written,0);
    const auto* entry=rt::Handles().Entry(h);
    if(!entry || entry->object->type!=rt::ObjectKind::File) { SetLastError(m,6);return Ret(0); }
    if(!(entry->access&6)) { SetLastError(m,5);return Ret(0); }
    std::string s(n, '\0');
    m.ReadBytes(buf, reinterpret_cast<uint8_t*>(&s[0]), n);
    if (written) m.Write32(written, 0);
    if (HleConsoleWrite(h, s.data(), s.size())) {
        if (written) m.Write32(written, n);
        return Ret(1);
    }
    auto f = rt::Files().Get(h);
    if (!f || !f->canWrite) { SetLastError(m, f ? ERROR_ACCESS_DENIED : ERROR_INVALID_HANDLE); return Ret(0); }
    size_t put = n;
    if (f->kind == rt::OpenFile::Disk && f->f) {
        if (f->append || !(entry->access&2)) std::fseek(f->f, 0, SEEK_END);
        put = std::fwrite(s.data(), 1, n, f->f);
    }
    if (written) m.Write32(written, static_cast<uint32_t>(put));
    return Ret(put == n ? 1 : 0);
}

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
int64_t FileSize(rt::OpenFile& f) {
    if (!f.f) return 0;
    std::fflush(f.f);
    struct stat st;
    if (fstat(fileno(f.f), &st) != 0) return 0;
    return static_cast<int64_t>(st.st_size);
}
HostCallOutcome K32_GetFileSize(cpu::CpuState& c, cpu::IGuestMemory& m) {
    auto f = rt::Files().Get(Arg(c, m, 0));
    if (!f) { SetLastError(m, ERROR_INVALID_HANDLE); return Ret(0xFFFFFFFFu); }
    int64_t sz = FileSize(*f);
    if (Arg(c, m, 1)) m.Write32(Arg(c, m, 1), static_cast<uint32_t>(static_cast<uint64_t>(sz) >> 32));
    return Ret(static_cast<uint32_t>(sz));
}
HostCallOutcome K32_GetFileSizeEx(cpu::CpuState& c, cpu::IGuestMemory& m) {
    auto f = rt::Files().Get(Arg(c, m, 0));
    if (!f) { SetLastError(m, ERROR_INVALID_HANDLE); return Ret(0); }
    Write64(m, Arg(c, m, 1), static_cast<uint64_t>(FileSize(*f)));
    return Ret(1);
}
HostCallOutcome K32_SetEndOfFile(cpu::CpuState& c, cpu::IGuestMemory& m) {
    auto f = rt::Files().Get(Arg(c, m, 0));
    if (!f || !f->f) { SetLastError(m, ERROR_INVALID_HANDLE); return Ret(0); }
    if (!HandleAccess(m,Arg(c,m,0),2)) return Ret(0);
    std::fflush(f->f);
    return Ret(ftruncate(fileno(f->f), ftello(f->f)) == 0 ? 1 : 0);
}
HostCallOutcome K32_FlushFileBuffers(cpu::CpuState& c, cpu::IGuestMemory& m) {
    auto f = rt::Files().Get(Arg(c, m, 0));
    if (f && f->f) std::fflush(f->f);
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
    const auto host=rt::FileSystem().ToHost(ReadPath(m,Arg(c,m,0),wide),true);
    if(host.empty()){SetLastError(m,ErrnoToWin());return Ret(0);}
    const int result=op==PathMutation::Delete?std::remove(host.c_str()):
                     op==PathMutation::Mkdir?mkdir(host.c_str(),0777):rmdir(host.c_str());
    return PathOp(m,result);
}
HostCallOutcome K32_DeleteFileA(cpu::CpuState& c,cpu::IGuestMemory& m){return MutatePath(c,m,false,PathMutation::Delete);}
HostCallOutcome K32_DeleteFileW(cpu::CpuState& c,cpu::IGuestMemory& m){return MutatePath(c,m,true,PathMutation::Delete);}
HostCallOutcome MoveCommon(cpu::CpuState& c, cpu::IGuestMemory& m, bool wide, bool replace) {
    std::string a = rt::FileSystem().ToHost(ReadPath(m, Arg(c, m, 0), wide),true);
    if(a.empty()){SetLastError(m,ErrnoToWin());return Ret(0);}
    std::string b = rt::FileSystem().ToHost(ReadPath(m, Arg(c, m, 1), wide),true);
    if(b.empty()){SetLastError(m,ErrnoToWin());return Ret(0);}
    struct stat st;
    if (!replace && StatHost(b, st)) { SetLastError(m, ERROR_ALREADY_EXISTS); return Ret(0); }
    return PathOp(m, std::rename(a.c_str(), b.c_str()));
}
HostCallOutcome K32_MoveFileA(cpu::CpuState& c, cpu::IGuestMemory& m) { return MoveCommon(c, m, false, false); }
HostCallOutcome K32_MoveFileW(cpu::CpuState& c, cpu::IGuestMemory& m) { return MoveCommon(c, m, true, false); }
HostCallOutcome K32_MoveFileExA(cpu::CpuState& c, cpu::IGuestMemory& m) { return MoveCommon(c, m, false, (Arg(c, m, 2) & 1) != 0); }
HostCallOutcome K32_MoveFileExW(cpu::CpuState& c, cpu::IGuestMemory& m) { return MoveCommon(c, m, true, (Arg(c, m, 2) & 1) != 0); }
HostCallOutcome CopyCommon(cpu::CpuState& c, cpu::IGuestMemory& m, bool wide) {
    std::string a = rt::FileSystem().ToHost(ReadPath(m, Arg(c, m, 0), wide));
    if(a.empty()){SetLastError(m,ErrnoToWin());return Ret(0);}
    std::string b = rt::FileSystem().ToHost(ReadPath(m, Arg(c, m, 1), wide),true);
    if(b.empty()){SetLastError(m,ErrnoToWin());return Ret(0);}
    bool failIfExists = Arg(c, m, 2) != 0;
    struct stat st;
    if (failIfExists && StatHost(b, st)) { SetLastError(m, ERROR_FILE_EXISTS); return Ret(0); }
    std::FILE* in = std::fopen(a.c_str(), "rb");
    if (!in) { SetLastError(m, ErrnoToWin()); return Ret(0); }
    std::FILE* out = std::fopen(b.c_str(), "wb");
    if (!out) { std::fclose(in); SetLastError(m, ErrnoToWin()); return Ret(0); }
    char buf[8192];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), in)) > 0) std::fwrite(buf, 1, n, out);
    std::fclose(in);
    std::fclose(out);
    return Ret(1);
}
HostCallOutcome K32_CopyFileA(cpu::CpuState& c, cpu::IGuestMemory& m) { return CopyCommon(c, m, false); }
HostCallOutcome K32_CopyFileW(cpu::CpuState& c, cpu::IGuestMemory& m) { return CopyCommon(c, m, true); }
HostCallOutcome K32_CreateDirectoryA(cpu::CpuState& c,cpu::IGuestMemory& m){return MutatePath(c,m,false,PathMutation::Mkdir);}
HostCallOutcome K32_CreateDirectoryW(cpu::CpuState& c,cpu::IGuestMemory& m){return MutatePath(c,m,true,PathMutation::Mkdir);}
HostCallOutcome K32_RemoveDirectoryA(cpu::CpuState& c,cpu::IGuestMemory& m){return MutatePath(c,m,false,PathMutation::Rmdir);}
HostCallOutcome K32_RemoveDirectoryW(cpu::CpuState& c,cpu::IGuestMemory& m){return MutatePath(c,m,true,PathMutation::Rmdir);}

HostCallOutcome AttrCommon(cpu::CpuState& c, cpu::IGuestMemory& m, bool wide) {
    struct stat st;
    if (!StatHost(rt::FileSystem().ToHost(ReadPath(m, Arg(c, m, 0), wide)), st)) {
        SetLastError(m, ERROR_FILE_NOT_FOUND);
        return Ret(0xFFFFFFFFu); // INVALID_FILE_ATTRIBUTES
    }
    return Ret(AttributesOf(st));
}
HostCallOutcome K32_GetFileAttributesA(cpu::CpuState& c, cpu::IGuestMemory& m) { return AttrCommon(c, m, false); }
HostCallOutcome K32_GetFileAttributesW(cpu::CpuState& c, cpu::IGuestMemory& m) { return AttrCommon(c, m, true); }
// BOOL GetFileAttributesEx(name, level, WIN32_FILE_ATTRIBUTE_DATA*)
HostCallOutcome AttrExCommon(cpu::CpuState& c, cpu::IGuestMemory& m, bool wide) {
    struct stat st;
    if (!StatHost(rt::FileSystem().ToHost(ReadPath(m, Arg(c, m, 0), wide)), st)) { SetLastError(m, ERROR_FILE_NOT_FOUND); return Ret(0); }
    uint32_t p = Arg(c, m, 2);
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
