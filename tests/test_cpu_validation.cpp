#include "source/runtime/cpu_validation.h"
#include "source/cpu/guest_memory.h"
#include "source/cpu/code_storage.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
std::string Read(const char* path){std::ifstream f(path);return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char** argv){
    assert(argc==3);const char* build=std::getenv("GITHUB_SHA");if(!build)build="local-test";
    const bool pass=rt::RunCpuValidation(argv[1],argv[2],build,nullptr,nullptr,"CI host/QEMU validation");
    const auto report=Read(argv[2]);assert(report.find("Platform: HOST_OR_QEMU_NOT_SWITCH")!=std::string::npos);
    if(cpu::PagedGuestMemory::NativeArchitectureAvailable())assert(pass && report.find("Result: PASS_CPU_BATCH_VALIDATION")!=std::string::npos);
    else assert(!pass && report.find("Result: UNAVAILABLE")!=std::string::npos);
    const std::string cancel=std::string(argv[2])+".cancel",missing=std::string(argv[2])+".missing";
    auto stop=+[](unsigned,uint64_t,void*){return false;};
    assert(!rt::RunCpuValidation(argv[1],cancel.c_str(),build,stop));assert(Read(cancel.c_str()).find("cancelled")!=std::string::npos);
    assert(!rt::RunCpuValidation("/definitely-missing/native_dispatch.exe",missing.c_str(),build));assert(Read(missing.c_str()).find("missing public fixture")!=std::string::npos);
    assert(!cpu::GlobalCodeStats().images);
    std::puts(cpu::PagedGuestMemory::NativeArchitectureAvailable()?"CPU validation shared runner PASS (19 real ARM64 samples,full checksum/census/quanta/native batch/lifetime checks,cancel+missing-file failures;NOT a Switch hardware result)":"CPU validation shared runner PASS (host fallback explicitly UNAVAILABLE,cancel/missing-file paths;NOT native/Switch acceptance)");
}
