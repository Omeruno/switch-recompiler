#pragma once
// First-pass x86 Windows SEH dispatcher for a single guest thread.
// No host exceptions; dispatches handlers from FS:[0] as interpreted x86.
#include <cstdint>
#include "../cpu/cpu_state.h"
#include "../cpu/guest_memory.h"

namespace win32 {
namespace seh {

enum class DispatchResult { Handled, Unhandled, InvalidChain, Noncontinuable };

DispatchResult Dispatch(cpu::CpuState& state, cpu::IGuestMemory& mem,
                        uint32_t teb, uint32_t code, uint32_t flags,
                        uint32_t exceptionAddress, const uint32_t* params,
                        uint32_t paramCount);
} // namespace seh
} // namespace win32
