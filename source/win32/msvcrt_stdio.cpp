// msvcrt_stdio.cpp — msvcrt.dll: потоки FILE*, дескрипторы _open/_read,
// семейства printf/scanf, каталоги и атрибуты (_stat, _findfirst, ...).
//
// FILE* гостя — настоящая 32-байтовая структура в его памяти (поля
// _ptr/_cnt держим нулевыми: макросы getc/putc старых программ тогда
// вызывают _filbuf/_flsbuf, которые и реализованы здесь). Состояние потока
// (дескриптор, EOF, ошибка, ungetc) — на стороне хоста.
//
// Текстовый режим (по умолчанию): при записи в файл "\n" -> "\r\n", при
// чтении "\r\n" -> "\n" и Ctrl+Z — конец файла. В консоль "\r" не пишем.

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <map>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "msvcrt.h"
#include "msvcrt_format.h"
#include "../runtime/files.h"

namespace win32 {

bool HleConsoleWrite(uint32_t handle, const char* data, size_t len); // kernel32_module.cpp

namespace crt {

using namespace hle;

namespace {

constexpr uint32_t _IOREAD = 0x0001, _IOWRT = 0x0002, _IOEOF = 0x0010, _IOERR = 0x0020, _IORW = 0x0080;

struct Fd {
    uint32_t handle = 0;
    bool open = false;
    bool text = true;
    bool append = false;
    bool eof = false;
};
struct FdTable {
    std::vector<Fd> fds;
    FdTable():fds(3) {
        fds[0]={rt::FileTable::kStdIn,true,true,false,false};
        fds[1]={rt::FileTable::kStdOut,true,true,false,false};
        fds[2]={rt::FileTable::kStdErr,true,true,false,false};
    }
};
std::vector<Fd>& Fds() {return rt::LocalState<FdTable>().fds;}

int NewFd(uint32_t handle, bool text, bool append) {
    auto& v = Fds();
    for (size_t i = 3; i < v.size(); ++i) if (!v[i].open) { v[i] = {handle, true, text, append, false}; return static_cast<int>(i); }
    v.push_back({handle, true, text, append, false});
    return static_cast<int>(v.size() - 1);
}
Fd* GetFd(int fd) {
    auto& v = Fds();
    if (fd < 0 || static_cast<size_t>(fd) >= v.size() || !v[fd].open) return nullptr;
    return &v[fd];
}

struct Stream {
    int fd = -1;
    bool eof = false, err = false;
    int ungot = -1;
    bool ownsStruct = false;
};
struct StreamTable {std::map<uint32_t,Stream> streams;};
std::map<uint32_t,Stream>& Streams() {return rt::LocalState<StreamTable>().streams;}
Stream* GetStream(cpu::IGuestMemory& m, uint32_t file) {
    auto& s = Streams();
    auto it = s.find(file);
    if (it != s.end()) return &it->second;
    // стандартные потоки _iob[0..2] — заводятся при первом обращении
    uint32_t iob = G(m).iob;
    if (file >= iob && file < iob + 3 * kFileSize && (file - iob) % kFileSize == 0) {
        Stream st;
        st.fd = static_cast<int>((file - iob) / kFileSize);
        return &(s[file] = st);
    }
    return nullptr;
}
void UpdateFlags(cpu::IGuestMemory& m, uint32_t file, const Stream& s) {
    uint32_t f = m.Read32(file + 12) & ~(_IOEOF | _IOERR);
    if (s.eof) f |= _IOEOF;
    if (s.err) f |= _IOERR;
    m.Write32(file + 12, f);
}

// --- Ввод-вывод по дескриптору ---
int64_t FdWrite(int fd, const char* data, size_t len) {
    Fd* d = GetFd(fd);
    if (!d) return -1;
    std::string buf;
    auto f = rt::Files().Get(d->handle);
    if (!f) return -1;
    const auto* entry=rt::Handles().Entry(d->handle);
    if(!entry || !(entry->access&6) || !f->canWrite) return -1;
    bool console = f->kind == rt::OpenFile::ConsoleOut || f->kind == rt::OpenFile::ConsoleErr;
    if (d->text && !console) {
        buf.reserve(len + len / 8);
        for (size_t i = 0; i < len; ++i) { if (data[i] == '\n') buf.push_back('\r'); buf.push_back(data[i]); }
        data = buf.data();
        len = buf.size();
    }
    if (console) { HleConsoleWrite(d->handle, data, len); return static_cast<int64_t>(len); }
    if (f->kind == rt::OpenFile::Null) return static_cast<int64_t>(len);
    if (!f->f || !f->canWrite) return -1;
    if (d->append || !(entry->access&2)) std::fseek(f->f, 0, SEEK_END);
    return static_cast<int64_t>(std::fwrite(data, 1, len, f->f));
}
// Один байт (без текстового преобразования); -1 — конец
int RawGetc(Fd* d) {
    auto f = rt::Files().Get(d->handle);
    if (!f) return -1;
    if(!rt::Handles().HasAccess(d->handle,1) || !f->canRead) return -1;
    if (f->kind == rt::OpenFile::ConsoleIn) return cpu::g_consoleAvailable ? std::fgetc(stdin) : -1;
    if (!f->f) return -1;
    return std::fgetc(f->f);
}
int FdGetc(int fd) {
    Fd* d = GetFd(fd);
    if (!d || d->eof) return -1;
    int ch = RawGetc(d);
    if (ch < 0) { d->eof = true; return -1; }
    if (d->text) {
        if (ch == 0x1A) { d->eof = true; return -1; }
        if (ch == '\r') {
            int next = RawGetc(d);
            if (next == '\n') return '\n';
            if (next >= 0) {
                auto f = rt::Files().Get(d->handle);
                if (f && f->f) std::ungetc(next, f->f);
            }
        }
    }
    return ch;
}
int64_t FdRead(int fd, uint8_t* out, size_t n) {
    Fd* d = GetFd(fd);
    if (!d) return -1;
    auto source=rt::Files().Get(d->handle);
    if(!source || !source->canRead || !rt::Handles().HasAccess(d->handle,1)) return -1;
    if (!d->text) {
        auto f = rt::Files().Get(d->handle);
        if (!f) return -1;
        if (f->kind == rt::OpenFile::Disk && f->f) {
            size_t got = std::fread(out, 1, n, f->f);
            if (got < n) d->eof = true;
            return static_cast<int64_t>(got);
        }
    }
    size_t i = 0;
    for (; i < n; ++i) {
        int ch = FdGetc(fd);
        if (ch < 0) break;
        out[i] = static_cast<uint8_t>(ch);
        if (ch == '\n' && GetFd(fd) && rt::Files().Get(GetFd(fd)->handle)->kind == rt::OpenFile::ConsoleIn) { ++i; break; }
    }
    return static_cast<int64_t>(i);
}

// --- Открытие ---
struct ModeInfo { bool read = false, write = false, append = false, create = false, truncate = false, text = true, valid = true; };
ModeInfo ParseMode(const std::string& mode) {
    ModeInfo mi;
    if (mode.empty()) { mi.valid = false; return mi; }
    switch (mode[0]) {
        case 'r': mi.read = true; break;
        case 'w': mi.write = true; mi.create = true; mi.truncate = true; break;
        case 'a': mi.write = true; mi.create = true; mi.append = true; break;
        default: mi.valid = false; return mi;
    }
    for (size_t i = 1; i < mode.size(); ++i) {
        if (mode[i] == '+') { mi.read = mi.write = true; }
        else if (mode[i] == 'b') mi.text = false;
        else if (mode[i] == 't') mi.text = true;
    }
    return mi;
}

// Открывает файл хоста по пути гостя; -1 и errno — при ошибке
int OpenPath(cpu::IGuestMemory& m, const std::string& guestPath, const ModeInfo& mi) {
    std::string host = rt::FileSystem().ToHost(guestPath,mi.write || mi.create);
    if(host.empty()){SetErrno(m,errno==EROFS||errno==EACCES?kEACCES:kENOENT);return -1;}
    struct stat st;
    bool exists = stat(host.c_str(), &st) == 0;
    if (exists && S_ISDIR(st.st_mode)) { SetErrno(m, kEACCES); return -1; }
    if (!exists && !mi.create) { SetErrno(m, kENOENT); return -1; }
    const char* hm;
    if (!mi.write) hm = "rb";
    else if (mi.truncate || !exists) hm = mi.read ? "w+b" : "wb";
    else hm = "r+b";
    std::FILE* f = std::fopen(host.c_str(), hm);
    if (!f) { SetErrno(m, errno == EACCES ? kEACCES : kENOENT); return -1; }
    auto of = std::make_shared<rt::OpenFile>();
    of->f = f;
    of->hostPath = host;
    of->canRead = mi.read;
    of->canWrite = mi.write;
    of->append = mi.append;
    uint32_t h = rt::Files().Add(of);
    return NewFd(h, mi.text, mi.append);
}

uint32_t NewFileStruct(cpu::IGuestMemory& m, int fd, const ModeInfo& mi) {
    // Dynamically allocated MSVCRT streams carry a six-DWORD critical section
    // immediately after the public FILE. MinGW's formatter enters that section
    // directly. Reserving only sizeof(FILE) aliases the next heap allocation.
    // Keep kFileSize unchanged: the public _iob array still has 32-byte stride.
    uint32_t file = Malloc(m, kFileSize + 24, true);
    m.Write32(file + kFileSize + 4, 0xFFFFFFFFu);
    m.Write32(file + 12, mi.read && mi.write ? _IORW : mi.read ? _IOREAD : _IOWRT);
    m.Write32(file + 16, static_cast<uint32_t>(fd));
    Stream s;
    s.fd = fd;
    s.ownsStruct = true;
    Streams()[file] = s;
    return file;
}

HostCallOutcome FopenCommon(cpu::IGuestMemory& m, const std::string& path, const std::string& mode) {
    ModeInfo mi = ParseMode(mode);
    if (!mi.valid || path.empty()) { SetErrno(m, kEINVAL); return Ret(0); }
    int fd = OpenPath(m, path, mi);
    if (fd < 0) return Ret(0);
    return Ret(NewFileStruct(m, fd, mi));
}
HostCallOutcome C_fopen(cpu::CpuState& c, cpu::IGuestMemory& m) { return FopenCommon(m, ReadAnsi(m, Arg(c, m, 0)), ReadAnsi(m, Arg(c, m, 1))); }
HostCallOutcome C_wfopen(cpu::CpuState& c, cpu::IGuestMemory& m) { return FopenCommon(m, WideToUtf8(ReadWide(m, Arg(c, m, 0))), WideToUtf8(ReadWide(m, Arg(c, m, 1)))); }
HostCallOutcome C_fsopen(cpu::CpuState& c, cpu::IGuestMemory& m) { return C_fopen(c, m); }

int CloseFd(int fd) {
    Fd* d = GetFd(fd);
    if (!d) return -1;
    if (fd > 2) { rt::Files().Close(d->handle); d->open = false; }
    return 0;
}
HostCallOutcome C_fclose(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t file = Arg(c, m, 0);
    Stream* s = GetStream(m, file);
    if (!s) { SetErrno(m, kEINVAL); return Ret(static_cast<uint32_t>(-1)); }
    CloseFd(s->fd);
    bool owns = s->ownsStruct;
    Streams().erase(file);
    if (owns) Free(m, file);
    return Ret(0);
}
HostCallOutcome C_fcloseall(cpu::CpuState&, cpu::IGuestMemory& m) {
    int n = 0;
    std::vector<uint32_t> files;
    for (auto& kv : Streams()) if (kv.second.ownsStruct) files.push_back(kv.first);
    for (uint32_t f : files) { CloseFd(Streams()[f].fd); Streams().erase(f); Free(m, f); ++n; }
    return Ret(static_cast<uint32_t>(n));
}
// FILE* _fdopen(int fd, const char* mode)
HostCallOutcome C_fdopen(cpu::CpuState& c, cpu::IGuestMemory& m) {
    int fd = static_cast<int>(Arg(c, m, 0));
    if (!GetFd(fd)) return Ret(0);
    return Ret(NewFileStruct(m, fd, ParseMode(ReadAnsi(m, Arg(c, m, 1)))));
}
// FILE* freopen(path, mode, FILE*)
HostCallOutcome C_freopen(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t file = Arg(c, m, 2);
    Stream* s = GetStream(m, file);
    if (!s) return Ret(0);
    ModeInfo mi = ParseMode(ReadAnsi(m, Arg(c, m, 1)));
    int fd = OpenPath(m, ReadAnsi(m, Arg(c, m, 0)), mi);
    if (fd < 0) return Ret(0);
    CloseFd(s->fd);
    s->fd = fd;
    s->eof = s->err = false;
    s->ungot = -1;
    m.Write32(file + 16, static_cast<uint32_t>(fd));
    return Ret(file);
}

} // namespace

int StreamWrite(cpu::IGuestMemory& m, uint32_t file, const char* data, size_t len) {
    Stream* s = GetStream(m, file);
    if (!s) return -1;
    int64_t n = FdWrite(s->fd, data, len);
    if (n < 0 || static_cast<size_t>(n) < len) { s->err = true; UpdateFlags(m, file, *s); return -1; }
    return static_cast<int>(len);
}

void FlushAll(cpu::IGuestMemory&) {
    for (auto& d : Fds()) {
        if (!d.open) continue;
        auto f = rt::Files().Get(d.handle);
        if (f && f->f) std::fflush(f->f);
    }
    std::fflush(stdout);
}

namespace {

int StreamGetc(cpu::IGuestMemory& m, uint32_t file) {
    Stream* s = GetStream(m, file);
    if (!s) return -1;
    if (s->ungot >= 0) { int ch = s->ungot; s->ungot = -1; return ch; }
    int ch = FdGetc(s->fd);
    if (ch < 0) { s->eof = true; UpdateFlags(m, file, *s); }
    return ch;
}

HostCallOutcome C_fflush(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t file = Arg(c, m, 0);
    if (!file) { FlushAll(m); return Ret(0); }
    Stream* s = GetStream(m, file);
    if (s) {
        Fd* d = GetFd(s->fd);
        if (d) { auto f = rt::Files().Get(d->handle); if (f && f->f) std::fflush(f->f); }
    }
    return Ret(0);
}
HostCallOutcome C_flushall(cpu::CpuState&, cpu::IGuestMemory& m) { FlushAll(m); return Ret(static_cast<uint32_t>(Streams().size())); }

// size_t fwrite(const void* p, size_t size, size_t n, FILE*)
HostCallOutcome C_fwrite(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t p = Arg(c, m, 0), size = Arg(c, m, 1), n = Arg(c, m, 2), file = Arg(c, m, 3);
    uint64_t total = static_cast<uint64_t>(size) * n;
    if (!total) return Ret(0);
    std::string buf(static_cast<size_t>(total), '\0');
    m.ReadBytes(p, reinterpret_cast<uint8_t*>(&buf[0]), buf.size());
    if (StreamWrite(m, file, buf.data(), buf.size()) < 0) return Ret(0);
    return Ret(n);
}
// size_t fread(void* p, size_t size, size_t n, FILE*)
HostCallOutcome C_fread(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t p = Arg(c, m, 0), size = Arg(c, m, 1), n = Arg(c, m, 2), file = Arg(c, m, 3);
    Stream* s = GetStream(m, file);
    if (!s || !size) return Ret(0);
    uint64_t total = static_cast<uint64_t>(size) * n;
    std::vector<uint8_t> buf(static_cast<size_t>(total));
    size_t got = 0;
    if (s->ungot >= 0 && total) { buf[got++] = static_cast<uint8_t>(s->ungot); s->ungot = -1; }
    int64_t r = FdRead(s->fd, buf.data() + got, buf.size() - got);
    if (r > 0) got += static_cast<size_t>(r);
    if (got < buf.size()) { s->eof = true; UpdateFlags(m, file, *s); }
    m.LoadBytes(p, buf.data(), got);
    return Ret(static_cast<uint32_t>(got / size));
}
HostCallOutcome C_fputc(cpu::CpuState& c, cpu::IGuestMemory& m) {
    char ch = static_cast<char>(Arg(c, m, 0));
    if (StreamWrite(m, Arg(c, m, 1), &ch, 1) < 0) return Ret(static_cast<uint32_t>(-1));
    return Ret(static_cast<uint8_t>(ch));
}
HostCallOutcome C_putchar(cpu::CpuState& c, cpu::IGuestMemory& m) {
    char ch = static_cast<char>(Arg(c, m, 0));
    StreamWrite(m, G(m).iob + kFileSize, &ch, 1);
    return Ret(static_cast<uint8_t>(ch));
}
HostCallOutcome C_fputs(cpu::CpuState& c, cpu::IGuestMemory& m) {
    std::string s = ReadAnsi(m, Arg(c, m, 0));
    return Ret(StreamWrite(m, Arg(c, m, 1), s.data(), s.size()) < 0 ? static_cast<uint32_t>(-1) : 0);
}
HostCallOutcome C_puts(cpu::CpuState& c, cpu::IGuestMemory& m) {
    std::string s = ReadAnsi(m, Arg(c, m, 0)) + "\n";
    return Ret(StreamWrite(m, G(m).iob + kFileSize, s.data(), s.size()) < 0 ? static_cast<uint32_t>(-1) : 0);
}
HostCallOutcome C_fgetc(cpu::CpuState& c, cpu::IGuestMemory& m) {
    int ch = StreamGetc(m, Arg(c, m, 0));
    return Ret(static_cast<uint32_t>(ch < 0 ? -1 : ch));
}
HostCallOutcome C_getchar(cpu::CpuState&, cpu::IGuestMemory& m) {
    int ch = StreamGetc(m, G(m).iob);
    return Ret(static_cast<uint32_t>(ch < 0 ? -1 : ch));
}
// int _filbuf(FILE*) — вызывается макросом getc, когда _cnt < 0
HostCallOutcome C_filbuf(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t file = Arg(c, m, 0);
    m.Write32(file + 4, 0);
    int ch = StreamGetc(m, file);
    return Ret(static_cast<uint32_t>(ch < 0 ? -1 : ch));
}
// int _flsbuf(int ch, FILE*) — вызывается макросом putc
HostCallOutcome C_flsbuf(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t file = Arg(c, m, 1);
    m.Write32(file + 4, 0);
    char ch = static_cast<char>(Arg(c, m, 0));
    if (StreamWrite(m, file, &ch, 1) < 0) return Ret(static_cast<uint32_t>(-1));
    return Ret(static_cast<uint8_t>(ch));
}
HostCallOutcome C_ungetc(cpu::CpuState& c, cpu::IGuestMemory& m) {
    int ch = static_cast<int>(Arg(c, m, 0));
    Stream* s = GetStream(m, Arg(c, m, 1));
    if (!s || ch < 0) return Ret(static_cast<uint32_t>(-1));
    s->ungot = ch & 0xFF;
    s->eof = false;
    UpdateFlags(m, Arg(c, m, 1), *s);
    return Ret(static_cast<uint32_t>(ch & 0xFF));
}
// char* fgets(char* buf, int n, FILE*)
HostCallOutcome C_fgets(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t buf = Arg(c, m, 0), file = Arg(c, m, 2);
    int32_t n = static_cast<int32_t>(Arg(c, m, 1));
    if (n <= 0) return Ret(0);
    int32_t i = 0;
    while (i < n - 1) {
        int ch = StreamGetc(m, file);
        if (ch < 0) break;
        m.Write8(buf + i++, static_cast<uint8_t>(ch));
        if (ch == '\n') break;
    }
    if (i == 0) return Ret(0);
    m.Write8(buf + i, 0);
    return Ret(buf);
}
HostCallOutcome C_gets(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t buf = Arg(c, m, 0);
    uint32_t i = 0;
    int ch;
    while ((ch = StreamGetc(m, G(m).iob)) >= 0 && ch != '\n') m.Write8(buf + i++, static_cast<uint8_t>(ch));
    if (ch < 0 && i == 0) return Ret(0);
    m.Write8(buf + i, 0);
    return Ret(buf);
}
HostCallOutcome C_feof(cpu::CpuState& c, cpu::IGuestMemory& m) { Stream* s = GetStream(m, Arg(c, m, 0)); return Ret(s && s->eof ? _IOEOF : 0); }
HostCallOutcome C_ferror(cpu::CpuState& c, cpu::IGuestMemory& m) { Stream* s = GetStream(m, Arg(c, m, 0)); return Ret(s && s->err ? _IOERR : 0); }
HostCallOutcome C_clearerr(cpu::CpuState& c, cpu::IGuestMemory& m) {
    Stream* s = GetStream(m, Arg(c, m, 0));
    if (s) { s->eof = s->err = false; if (Fd* d = GetFd(s->fd)) d->eof = false; UpdateFlags(m, Arg(c, m, 0), *s); }
    return Ret(0);
}
HostCallOutcome C_fileno(cpu::CpuState& c, cpu::IGuestMemory& m) { Stream* s = GetStream(m, Arg(c, m, 0)); return Ret(static_cast<uint32_t>(s ? s->fd : -1)); }

std::FILE* HostFileOf(cpu::IGuestMemory& m, uint32_t file) {
    Stream* s = GetStream(m, file);
    if (!s) return nullptr;
    Fd* d = GetFd(s->fd);
    if (!d) return nullptr;
    auto f = rt::Files().Get(d->handle);
    return f ? f->f : nullptr;
}
// int fseek(FILE*, long, int)
HostCallOutcome C_fseek(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t file = Arg(c, m, 0);
    std::FILE* f = HostFileOf(m, file);
    Stream* s = GetStream(m, file);
    if (!f || !s) { SetErrno(m, kEBADF); return Ret(static_cast<uint32_t>(-1)); }
    int whence = Arg(c, m, 2) == 1 ? SEEK_CUR : Arg(c, m, 2) == 2 ? SEEK_END : SEEK_SET;
    long off = static_cast<int32_t>(Arg(c, m, 1));
    if (whence == SEEK_CUR && s->ungot >= 0) off -= 1;
    s->ungot = -1;
    s->eof = false;
    if (Fd* d = GetFd(s->fd)) d->eof = false;
    UpdateFlags(m, file, *s);
    return Ret(std::fseek(f, off, whence) == 0 ? 0 : static_cast<uint32_t>(-1));
}
HostCallOutcome C_ftell(cpu::CpuState& c, cpu::IGuestMemory& m) {
    std::FILE* f = HostFileOf(m, Arg(c, m, 0));
    Stream* s = GetStream(m, Arg(c, m, 0));
    if (!f) return Ret(static_cast<uint32_t>(-1));
    long p = std::ftell(f);
    if (s && s->ungot >= 0) --p;
    return Ret(static_cast<uint32_t>(p));
}
HostCallOutcome C_rewind(cpu::CpuState& c, cpu::IGuestMemory& m) {
    std::FILE* f = HostFileOf(m, Arg(c, m, 0));
    Stream* s = GetStream(m, Arg(c, m, 0));
    if (f) std::rewind(f);
    if (s) { s->eof = s->err = false; s->ungot = -1; if (Fd* d = GetFd(s->fd)) d->eof = false; UpdateFlags(m, Arg(c, m, 0), *s); }
    return Ret(0);
}
HostCallOutcome C_fgetpos(cpu::CpuState& c, cpu::IGuestMemory& m) {
    std::FILE* f = HostFileOf(m, Arg(c, m, 0));
    if (!f) return Ret(static_cast<uint32_t>(-1));
    long p = std::ftell(f);
    m.Write32(Arg(c, m, 1), static_cast<uint32_t>(p));
    m.Write32(Arg(c, m, 1) + 4, 0);
    return Ret(0);
}
HostCallOutcome C_fsetpos(cpu::CpuState& c, cpu::IGuestMemory& m) {
    std::FILE* f = HostFileOf(m, Arg(c, m, 0));
    if (!f) return Ret(static_cast<uint32_t>(-1));
    return Ret(std::fseek(f, static_cast<long>(m.Read32(Arg(c, m, 1))), SEEK_SET) == 0 ? 0 : static_cast<uint32_t>(-1));
}
HostCallOutcome C_setvbuf(cpu::CpuState&, cpu::IGuestMemory&) { return Ret(0); }

// --- printf ---
HostCallOutcome Printf(cpu::IGuestMemory& m, uint32_t file, uint32_t fmt, uint32_t va) {
    GuestVaList args{m, va};
    std::string s = FormatA(m, fmt, args);
    if (StreamWrite(m, file, s.data(), s.size()) < 0) return Ret(static_cast<uint32_t>(-1));
    return Ret(static_cast<uint32_t>(s.size()));
}
HostCallOutcome WPrintf(cpu::IGuestMemory& m, uint32_t file, uint32_t fmt, uint32_t va) {
    GuestVaList args{m, va};
    std::string s = WideToUtf8(FormatW(m, fmt, args));
    if (StreamWrite(m, file, s.data(), s.size()) < 0) return Ret(static_cast<uint32_t>(-1));
    return Ret(static_cast<uint32_t>(s.size()));
}
uint32_t Stdout(cpu::IGuestMemory& m) { return G(m).iob + kFileSize; }
HostCallOutcome C_printf(cpu::CpuState& c, cpu::IGuestMemory& m) { return Printf(m, Stdout(m), Arg(c, m, 0), c.ESP() + 8); }
HostCallOutcome C_vprintf(cpu::CpuState& c, cpu::IGuestMemory& m) { return Printf(m, Stdout(m), Arg(c, m, 0), Arg(c, m, 1)); }
HostCallOutcome C_fprintf(cpu::CpuState& c, cpu::IGuestMemory& m) { return Printf(m, Arg(c, m, 0), Arg(c, m, 1), c.ESP() + 12); }
HostCallOutcome C_vfprintf(cpu::CpuState& c, cpu::IGuestMemory& m) { return Printf(m, Arg(c, m, 0), Arg(c, m, 1), Arg(c, m, 2)); }
HostCallOutcome C_wprintf(cpu::CpuState& c, cpu::IGuestMemory& m) { return WPrintf(m, Stdout(m), Arg(c, m, 0), c.ESP() + 8); }
HostCallOutcome C_vwprintf(cpu::CpuState& c, cpu::IGuestMemory& m) { return WPrintf(m, Stdout(m), Arg(c, m, 0), Arg(c, m, 1)); }
HostCallOutcome C_fwprintf(cpu::CpuState& c, cpu::IGuestMemory& m) { return WPrintf(m, Arg(c, m, 0), Arg(c, m, 1), c.ESP() + 12); }
HostCallOutcome C_vfwprintf(cpu::CpuState& c, cpu::IGuestMemory& m) { return WPrintf(m, Arg(c, m, 0), Arg(c, m, 1), Arg(c, m, 2)); }

HostCallOutcome Sprintf(cpu::IGuestMemory& m, uint32_t buf, int64_t cap, uint32_t fmt, uint32_t va) {
    GuestVaList args{m, va};
    std::string s = FormatA(m, fmt, args);
    if (cap < 0) {
        WriteAnsi(m, buf, s, static_cast<uint32_t>(s.size() + 1));
        return Ret(static_cast<uint32_t>(s.size()));
    }
    // _snprintf: если не влезло — -1 и без завершающего нуля
    size_t n = s.size() < static_cast<size_t>(cap) ? s.size() : static_cast<size_t>(cap);
    for (size_t i = 0; i < n; ++i) m.Write8(buf + static_cast<uint32_t>(i), static_cast<uint8_t>(s[i]));
    if (s.size() < static_cast<size_t>(cap)) { m.Write8(buf + static_cast<uint32_t>(n), 0); return Ret(static_cast<uint32_t>(s.size())); }
    return Ret(s.size() == static_cast<size_t>(cap) ? static_cast<uint32_t>(cap) : static_cast<uint32_t>(-1));
}
HostCallOutcome Swprintf(cpu::IGuestMemory& m, uint32_t buf, int64_t cap, uint32_t fmt, uint32_t va) {
    GuestVaList args{m, va};
    std::u16string s = FormatW(m, fmt, args);
    if (cap < 0) {
        WriteWide(m, buf, s, static_cast<uint32_t>(s.size() + 1));
        return Ret(static_cast<uint32_t>(s.size()));
    }
    size_t n = s.size() < static_cast<size_t>(cap) ? s.size() : static_cast<size_t>(cap);
    for (size_t i = 0; i < n; ++i) m.Write16(buf + static_cast<uint32_t>(i * 2), s[i]);
    if (s.size() < static_cast<size_t>(cap)) { m.Write16(buf + static_cast<uint32_t>(n * 2), 0); return Ret(static_cast<uint32_t>(s.size())); }
    return Ret(s.size() == static_cast<size_t>(cap) ? static_cast<uint32_t>(cap) : static_cast<uint32_t>(-1));
}
HostCallOutcome C_sprintf(cpu::CpuState& c, cpu::IGuestMemory& m) { return Sprintf(m, Arg(c, m, 0), -1, Arg(c, m, 1), c.ESP() + 12); }
HostCallOutcome C_vsprintf(cpu::CpuState& c, cpu::IGuestMemory& m) { return Sprintf(m, Arg(c, m, 0), -1, Arg(c, m, 1), Arg(c, m, 2)); }
HostCallOutcome C_snprintf(cpu::CpuState& c, cpu::IGuestMemory& m) { return Sprintf(m, Arg(c, m, 0), Arg(c, m, 1), Arg(c, m, 2), c.ESP() + 16); }
HostCallOutcome C_vsnprintf(cpu::CpuState& c, cpu::IGuestMemory& m) { return Sprintf(m, Arg(c, m, 0), Arg(c, m, 1), Arg(c, m, 2), Arg(c, m, 3)); }
HostCallOutcome C_swprintf(cpu::CpuState& c, cpu::IGuestMemory& m) { return Swprintf(m, Arg(c, m, 0), -1, Arg(c, m, 1), c.ESP() + 12); }
HostCallOutcome C_vswprintf(cpu::CpuState& c, cpu::IGuestMemory& m) { return Swprintf(m, Arg(c, m, 0), -1, Arg(c, m, 1), Arg(c, m, 2)); }
HostCallOutcome C_snwprintf(cpu::CpuState& c, cpu::IGuestMemory& m) { return Swprintf(m, Arg(c, m, 0), Arg(c, m, 1), Arg(c, m, 2), c.ESP() + 16); }
HostCallOutcome C_vsnwprintf(cpu::CpuState& c, cpu::IGuestMemory& m) { return Swprintf(m, Arg(c, m, 0), Arg(c, m, 1), Arg(c, m, 2), Arg(c, m, 3)); }
// *_s: (buf, размер в символах, fmt, ...) — не влезло: пустая строка и -1
HostCallOutcome C_sprintf_s(cpu::CpuState& c, cpu::IGuestMemory& m) {
    HostCallOutcome r = Sprintf(m, Arg(c, m, 0), Arg(c, m, 1), Arg(c, m, 2), c.ESP() + 16);
    if (r.returnValue >= Arg(c, m, 1)) { if (Arg(c, m, 1)) m.Write8(Arg(c, m, 0), 0); r.returnValue = static_cast<uint32_t>(-1); }
    return r;
}
HostCallOutcome C_vsprintf_s(cpu::CpuState& c, cpu::IGuestMemory& m) {
    HostCallOutcome r = Sprintf(m, Arg(c, m, 0), Arg(c, m, 1), Arg(c, m, 2), Arg(c, m, 3));
    if (r.returnValue >= Arg(c, m, 1)) { if (Arg(c, m, 1)) m.Write8(Arg(c, m, 0), 0); r.returnValue = static_cast<uint32_t>(-1); }
    return r;
}
HostCallOutcome C_swprintf_s(cpu::CpuState& c, cpu::IGuestMemory& m) {
    HostCallOutcome r = Swprintf(m, Arg(c, m, 0), Arg(c, m, 1), Arg(c, m, 2), c.ESP() + 16);
    if (r.returnValue >= Arg(c, m, 1)) { if (Arg(c, m, 1)) m.Write16(Arg(c, m, 0), 0); r.returnValue = static_cast<uint32_t>(-1); }
    return r;
}
HostCallOutcome C_vswprintf_s(cpu::CpuState& c, cpu::IGuestMemory& m) {
    HostCallOutcome r = Swprintf(m, Arg(c, m, 0), Arg(c, m, 1), Arg(c, m, 2), Arg(c, m, 3));
    if (r.returnValue >= Arg(c, m, 1)) { if (Arg(c, m, 1)) m.Write16(Arg(c, m, 0), 0); r.returnValue = static_cast<uint32_t>(-1); }
    return r;
}
HostCallOutcome C_scprintf(cpu::CpuState& c, cpu::IGuestMemory& m) {
    GuestVaList args{m, c.ESP() + 8};
    return Ret(static_cast<uint32_t>(FormatA(m, Arg(c, m, 0), args).size()));
}
HostCallOutcome C_vscprintf(cpu::CpuState& c, cpu::IGuestMemory& m) {
    GuestVaList args{m, Arg(c, m, 1)};
    return Ret(static_cast<uint32_t>(FormatA(m, Arg(c, m, 0), args).size()));
}

// --- scanf ---
HostCallOutcome C_sscanf(cpu::CpuState& c, cpu::IGuestMemory& m) {
    GuestVaList args{m, c.ESP() + 12};
    return Ret(static_cast<uint32_t>(ScanA(m, ReadAnsi(m, Arg(c, m, 0)), Arg(c, m, 1), args)));
}
HostCallOutcome C_swscanf(cpu::CpuState& c, cpu::IGuestMemory& m) {
    GuestVaList args{m, c.ESP() + 12};
    return Ret(static_cast<uint32_t>(ScanW(m, ReadWide(m, Arg(c, m, 0)), Arg(c, m, 1), args)));
}
// fscanf/scanf: читаем строку целиком (консоль — до конца строки, файл —
// до конца), разбираем и возвращаем непрочитанное назад.
HostCallOutcome StreamScan(cpu::IGuestMemory& m, uint32_t file, uint32_t fmt, uint32_t va) {
    Stream* s = GetStream(m, file);
    if (!s) return Ret(static_cast<uint32_t>(-1));
    std::string input;
    bool console = s->fd == 0;
    for (;;) {
        int ch = StreamGetc(m, file);
        if (ch < 0) break;
        input.push_back(static_cast<char>(ch));
        if (console && ch == '\n') {
            // одна строка, если в ней уже есть что-то кроме пробелов
            if (input.find_first_not_of(" \t\r\n") != std::string::npos) break;
        }
    }
    GuestVaList args{m, va};
    size_t used = 0;
    int r = ScanA(m, input, fmt, args, &used);
    // вернуть непрочитанное
    std::FILE* f = HostFileOf(m, file);
    if (f && used < input.size()) std::fseek(f, -static_cast<long>(input.size() - used), SEEK_CUR);
    if (used < input.size()) { s->eof = false; UpdateFlags(m, file, *s); }
    return Ret(static_cast<uint32_t>(r));
}
HostCallOutcome C_fscanf(cpu::CpuState& c, cpu::IGuestMemory& m) { return StreamScan(m, Arg(c, m, 0), Arg(c, m, 1), c.ESP() + 12); }
HostCallOutcome C_scanf(cpu::CpuState& c, cpu::IGuestMemory& m) { return StreamScan(m, G(m).iob, Arg(c, m, 0), c.ESP() + 8); }

// --- Дескрипторы ---
// int _open(const char* path, int oflag, int pmode)
HostCallOutcome OpenCommon(cpu::IGuestMemory& m, const std::string& path, uint32_t of) {
    ModeInfo mi;
    uint32_t acc = of & 3;
    mi.read = acc == 0 || acc == 2;
    mi.write = acc == 1 || acc == 2;
    mi.append = (of & 0x0008) != 0;   // _O_APPEND
    mi.create = (of & 0x0100) != 0;   // _O_CREAT
    mi.truncate = (of & 0x0200) != 0; // _O_TRUNC
    mi.text = !(of & 0x8000);         // _O_BINARY
    if ((of & 0x0400) && mi.create) { // _O_EXCL
        struct stat st;
        if (stat(rt::FileSystem().ToHost(path).c_str(), &st) == 0) { SetErrno(m, kEEXIST); return Ret(static_cast<uint32_t>(-1)); }
    }
    return Ret(static_cast<uint32_t>(OpenPath(m, path, mi)));
}
HostCallOutcome C_open(cpu::CpuState& c, cpu::IGuestMemory& m) { return OpenCommon(m, ReadAnsi(m, Arg(c, m, 0)), Arg(c, m, 1)); }
HostCallOutcome C_wopen(cpu::CpuState& c, cpu::IGuestMemory& m) { return OpenCommon(m, WideToUtf8(ReadWide(m, Arg(c, m, 0))), Arg(c, m, 1)); }
HostCallOutcome C_creat(cpu::CpuState& c, cpu::IGuestMemory& m) { return OpenCommon(m, ReadAnsi(m, Arg(c, m, 0)), 0x0301 | 0x8000); }
HostCallOutcome C_close(cpu::CpuState& c, cpu::IGuestMemory& m) { return Ret(static_cast<uint32_t>(CloseFd(static_cast<int>(Arg(c, m, 0))))); }
HostCallOutcome C_read(cpu::CpuState& c, cpu::IGuestMemory& m) {
    int fd = static_cast<int>(Arg(c, m, 0));
    uint32_t buf = Arg(c, m, 1), n = Arg(c, m, 2);
    std::vector<uint8_t> tmp(n);
    int64_t r = FdRead(fd, tmp.data(), n);
    if (r < 0) { SetErrno(m, kEBADF); return Ret(static_cast<uint32_t>(-1)); }
    m.LoadBytes(buf, tmp.data(), static_cast<size_t>(r));
    return Ret(static_cast<uint32_t>(r));
}
HostCallOutcome C_write(cpu::CpuState& c, cpu::IGuestMemory& m) {
    int fd = static_cast<int>(Arg(c, m, 0));
    uint32_t buf = Arg(c, m, 1), n = Arg(c, m, 2);
    std::string tmp(n, '\0');
    m.ReadBytes(buf, reinterpret_cast<uint8_t*>(&tmp[0]), n);
    int64_t r = FdWrite(fd, tmp.data(), tmp.size());
    if (r < 0) { SetErrno(m, kEBADF); return Ret(static_cast<uint32_t>(-1)); }
    return Ret(n);
}
std::FILE* HostFileOfFd(int fd) {
    Fd* d = GetFd(fd);
    if (!d) return nullptr;
    auto f = rt::Files().Get(d->handle);
    return f ? f->f : nullptr;
}
HostCallOutcome C_lseek(cpu::CpuState& c, cpu::IGuestMemory& m) {
    std::FILE* f = HostFileOfFd(static_cast<int>(Arg(c, m, 0)));
    if (!f) { SetErrno(m, kEBADF); return Ret(static_cast<uint32_t>(-1)); }
    int whence = Arg(c, m, 2) == 1 ? SEEK_CUR : Arg(c, m, 2) == 2 ? SEEK_END : SEEK_SET;
    if (std::fseek(f, static_cast<int32_t>(Arg(c, m, 1)), whence) != 0) { SetErrno(m, kEINVAL); return Ret(static_cast<uint32_t>(-1)); }
    if (Fd* d = GetFd(static_cast<int>(Arg(c, m, 0)))) d->eof = false;
    return Ret(static_cast<uint32_t>(std::ftell(f)));
}
HostCallOutcome C_tell(cpu::CpuState& c, cpu::IGuestMemory& m) {
    std::FILE* f = HostFileOfFd(static_cast<int>(Arg(c, m, 0)));
    return Ret(f ? static_cast<uint32_t>(std::ftell(f)) : static_cast<uint32_t>(-1));
}
HostCallOutcome C_filelength(cpu::CpuState& c, cpu::IGuestMemory& m) {
    std::FILE* f = HostFileOfFd(static_cast<int>(Arg(c, m, 0)));
    if (!f) return Ret(static_cast<uint32_t>(-1));
    std::fflush(f);
    struct stat st;
    fstat(fileno(f), &st);
    return Ret(static_cast<uint32_t>(st.st_size));
}
HostCallOutcome C_eof(cpu::CpuState& c, cpu::IGuestMemory& m) {
    std::FILE* f = HostFileOfFd(static_cast<int>(Arg(c, m, 0)));
    if (!f) return Ret(static_cast<uint32_t>(-1));
    long pos = std::ftell(f);
    std::fseek(f, 0, SEEK_END);
    long end = std::ftell(f);
    std::fseek(f, pos, SEEK_SET);
    return Ret(pos >= end ? 1 : 0);
}
HostCallOutcome C_setmode(cpu::CpuState& c, cpu::IGuestMemory& m) {
    Fd* d = GetFd(static_cast<int>(Arg(c, m, 0)));
    if (!d) return Ret(static_cast<uint32_t>(-1));
    uint32_t old = d->text ? 0x4000 : 0x8000;
    d->text = (Arg(c, m, 1) & 0x8000) == 0;
    return Ret(old);
}
HostCallOutcome C_isatty(cpu::CpuState& c, cpu::IGuestMemory& m) {
    Fd* d = GetFd(static_cast<int>(Arg(c, m, 0)));
    if (!d) return Ret(0);
    auto f = rt::Files().Get(d->handle);
    return Ret(f && f->kind != rt::OpenFile::Disk ? 1 : 0);
}
HostCallOutcome C_get_osfhandle(cpu::CpuState& c, cpu::IGuestMemory& m) {
    Fd* d = GetFd(static_cast<int>(Arg(c, m, 0)));
    return Ret(d ? d->handle : 0xFFFFFFFFu);
}
HostCallOutcome C_open_osfhandle(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t h = Arg(c, m, 0);
    if (!rt::Files().Get(h)) return Ret(static_cast<uint32_t>(-1));
    return Ret(static_cast<uint32_t>(NewFd(h, !(Arg(c, m, 1) & 0x8000), (Arg(c, m, 1) & 0x8) != 0)));
}
HostCallOutcome C_dup(cpu::CpuState& c, cpu::IGuestMemory& m) {
    Fd* d = GetFd(static_cast<int>(Arg(c, m, 0)));
    if (!d) return Ret(static_cast<uint32_t>(-1));
    Fd copy = *d;
    return Ret(static_cast<uint32_t>(NewFd(rt::Files().Duplicate(copy.handle), copy.text, copy.append)));
}
HostCallOutcome C_commit(cpu::CpuState& c, cpu::IGuestMemory& m) {
    std::FILE* f = HostFileOfFd(static_cast<int>(Arg(c, m, 0)));
    if (f) std::fflush(f);
    return Ret(0);
}

// --- Файлы по имени ---
std::string HostOf(cpu::IGuestMemory& m, uint32_t p, bool wide = false, bool write = false) {
    return rt::FileSystem().ToHost(wide ? WideToUtf8(ReadWide(m, p)) : ReadAnsi(m, p),write);
}
enum class CrtMutation {Remove,Mkdir,Rmdir,Rename};
HostCallOutcome CrtMutate(cpu::CpuState& c,cpu::IGuestMemory& m,bool wide,CrtMutation op) {
    const auto path=wide?WideToUtf8(ReadWide(m,Arg(c,m,0))):ReadAnsi(m,Arg(c,m,0));
    const auto& fs=rt::FileSystem();
    const bool ok=op==CrtMutation::Rename?fs.Move(path,ReadAnsi(m,Arg(c,m,1))):
        op==CrtMutation::Remove?fs.DeleteFile(path):op==CrtMutation::Mkdir?fs.CreateDirectory(path):fs.RemoveDirectory(path);
    if(!ok) {
        SetErrno(m,errno==ENOENT?kENOENT:errno==EEXIST?kEEXIST:errno==EINVAL?kEINVAL:kEACCES);
        return Ret(uint32_t(-1));
    }
    return Ret(0);
}
HostCallOutcome C_remove(cpu::CpuState& c,cpu::IGuestMemory& m){return CrtMutate(c,m,false,CrtMutation::Remove);}
HostCallOutcome C_wremove(cpu::CpuState& c,cpu::IGuestMemory& m){return CrtMutate(c,m,true,CrtMutation::Remove);}
HostCallOutcome C_rename(cpu::CpuState& c,cpu::IGuestMemory& m){return CrtMutate(c,m,false,CrtMutation::Rename);}
HostCallOutcome C_mkdir(cpu::CpuState& c,cpu::IGuestMemory& m){return CrtMutate(c,m,false,CrtMutation::Mkdir);}
HostCallOutcome C_wmkdir(cpu::CpuState& c,cpu::IGuestMemory& m){return CrtMutate(c,m,true,CrtMutation::Mkdir);}
HostCallOutcome C_rmdir(cpu::CpuState& c,cpu::IGuestMemory& m){return CrtMutate(c,m,false,CrtMutation::Rmdir);}
HostCallOutcome C_chdir(cpu::CpuState& c, cpu::IGuestMemory& m) {
    if (!rt::FileSystem().SetCurrentDirectory(ReadAnsi(m, Arg(c, m, 0)))) { SetErrno(m, kENOENT); return Ret(static_cast<uint32_t>(-1)); }
    return Ret(0);
}
HostCallOutcome C_getcwd(cpu::CpuState& c, cpu::IGuestMemory& m) {
    std::string d = rt::FileSystem().CurrentDirectory();
    if (d.size() == 2) d += "\\";
    uint32_t buf = Arg(c, m, 0), n = Arg(c, m, 1);
    if (!buf) { buf = Malloc(m, std::max<uint32_t>(n, static_cast<uint32_t>(d.size() + 1))); n = static_cast<uint32_t>(d.size() + 1); }
    if (n <= d.size()) { SetErrno(m, kERANGE); return Ret(0); }
    WriteAnsi(m, buf, d, n);
    return Ret(buf);
}
// int _access(const char* path, int mode): 0 существует, 2 запись, 4 чтение
HostCallOutcome C_access(cpu::CpuState& c, cpu::IGuestMemory& m) {
    struct stat st;
    if (stat(HostOf(m, Arg(c, m, 0)).c_str(), &st) != 0) { SetErrno(m, kENOENT); return Ret(static_cast<uint32_t>(-1)); }
    return Ret(0);
}
HostCallOutcome C_waccess(cpu::CpuState& c, cpu::IGuestMemory& m) {
    struct stat st;
    if (stat(HostOf(m, Arg(c, m, 0), true).c_str(), &st) != 0) { SetErrno(m, kENOENT); return Ret(static_cast<uint32_t>(-1)); }
    return Ret(0);
}
// struct _stat (32 бита): dev(4) ino(2) mode(2) nlink(2) uid(2) gid(2) pad(2) rdev(4) size(4) atime(4) mtime(4) ctime(4) = 36
void WriteStat(cpu::IGuestMemory& m, uint32_t p, const struct stat& st) {
    for (uint32_t i = 0; i < 36; i += 4) m.Write32(p + i, 0);
    m.Write32(p + 0, 2); // диск C:
    uint16_t mode = S_ISDIR(st.st_mode) ? 0x4000 | 0x49 : 0x8000;
    mode |= 0x100; // _S_IREAD
    if (st.st_mode & S_IWUSR) mode |= 0x80;
    m.Write16(p + 6, mode);
    m.Write16(p + 8, 1);
    m.Write32(p + 16, 2);
    m.Write32(p + 20, S_ISDIR(st.st_mode) ? 0 : static_cast<uint32_t>(st.st_size));
    m.Write32(p + 24, static_cast<uint32_t>(st.st_atime));
    m.Write32(p + 28, static_cast<uint32_t>(st.st_mtime));
    m.Write32(p + 32, static_cast<uint32_t>(st.st_ctime));
}
HostCallOutcome C_stat(cpu::CpuState& c, cpu::IGuestMemory& m) {
    struct stat st;
    if (stat(HostOf(m, Arg(c, m, 0)).c_str(), &st) != 0) { SetErrno(m, kENOENT); return Ret(static_cast<uint32_t>(-1)); }
    WriteStat(m, Arg(c, m, 1), st);
    return Ret(0);
}
HostCallOutcome C_wstat(cpu::CpuState& c, cpu::IGuestMemory& m) {
    struct stat st;
    if (stat(HostOf(m, Arg(c, m, 0), true).c_str(), &st) != 0) { SetErrno(m, kENOENT); return Ret(static_cast<uint32_t>(-1)); }
    WriteStat(m, Arg(c, m, 1), st);
    return Ret(0);
}
HostCallOutcome C_fstat(cpu::CpuState& c, cpu::IGuestMemory& m) {
    std::FILE* f = HostFileOfFd(static_cast<int>(Arg(c, m, 0)));
    struct stat st{};
    if (f) { std::fflush(f); fstat(fileno(f), &st); }
    else { st.st_mode = S_IFCHR; }
    WriteStat(m, Arg(c, m, 1), st);
    return Ret(0);
}

// --- _findfirst/_findnext: struct _finddata_t {attrib, time_create, time_access, time_write, size, name[260]} ---
struct FindState { std::vector<std::string> names; std::string dir; size_t next = 0; };
struct FindTable {
    std::map<uint32_t,FindState> finds;
    uint32_t next=0x100,tmpBuf=0;int tmpFile=0,tmpName=0;
};
FindTable& FindData() {return rt::LocalState<FindTable>();}
std::map<uint32_t,FindState>& Finds() {return FindData().finds;}
bool Wild(const char* p, const char* s) {
    if (!*p) return !*s;
    if (*p == '*') { for (const char* t = s;; ++t) { if (Wild(p + 1, t)) return true; if (!*t) return false; } }
    if (!*s) return std::strcmp(p, ".") == 0 || std::strcmp(p, "*") == 0;
    if (*p == '?' || std::tolower(static_cast<unsigned char>(*p)) == std::tolower(static_cast<unsigned char>(*s))) return Wild(p + 1, s + 1);
    return false;
}
void FillFind(cpu::IGuestMemory& m, uint32_t p, const std::string& dir, const std::string& name) {
    struct stat st{};
    stat((dir + "/" + name).c_str(), &st);
    m.Write32(p + 0, S_ISDIR(st.st_mode) ? 0x10 : 0x20);
    m.Write32(p + 4, static_cast<uint32_t>(st.st_ctime));
    m.Write32(p + 8, static_cast<uint32_t>(st.st_atime));
    m.Write32(p + 12, static_cast<uint32_t>(st.st_mtime));
    m.Write32(p + 16, S_ISDIR(st.st_mode) ? 0 : static_cast<uint32_t>(st.st_size));
    WriteAnsi(m, p + 20, name, 260);
}
HostCallOutcome C_findfirst(cpu::CpuState& c, cpu::IGuestMemory& m) {
    std::string full = rt::FileSystem().FullGuestPath(ReadAnsi(m, Arg(c, m, 0)));
    if(full.empty()){SetErrno(m,kENOENT);return Ret(uint32_t(-1));}
    size_t slash = full.find_last_of('\\');
    std::string dirGuest = full.substr(0, slash), mask = full.substr(slash + 1);
    FindState st;
    st.dir = rt::FileSystem().ToHost(dirGuest.size() == 2 ? dirGuest + "\\" : dirGuest);
    DIR* d = opendir(st.dir.c_str());
    if (!d) { SetErrno(m, kENOENT); return Ret(static_cast<uint32_t>(-1)); }
    while (dirent* e = readdir(d)) if (Wild(mask.c_str(), e->d_name)) st.names.push_back(e->d_name);
    closedir(d);
    if (st.names.empty()) { SetErrno(m, kENOENT); return Ret(static_cast<uint32_t>(-1)); }
    FillFind(m, Arg(c, m, 1), st.dir, st.names[0]);
    st.next = 1;
    uint32_t h = FindData().next++;
    Finds()[h] = st;
    return Ret(h);
}
HostCallOutcome C_findnext(cpu::CpuState& c, cpu::IGuestMemory& m) {
    auto it = Finds().find(Arg(c, m, 0));
    if (it == Finds().end() || it->second.next >= it->second.names.size()) { SetErrno(m, kENOENT); return Ret(static_cast<uint32_t>(-1)); }
    FillFind(m, Arg(c, m, 1), it->second.dir, it->second.names[it->second.next++]);
    return Ret(0);
}
HostCallOutcome C_findclose(cpu::CpuState& c, cpu::IGuestMemory& m) { Finds().erase(Arg(c, m, 0)); return Ret(0); }

HostCallOutcome C_tmpfile(cpu::CpuState&, cpu::IGuestMemory& m) {
    auto& n=FindData().tmpFile;
    std::string name = "C:\\TEMP\\tmp" + std::to_string(n++) + ".tmp";
    mkdir(rt::FileSystem().ToHost("C:\\TEMP",true).c_str(), 0777);
    ModeInfo mi = ParseMode("w+b");
    int fd = OpenPath(m, name, mi);
    if (fd < 0) return Ret(0);
    if (Fd* d = GetFd(fd)) if (auto f = rt::Files().Get(d->handle)) f->deleteOnClose = true;
    return Ret(NewFileStruct(m, fd, mi));
}
HostCallOutcome C_tmpnam(cpu::CpuState& c, cpu::IGuestMemory& m) {
    auto& n=FindData().tmpName;
    auto& buf=FindData().tmpBuf;
    if (!buf) buf = Malloc(m, 260, true);
    uint32_t out = Arg(c, m, 0) ? Arg(c, m, 0) : buf;
    WriteAnsi(m, out, "\\s" + std::to_string(n++) + ".", 260);
    return Ret(out);
}

// Консоль conio: getch/kbhit — ввода нет
HostCallOutcome C_kbhit(cpu::CpuState&, cpu::IGuestMemory&) { return Ret(0); }
HostCallOutcome C_getch(cpu::CpuState&, cpu::IGuestMemory&) { return Ret(cpu::g_consoleAvailable ? static_cast<uint32_t>(std::getchar()) : 13); }
HostCallOutcome C_putch(cpu::CpuState& c, cpu::IGuestMemory& m) { char ch = static_cast<char>(Arg(c, m, 0)); HleConsoleWrite(rt::FileTable::kStdOut, &ch, 1); return Ret(static_cast<uint8_t>(ch)); }
HostCallOutcome C_cputs(cpu::CpuState& c, cpu::IGuestMemory& m) { std::string s = ReadAnsi(m, Arg(c, m, 0)); HleConsoleWrite(rt::FileTable::kStdOut, s.data(), s.size()); return Ret(0); }
HostCallOutcome C_cprintf(cpu::CpuState& c, cpu::IGuestMemory& m) {
    GuestVaList args{m, c.ESP() + 8};
    std::string s = FormatA(m, Arg(c, m, 0), args);
    HleConsoleWrite(rt::FileTable::kStdOut, s.data(), s.size());
    return Ret(static_cast<uint32_t>(s.size()));
}

// Широкие символы в потоках (консоль — UTF-8)
HostCallOutcome C_fputwc(cpu::CpuState& c, cpu::IGuestMemory& m) {
    std::string s = WideToUtf8(std::u16string(1, static_cast<char16_t>(Arg(c, m, 0))));
    StreamWrite(m, Arg(c, m, 1), s.data(), s.size());
    return Ret(Arg(c, m, 0) & 0xFFFF);
}
HostCallOutcome C_fputws(cpu::CpuState& c, cpu::IGuestMemory& m) {
    std::string s = WideToUtf8(ReadWide(m, Arg(c, m, 0)));
    return Ret(StreamWrite(m, Arg(c, m, 1), s.data(), s.size()) < 0 ? static_cast<uint32_t>(-1) : 0);
}
HostCallOutcome C_putws(cpu::CpuState& c, cpu::IGuestMemory& m) {
    std::string s = WideToUtf8(ReadWide(m, Arg(c, m, 0))) + "\n";
    return Ret(StreamWrite(m, Stdout(m), s.data(), s.size()) < 0 ? static_cast<uint32_t>(-1) : 0);
}
HostCallOutcome C_fgetwc(cpu::CpuState& c, cpu::IGuestMemory& m) {
    int ch = StreamGetc(m, Arg(c, m, 0));
    return Ret(ch < 0 ? 0xFFFFu : static_cast<uint32_t>(ch));
}

#define CDECL_ENTRY(name, fn) {"msvcrt.dll", name, 0, CallConv::Cdecl, &fn}
const HostFunctionEntry kEntries[] = {
    CDECL_ENTRY("fopen", C_fopen), CDECL_ENTRY("_wfopen", C_wfopen), CDECL_ENTRY("_fsopen", C_fsopen),
    CDECL_ENTRY("fclose", C_fclose), CDECL_ENTRY("_fcloseall", C_fcloseall), CDECL_ENTRY("_fdopen", C_fdopen),
    CDECL_ENTRY("freopen", C_freopen), CDECL_ENTRY("fflush", C_fflush), CDECL_ENTRY("_flushall", C_flushall),
    CDECL_ENTRY("fwrite", C_fwrite), CDECL_ENTRY("fread", C_fread), CDECL_ENTRY("fputc", C_fputc),
    CDECL_ENTRY("putc", C_fputc), CDECL_ENTRY("_fputchar", C_putchar), CDECL_ENTRY("putchar", C_putchar),
    CDECL_ENTRY("fputs", C_fputs), CDECL_ENTRY("puts", C_puts), CDECL_ENTRY("fgetc", C_fgetc),
    CDECL_ENTRY("getc", C_fgetc), CDECL_ENTRY("_fgetchar", C_getchar), CDECL_ENTRY("getchar", C_getchar),
    CDECL_ENTRY("_filbuf", C_filbuf), CDECL_ENTRY("_flsbuf", C_flsbuf), CDECL_ENTRY("ungetc", C_ungetc),
    CDECL_ENTRY("fgets", C_fgets), CDECL_ENTRY("gets", C_gets), CDECL_ENTRY("feof", C_feof),
    CDECL_ENTRY("ferror", C_ferror), CDECL_ENTRY("clearerr", C_clearerr), CDECL_ENTRY("_fileno", C_fileno),
    CDECL_ENTRY("fseek", C_fseek), CDECL_ENTRY("ftell", C_ftell), CDECL_ENTRY("rewind", C_rewind),
    CDECL_ENTRY("fgetpos", C_fgetpos), CDECL_ENTRY("fsetpos", C_fsetpos), CDECL_ENTRY("setvbuf", C_setvbuf),
    CDECL_ENTRY("setbuf", C_setvbuf),
    CDECL_ENTRY("printf", C_printf), CDECL_ENTRY("vprintf", C_vprintf), CDECL_ENTRY("fprintf", C_fprintf),
    CDECL_ENTRY("vfprintf", C_vfprintf), CDECL_ENTRY("wprintf", C_wprintf), CDECL_ENTRY("vwprintf", C_vwprintf),
    CDECL_ENTRY("fwprintf", C_fwprintf), CDECL_ENTRY("vfwprintf", C_vfwprintf),
    CDECL_ENTRY("sprintf", C_sprintf), CDECL_ENTRY("vsprintf", C_vsprintf), CDECL_ENTRY("_snprintf", C_snprintf),
    CDECL_ENTRY("_vsnprintf", C_vsnprintf), CDECL_ENTRY("swprintf", C_swprintf), CDECL_ENTRY("vswprintf", C_vswprintf),
    CDECL_ENTRY("sprintf_s", C_sprintf_s), CDECL_ENTRY("vsprintf_s", C_vsprintf_s), CDECL_ENTRY("swprintf_s", C_swprintf_s),
    CDECL_ENTRY("vswprintf_s", C_vswprintf_s),
    CDECL_ENTRY("_swprintf", C_swprintf), CDECL_ENTRY("_snwprintf", C_snwprintf), CDECL_ENTRY("_vsnwprintf", C_vsnwprintf),
    CDECL_ENTRY("_scprintf", C_scprintf), CDECL_ENTRY("_vscprintf", C_vscprintf),
    CDECL_ENTRY("sscanf", C_sscanf), CDECL_ENTRY("swscanf", C_swscanf), CDECL_ENTRY("fscanf", C_fscanf),
    CDECL_ENTRY("scanf", C_scanf),
    CDECL_ENTRY("_open", C_open), CDECL_ENTRY("_wopen", C_wopen), CDECL_ENTRY("_creat", C_creat),
    CDECL_ENTRY("_close", C_close), CDECL_ENTRY("_read", C_read), CDECL_ENTRY("_write", C_write),
    CDECL_ENTRY("_lseek", C_lseek), CDECL_ENTRY("_tell", C_tell), CDECL_ENTRY("_filelength", C_filelength),
    CDECL_ENTRY("_eof", C_eof), CDECL_ENTRY("_setmode", C_setmode), CDECL_ENTRY("_isatty", C_isatty),
    CDECL_ENTRY("_get_osfhandle", C_get_osfhandle), CDECL_ENTRY("_open_osfhandle", C_open_osfhandle),
    CDECL_ENTRY("_dup", C_dup), CDECL_ENTRY("_commit", C_commit),
    CDECL_ENTRY("remove", C_remove), CDECL_ENTRY("_unlink", C_remove), CDECL_ENTRY("_wremove", C_wremove),
    CDECL_ENTRY("_wunlink", C_wremove), CDECL_ENTRY("rename", C_rename), CDECL_ENTRY("_mkdir", C_mkdir),
    CDECL_ENTRY("_wmkdir", C_wmkdir), CDECL_ENTRY("_rmdir", C_rmdir), CDECL_ENTRY("_chdir", C_chdir),
    CDECL_ENTRY("_getcwd", C_getcwd), CDECL_ENTRY("_access", C_access), CDECL_ENTRY("_waccess", C_waccess),
    CDECL_ENTRY("_stat", C_stat), CDECL_ENTRY("_wstat", C_wstat), CDECL_ENTRY("_fstat", C_fstat),
    CDECL_ENTRY("_findfirst", C_findfirst), CDECL_ENTRY("_findnext", C_findnext), CDECL_ENTRY("_findclose", C_findclose),
    CDECL_ENTRY("tmpfile", C_tmpfile), CDECL_ENTRY("tmpnam", C_tmpnam),
    CDECL_ENTRY("_kbhit", C_kbhit), CDECL_ENTRY("_getch", C_getch), CDECL_ENTRY("_getche", C_getch),
    CDECL_ENTRY("_putch", C_putch), CDECL_ENTRY("_cputs", C_cputs), CDECL_ENTRY("_cprintf", C_cprintf),
    CDECL_ENTRY("fputwc", C_fputwc), CDECL_ENTRY("putwc", C_fputwc), CDECL_ENTRY("fputws", C_fputws),
    CDECL_ENTRY("_putws", C_putws), CDECL_ENTRY("fgetwc", C_fgetwc), CDECL_ENTRY("getwc", C_fgetwc),
};
const bool kRegistered = RegisterHostFunctions(kEntries, sizeof(kEntries) / sizeof(kEntries[0]));

} // namespace
} // namespace crt
} // namespace win32
