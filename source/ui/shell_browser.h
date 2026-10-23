#pragma once
#include "../runtime/files.h"
#include <string>
#include <vector>

namespace ui {
struct ShellLaunchRequest {
    std::string guestPath,nativePath;
    void Clear() {guestPath.clear();nativePath.clear();}
};
// Independent host VFS metadata over the same F1 roots as each GuestSession.
// No global cwd, borrowed Process or file handles survive an operation.
class ShellBrowser {
public:
    static constexpr size_t MaxItems=256,MaxScanned=4096,MaxHistory=32;
    enum class Kind {Drive,Directory,File};
    enum class Error {None,NotReady,InvalidPath,NotFound,NotDirectory,AccessDenied,ReadFailed,UnsupportedFile,ResolutionLimit};
    enum class Sort {Name,NameDescending,SizeDescending};
    struct Entry {
        std::string name,path;
        Kind kind=Kind::File;
        uint64_t size=0;
        bool readOnly=false;
    };
    explicit ShellBrowser(size_t maxItems=MaxItems,size_t maxScanned=MaxScanned);
    bool Configure(const std::string& system,const std::string& sd,const std::string& rom,std::string& error);
    bool Ready() const {return ready_;}
    // Empty path is the virtual My Computer root; all others go through F1 VFS.
    bool Navigate(const std::string& path);
    bool Refresh();
    bool Back();
    bool Up();
    bool CanBack() const {return !history_.empty();}
    bool CanUp() const {return !path_.empty();}
    const std::string& Path() const {return path_;}
    std::string DocumentsPath() const;
    bool Mounted(char drive) const;
    const std::vector<Entry>& Entries() const {return entries_;}
    size_t Selected() const {return selected_;}
    void Select(size_t index);
    void Move(int delta);
    void Page(int delta,size_t rows);
    bool OpenSelected(ShellLaunchRequest&);
    bool OpenPath(const std::string& guestPath,ShellLaunchRequest&);
    bool CanOpenSelected() const;
    bool ResolveExecutable(const std::string& guestPath,ShellLaunchRequest&);
    bool ResolveCatalogExecutable(const std::string& nativePath,ShellLaunchRequest&);
    void CycleSort();
    Sort Ordering() const {return sort_;}
    bool ReadOnly() const {return readOnly_;}
    bool Truncated() const {return truncated_;}
    size_t Errors() const {return errors_;}
    size_t Scanned() const {return scanned_;}
    Error LastError() const {return error_;}
    static bool IsExecutable(const std::string& path);
    static const char16_t* ErrorText(Error);
private:
    struct Location {std::string path,selectedPath;};
    struct Listing {
        std::vector<Entry> entries;
        bool truncated=false,readOnly=false;
        size_t errors=0,scanned=0;
    };
    bool Read(const std::string& path,Listing&);
    bool Load(const std::string& path,const std::string& selectedPath);
    void SortEntries(const std::string& selectedPath);
    std::string SelectedPath() const;
    bool Fail(Error error) {error_=error;return false;}
    bool FailErrno();
    rt::Vfs vfs_;
    bool ready_=false,readOnly_=false,truncated_=false;
    std::string path_;
    std::vector<Entry> entries_;
    std::vector<Location> history_;
    size_t selected_=0,errors_=0,scanned_=0,itemLimit_,scanLimit_;
    Error error_=Error::None;
    Sort sort_=Sort::Name;
};
} // namespace ui
