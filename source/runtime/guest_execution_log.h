#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace rt {
// Frontend lifetime scope. Bounded guest stdout/stderr, never a frame trace.
class GuestExecutionLog {
public:
    GuestExecutionLog(const char* path,const char* exe,const char* layer,const char* revision="unknown");
    ~GuestExecutionLog();
    GuestExecutionLog(const GuestExecutionLog&)=delete;
    GuestExecutionLog& operator=(const GuestExecutionLog&)=delete;
    void Complete(const char* reason,uint32_t exitCode,uint64_t steps);
    bool Available() const {return file_!=nullptr;}
    static constexpr size_t ConsoleLimit=16*1024;
private:
    static void Capture(const char* data,size_t size,bool isStderr);
    static GuestExecutionLog* active_;
    std::FILE* file_=nullptr;
    GuestExecutionLog* previous_=nullptr;
    void (*previousWriter_)(const char*,size_t,bool)=nullptr;
    size_t consoleBytes_=0;
    bool truncated_=false;
};
}
