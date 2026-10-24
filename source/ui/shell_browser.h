#pragma once
#include "../runtime/files.h"
#include "../runtime/command_line.h"
#include <string>
#include <vector>

namespace ui {
struct ShellLaunchRequest {
    std::string guestPath,nativePath,commandLine;
    std::vector<std::string> arguments;
    void Clear() {guestPath.clear();nativePath.clear();commandLine.clear();arguments.clear();}
};
// Independent host VFS metadata over the same F1 roots as each GuestSession.
// No global cwd, borrowed Process or file handles survive an operation.
class ShellBrowser {
public:
    static constexpr size_t MaxItems=256,MaxScanned=4096,MaxHistory=32,MaxAssociations=32;
    enum class Kind {Drive,Directory,File};
    enum class Error {None,NotReady,InvalidPath,NotFound,NotDirectory,AccessDenied,ReadFailed,UnsupportedFile,ResolutionLimit,InvalidCommand,AssociationLimit,MissingApplication,AlreadyExists,ReadOnly,NotEmpty,Busy,OperationFailed};
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
    bool CanChooseApplication() const;
    std::string SelectedDocument() const;
    const std::string& UnassociatedDocument() const {return unassociatedDocument_;}
    std::string ApplicationFor(const std::string& path) const;
    bool OpenWith(const std::string& document,const std::string& application,ShellLaunchRequest&);
    bool RunCommand(const std::string& command,ShellLaunchRequest&);
    bool ResolveExecutable(const std::string& guestPath,ShellLaunchRequest&);
    bool ResolveCatalogExecutable(const std::string& nativePath,ShellLaunchRequest&);
    // A successful mutation is never reported as failed merely because a later
    // listing refresh failed. Callers must not retry AppliedViewStale.
    enum class MutationResult {Failed,Applied,AppliedViewStale};
    MutationResult CreateFolder(const std::string& name);
    MutationResult CopyHere(const std::string& source,const std::string& name); // no overwrite
    MutationResult RenameSelected(const std::string& name);
    MutationResult DeleteSelected(); // permanent; UI must confirm before calling
    bool SelectedProperties(rt::FileProperties&);
    int OperationErrno() const {return operationErrno_;}
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
    struct Association {std::string extension,application;};
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
    static bool ValidLeaf(const std::string&);
    MutationResult FinishMutation(bool ok,const std::string& selected);
    int operationErrno_=0;
    static std::string Extension(const std::string&);
    bool AttachArguments(const std::vector<std::string>&,ShellLaunchRequest&);
    bool Document(const std::string& path,std::string& full);
    rt::Vfs vfs_;
    bool ready_=false,readOnly_=false,truncated_=false;
    std::string path_,unassociatedDocument_;
    std::vector<Entry> entries_;
    std::vector<Location> history_;
    std::vector<Association> associations_;
    size_t selected_=0,errors_=0,scanned_=0,itemLimit_,scanLimit_;
    Error error_=Error::None;
    Sort sort_=Sort::Name;
};
} // namespace ui
