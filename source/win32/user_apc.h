#pragma once
#include "../cpu/guest_memory.h"
#include "../cpu/cpu_state.h"
#include "../runtime/process.h"
namespace win32 {
class AlertableScope {
public:
    explicit AlertableScope(bool enabled);
    ~AlertableScope();
    AlertableScope(const AlertableScope&)=delete;
    AlertableScope& operator=(const AlertableScope&)=delete;
private:
    rt::GuestThread* thread_;
    bool previous_;
};
bool DispatchUserApcs(cpu::CpuState& state,cpu::IGuestMemory& memory);
}
