#include "source/runtime/guest_execution_log.h"
#include "source/win32/host_functions.h"
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>

static unsigned previousCalls;
static void Previous(const char*,size_t,bool){++previousCalls;}
static std::string Read(const std::string& path) {
    std::ifstream file(path);return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
int main() {
    const std::string path="/tmp/switch-execution-log-"+std::to_string(getpid());
    win32::g_consoleWriteImpl=&Previous;
    {
        rt::GuestExecutionLog outer(path.c_str(),"acceptance.exe","new","test-revision");assert(outer.Available());
        errno=ERANGE;win32::g_consoleWriteImpl("FAIL input line 67\n",19,false);assert(errno==ERANGE);
        assert(Read(path).find("FAIL input line 67")!=std::string::npos); // flushed before guest exit
        {
            rt::GuestExecutionLog inner((path+"-inner").c_str(),"nested.exe","legacy");
            win32::g_consoleWriteImpl("inner\n",6,true);inner.Complete("explicit exit",3,41);
        }
        win32::g_consoleWriteImpl("outer again\n",12,false);
        {
            errno=EACCES;rt::GuestExecutionLog missing((path+"/missing").c_str(),"bad","new");assert(!missing.Available() && errno==EACCES);
            win32::g_consoleWriteImpl("still outer\n",12,false);
        }
        std::string large(rt::GuestExecutionLog::ConsoleLimit+4096,'X');
        win32::g_consoleWriteImpl(large.data(),large.size(),false);
        win32::g_consoleWriteImpl("hidden",6,true);
        outer.Complete("instruction budget exhausted",1,100000000);
    }
    assert(win32::g_consoleWriteImpl==&Previous && !previousCalls);
    win32::g_consoleWriteImpl("restored",8,false);assert(previousCalls==1);
    const auto outer=Read(path),inner=Read(path+"-inner");
    assert(outer.find("Window layer: new")!=std::string::npos && outer.find("Build: test-revision")!=std::string::npos);
    assert(outer.find("outer again")!=std::string::npos && outer.find("still outer")!=std::string::npos);
    assert(outer.find("legacy")==std::string::npos && outer.find("hidden")==std::string::npos);
    auto trunc=outer.find("[guest console truncated");assert(trunc!=std::string::npos);
    assert(outer.find("[guest console truncated",trunc+1)==std::string::npos);
    assert(outer.size()<rt::GuestExecutionLog::ConsoleLimit+1024);
    assert(outer.find("State: STOPPED")!=std::string::npos && outer.find("100000000")!=std::string::npos);
    assert(inner.find("Window layer: legacy")!=std::string::npos && inner.find("[stderr] inner")!=std::string::npos);
    assert(inner.find("0x00000003")!=std::string::npos);
    std::remove(path.c_str());std::remove((path+"-inner").c_str());win32::g_consoleWriteImpl=nullptr;
    puts("PASSED: flushed bounded execution log, failure text/status/budget, nested scopes and console restoration");
}
