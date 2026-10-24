#pragma once
// files.h
//
// Файловая система гостя поверх файлов хоста.
//
// Пути Windows ("C:\Games\save.dat", "data\\level1.map", "/x/y") сводятся к
// путям хоста: диск C: — корневой каталог гостя (по умолчанию каталог
// EXE). Регистр букв не важен, как в Windows: на хосте с
// регистрозависимой ФС (Linux) нужный файл ищется без учёта регистра.
//
// Открытые файлы — в общей таблице: хендлы KERNEL32 (CreateFile) и
// дескрипторы msvcrt (_open, fopen) ссылаются на одни и те же объекты.

#include <array>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "handle_table.h"

namespace rt {

struct VolumeInfo {
    uint64_t totalBytes=0, freeBytes=0, availableBytes=0;
    uint32_t bytesPerSector=512, sectorsPerCluster=1, maxComponent=255;
    bool readOnly=false;
    std::string label, fileSystem;
};

// Snapshot of one object, not recursive directory totals or Windows birth time.
struct FileProperties {
    enum class Kind {File,Directory};
    Kind kind=Kind::File;
    uint64_t size=0;
    int64_t modifiedSeconds=0,accessedSeconds=0;
    bool mountReadOnly=false,ownerWriteBit=false;
};

class FileCopyIo;
class Vfs {
public:
    Vfs();
    // Standalone compatibility: one C drive rooted at the EXE directory.
    void SetRoot(const std::string& hostDir);
    const std::string& Root() const { return mounts_[2].root; }
    bool Persistent() const { return persistent_; }
    // Explicit directory-backed mounts; no global host chdir or guest pointers.
    bool Mount(char drive,const std::string& hostDir,bool readOnly=false,uint32_t type=3);
    bool ConfigurePersistent(const std::string& systemRoot,const std::string& sdRoot,
                             const std::string& romRoot,std::string& error);
    uint32_t DriveMask() const;
    uint32_t DriveType(const std::string& path) const;
    bool QueryVolume(const std::string& path,VolumeInfo& out) const;
    const std::string& CurrentDirectory() const { return cwd_; }
    bool SetCurrentDirectory(const std::string& guestPath,size_t* caseBudget=nullptr);
    std::string FullGuestPath(const std::string& guestPath) const;
    // Empty on invalid/unmounted/read-only/linked path; errno explains failure.
    // Every mutating API must request write access before touching host files.
    // Optional cumulative directory-entry budget for shell case lookup; ordinary
    // guest API behavior is unchanged. Exhaustion returns empty/EOVERFLOW.
    std::string ToHost(const std::string& guestPath,bool write=false,size_t* caseBudget=nullptr) const;
    std::string ToGuest(const std::string& hostPath,size_t* caseBudget=nullptr) const;
    std::string ProfileDirectory() const;
    // Real synchronous operations; false preserves host errno. No recursive
    // deletion, replacement-by-default, global cwd or retained native handles.
    bool QueryProperties(const std::string&,FileProperties&,size_t* caseBudget=nullptr) const;
    bool CreateDirectory(const std::string&,size_t* caseBudget=nullptr) const;
    bool DeleteFile(const std::string&,size_t* caseBudget=nullptr) const;
    bool RemoveDirectory(const std::string&,size_t* caseBudget=nullptr) const;
    bool Move(const std::string& from,const std::string& to,bool replace=false,size_t* caseBudget=nullptr) const;
    // Checked regular-file byte copy via an exclusive sibling staging file.
    // Existing destination is never opened/truncated; native publication can
    // fail if replacement rename is unsupported. No metadata/durability promise.
    bool CopyFile(const std::string& from,const std::string& to,bool failIfExists=true,
                  size_t* caseBudget=nullptr,FileCopyIo* io=nullptr) const;
private:
    friend class FileCopyJob;
    bool MutationPath(const std::string&,std::string& host,size_t*) const;
    struct MountPoint {std::string root,cwd;bool readOnly=false;uint32_t type=0;};
    std::array<MountPoint,26> mounts_{};
    std::string cwd_="C:\\";
    bool persistent_=false;
};

Vfs& FileSystem();

// Открытый файл или устройство.
struct OpenFile : KernelObject {
    OpenFile() : KernelObject(ObjectKind::File) {}
    enum Kind { Disk, ConsoleIn, ConsoleOut, ConsoleErr, Directory, Null } kind = Disk;
    std::FILE* f = nullptr;
    std::string hostPath;
    bool canRead = false, canWrite = false, append = false;
    bool deleteOnClose = false;
    ~OpenFile();
};

class FileTable {
public:
    FileTable();
    // Возвращает хендл KERNEL32 (не 0 и не INVALID_HANDLE_VALUE).
    uint32_t Add(std::shared_ptr<OpenFile> f);
    std::shared_ptr<OpenFile> Get(uint32_t handle) const;
    bool Close(uint32_t handle);
    // Дубликат хендла на тот же объект
    uint32_t Duplicate(uint32_t handle);

    static constexpr uint32_t kStdIn = 0x03, kStdOut = 0x07, kStdErr = 0x0B;

};

FileTable& Files();

// Вывод в консоль гостя (kernel32_module.cpp: g_consoleWriteImpl или stdout).
void ConsoleWrite(bool isStderr, const char* data, size_t len);

} // namespace rt
