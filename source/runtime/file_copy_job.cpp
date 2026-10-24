#include "file_copy_job.h"
#include <atomic>
#include <cerrno>
#include <cstdio>
namespace rt {
void FileCopyJob::Cleanup() {
    if(out_>=0){io_->Close(out_);out_=-1;}
    if(in_>=0){io_->Close(in_);in_=-1;}
    if(ownedStage_){io_->Remove(stage_);ownedStage_=false;}
}
bool FileCopyJob::Fail(int error) {
    error_=error?error:EIO;Cleanup();state_=State::Failed;errno=error_;return false;
}
void FileCopyJob::Cancel() {
    if(!Running())return;
    Cleanup();state_=State::Cancelled;error_=ECANCELED;
}
bool FileCopyJob::CheckDestination() {
    struct stat dst{};
    if(stat(destination_.c_str(),&dst)!=0)return errno==ENOENT;
    if(source_==destination_ || (sourceStat_.st_ino && sourceStat_.st_ino==dst.st_ino && sourceStat_.st_dev==dst.st_dev)){errno=EINVAL;return false;}
    if(failIfExists_){errno=EEXIST;return false;}
    if(!S_ISREG(dst.st_mode) || !(dst.st_mode&S_IWUSR)){errno=EACCES;return false;}
    return true;
}
bool FileCopyJob::Start(const Vfs& vfs,const std::string& from,const std::string& to,bool failIfExists,size_t* budget,FileCopyIo* io) {
    Cancel();vfs_=vfs;io_=io?io:&native_;state_=State::Running;error_=0;
    written_=initialSize_=0;count_=offset_=0;phase_=Phase::Read;
    failIfExists_=failIfExists;limited_=budget!=nullptr;budget_=budget?*budget:0;
    if(from.empty())return Fail(EINVAL);
    source_=vfs_.ToHost(from,false,limited_?&budget_:nullptr);
    const bool resolved=!source_.empty() && vfs_.MutationPath(to,destination_,limited_?&budget_:nullptr);
    if(budget)*budget=budget_;
    if(!resolved)return Fail(errno);
    if(stat(source_.c_str(),&sourceStat_)!=0)return Fail(errno);
    if(!S_ISREG(sourceStat_.st_mode))return Fail(EACCES);
    initialSize_=sourceStat_.st_size>0?uint64_t(sourceStat_.st_size):0;
    if(!CheckDestination())return Fail(errno);
    in_=io_->OpenRead(source_);if(in_<0)return Fail(errno);
    static std::atomic<uint32_t> serial{0};
    const auto parent=destination_.substr(0,destination_.find_last_of('/')+1);
    for(unsigned attempt=0;attempt<64;++attempt) {
        char leaf[40];std::snprintf(leaf,sizeof(leaf),".sr-copy-%08x.tmp",unsigned(serial.fetch_add(1)));
        stage_=parent+leaf;if(stage_==source_ || stage_==destination_)continue;
        out_=io_->CreateExclusive(stage_);
        if(out_>=0){ownedStage_=true;return true;}
        if(errno!=EEXIST)return Fail(errno);
    }
    return Fail(EEXIST);
}
FileCopyJob::State FileCopyJob::Step() {
    if(!Running())return state_;
    switch(phase_) {
    case Phase::Read: {
        const auto n=io_->Read(in_,buffer_,sizeof(buffer_));
        if(n<0){if(errno!=EINTR)Fail(errno);break;}
        if(!n){phase_=Phase::Flush;break;}
        count_=size_t(n);offset_=0;phase_=Phase::Write;break;
    }
    case Phase::Write: {
        const auto n=io_->Write(out_,buffer_+offset_,count_-offset_);
        if(n<0){if(errno!=EINTR)Fail(errno);break;}
        if(!n){Fail(EIO);break;}
        offset_+=size_t(n);written_+=uint64_t(n);
        if(offset_==count_)phase_=Phase::Read;
        break;
    }
    case Phase::Flush:if(io_->Flush(out_)!=0)Fail(errno);else phase_=Phase::CloseOut;break;
    case Phase::CloseOut: {
        const int fd=out_;out_=-1;if(io_->Close(fd)!=0)Fail(errno);else phase_=Phase::CloseIn;break;
    }
    case Phase::CloseIn: {
        const int fd=in_;in_=-1;if(io_->Close(fd)!=0)Fail(errno);else phase_=Phase::Publish;break;
    }
    case Phase::Publish:
        // No yield between the final no-replace check and rename, even when
        // a guest created a destination while earlier chunks were being copied.
        if(!CheckDestination()){Fail(errno);break;}
        if(io_->Rename(stage_,destination_)!=0)Fail(errno);
        else {ownedStage_=false;state_=State::Done;}break;
    }
    return state_;
}
} // namespace rt
