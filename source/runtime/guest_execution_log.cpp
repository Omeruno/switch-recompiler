#include "guest_execution_log.h"
#include "../win32/host_functions.h"
#include <algorithm>
#include <cerrno>

namespace rt {
namespace {
struct PreserveErrno {int saved=errno;~PreserveErrno(){errno=saved;}};
}
GuestExecutionLog* GuestExecutionLog::active_=nullptr;
GuestExecutionLog::GuestExecutionLog(const char* path,const char* exe,const char* layer,const char* revision) {
    PreserveErrno preserve;
    file_=std::fopen(path,"w");
    if(!file_)return;
    std::fprintf(file_,"switch-recompiler execution log v1\nEXE: %s\nWindow layer: %s\nBuild: %s\nState: RUNNING\n",
        exe?exe:"?",layer?layer:"?",revision?revision:"unknown");
    std::fflush(file_);
    previous_=active_;previousWriter_=win32::g_consoleWriteImpl;
    active_=this;win32::g_consoleWriteImpl=&Capture;
}
GuestExecutionLog::~GuestExecutionLog() {
    PreserveErrno preserve;
    if(!file_)return;
    win32::g_consoleWriteImpl=previousWriter_;active_=previous_;
    std::fclose(file_);
}
void GuestExecutionLog::Capture(const char* data,size_t size,bool isStderr) {
    PreserveErrno preserve;
    auto* log=active_;
    if(!log || !size || log->truncated_)return;
    const size_t count=std::min(size,ConsoleLimit-log->consoleBytes_);
    if(count) {
        std::fprintf(log->file_,"[%s] ",isStderr?"stderr":"stdout");
        std::fwrite(data,1,count,log->file_);log->consoleBytes_+=count;
    }
    if(count<size && !log->truncated_) {
        std::fprintf(log->file_,"\n[guest console truncated after 16384 bytes]\n");log->truncated_=true;
    }
    std::fflush(log->file_);
}
void GuestExecutionLog::Complete(const char* reason,uint32_t exitCode,uint64_t steps) {
    PreserveErrno preserve;
    if(!file_)return;
    std::fprintf(file_,"\nState: STOPPED\nReason: %s\nGuest exit code: 0x%08x\nSteps: %llu\n",
        reason?reason:"?",exitCode,static_cast<unsigned long long>(steps));
    std::fflush(file_);
}
}
