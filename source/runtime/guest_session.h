#pragma once
#include "process.h"
#include "audio_output.h"

namespace win32 {class LegacySession;namespace user {class GuiSession;}}

namespace rt {
// Top-level sequential guest scope. Declare before GuestExecution and renderer
// cleanup: native continuations and presentation must drain before its teardown.
// This owns Process/memory and both USER/GDI facades, including legacy/MFC caches.
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
    AudioScope audio_;
    std::unique_ptr<win32::user::GuiSession> gui_;
    std::unique_ptr<win32::LegacySession> legacy_;
};
} // namespace rt
