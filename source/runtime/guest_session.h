#pragma once
#include "process.h"

namespace rt {
// Top-level sequential guest scope. Declare before GuestExecution and renderer
// cleanup: native continuations and presentation must drain before its teardown.
// This owns core Process/memory; GUI/legacy singleton reset is a separate gate.
class GuestSession {
public:
    GuestSession();
    ~GuestSession();
    GuestSession(const GuestSession&)=delete;
    GuestSession& operator=(const GuestSession&)=delete;
    cpu::PagedGuestMemory& Memory() {return memory_;}
    Process& ProcessObject() {return process_;}
private:
    struct Context {
        Process* process=nullptr;
        cpu::IGuestMemory* binding=nullptr;
        cpu::CpuState* guiCpu=nullptr;
        cpu::IGuestMemory* guiMemory=nullptr;
        bool exitRequested=false;
        int32_t exitCode=0;
        uint32_t crt=0,heapBase=0,heapNext=0,mfc=0,teb=0;
    };
    static Context Capture();
    // Capture before Process's compatibility constructor changes import binding.
    Context previous_;
    cpu::PagedGuestMemory memory_;
    Process process_;
};
} // namespace rt
