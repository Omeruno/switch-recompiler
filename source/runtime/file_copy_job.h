#pragma once
#include "files.h"
#include "file_copy_io.h"
#include <sys/stat.h>
namespace rt {
// Single-owner resumable copy. No thread, guest pointers or borrowed VFS/cwd.
class FileCopyJob {
public:
    enum class State {Idle,Running,Done,Failed,Cancelled};
    FileCopyJob()=default;
    ~FileCopyJob(){Cancel();}
    FileCopyJob(const FileCopyJob&)=delete;
    FileCopyJob& operator=(const FileCopyJob&)=delete;
    bool Start(const Vfs&,const std::string& from,const std::string& to,bool failIfExists=true,
               size_t* budget=nullptr,FileCopyIo* io=nullptr);
    // One data syscall or one finalization phase; EINTR resumes on next step.
    State Step();
    void Cancel();
    State Status() const {return state_;}
    bool Running() const {return state_==State::Running;}
    int Error() const {return error_;}
    uint64_t BytesWritten() const {return written_;}
    uint64_t InitialSize() const {return initialSize_;}
    size_t RemainingBudget() const {return budget_;}
private:
    enum class Phase {Read,Write,Flush,CloseOut,CloseIn,Publish};
    bool CheckDestination();
    bool Fail(int);
    void Cleanup();
    Vfs vfs_;
    FileCopyIo native_;
    FileCopyIo* io_=&native_;
    std::string source_,destination_,stage_;
    struct stat sourceStat_{};
    int in_=-1,out_=-1,error_=0;
    bool ownedStage_=false,failIfExists_=true,limited_=false;
    size_t budget_=0,count_=0,offset_=0;
    uint64_t written_=0,initialSize_=0;
    char buffer_[8192];
    State state_=State::Idle;
    Phase phase_=Phase::Read;
};
} // namespace rt
