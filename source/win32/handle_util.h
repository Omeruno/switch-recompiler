#pragma once
#include "hle_util.h"
namespace win32 { namespace hle {
inline bool HandleAccess(cpu::IGuestMemory& memory,uint32_t handle,uint32_t access) {
    if(handle==0xfffffffeu && rt::g_process) return true; // current thread
    if(!rt::Handles().Entry(handle)) { SetLastError(memory,6);return false; }
    if(!rt::Handles().HasAccess(handle,access)) { SetLastError(memory,5);return false; }
    return true;
}
inline bool InheritAttribute(cpu::IGuestMemory& memory,uint32_t attributes,uint32_t& flags) {
    flags=0;
    if(!attributes) return true;
    if(!memory.IsRangeMapped(attributes,12) || memory.Read32(attributes)!=12) {
        SetLastError(memory,87);return false;
    }
    // Security descriptors/token ACL checks remain outside the current model.
    flags=memory.Read32(attributes+8) ? rt::kHandleInherit : 0;return true;
}
} }
