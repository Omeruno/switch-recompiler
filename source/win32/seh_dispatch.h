#pragma once
// First-pass x86 Windows SEH dispatcher for a single guest thread.
// No host exceptions; dispatches handlers from FS:[0] as interpreted x86.
#include <cstdint>
#include "../cpu/cpu_state.h"
#include "../cpu/guest_memory.h"
#include "../cpu/interpreter.h"

namespace win32 {
namespace seh {

enum class DispatchResult { Handled, Unhandled, InvalidChain, Noncontinuable };

enum class UnwindResult { Completed, InvalidTarget, InvalidChain, UnsupportedDisposition };

// x86 RtlUnwind subset: target frame and continuation in the current stack.
UnwindResult UnwindToFrame(cpu::CpuState& state, cpu::IGuestMemory& mem,
                           uint32_t teb, uint32_t targetFrame, uint32_t targetIp,
                           uint32_t exceptionRecord, uint32_t returnValue);

// Shared entry point for faults raised by the interpreter (not just KERNEL32).
DispatchResult DispatchCpuFault(cpu::CpuState& state, cpu::IGuestMemory& mem,
                                uint32_t teb, const cpu::CpuException& fault);

DispatchResult Dispatch(cpu::CpuState& state, cpu::IGuestMemory& mem,
                        uint32_t teb, uint32_t code, uint32_t flags,
                        uint32_t exceptionAddress, const uint32_t* params,
                        uint32_t paramCount);
} // namespace seh
} // namespace win32
