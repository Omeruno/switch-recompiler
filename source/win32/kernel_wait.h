#pragma once
#include "../cpu/guest_memory.h"
#include <vector>
namespace win32 {
// Shared object preflight/consumption for KERNEL32 and USER32 waits.
bool ValidateWaitObjects(cpu::IGuestMemory& memory,const std::vector<uint32_t>& handles,bool all);
bool ReadWaitObjects(cpu::IGuestMemory& memory,uint32_t count,uint32_t array,
                     bool all,uint32_t maximum,bool allowEmpty,std::vector<uint32_t>& handles);
uint32_t PollWaitObjects(const std::vector<uint32_t>& handles,bool all);
uint32_t AlertableObjectWait(cpu::CpuState& state,cpu::IGuestMemory& memory,
    const std::vector<uint32_t>& handles,bool all,uint32_t milliseconds,bool sleepOnly=false,bool enableApcs=true);
}
