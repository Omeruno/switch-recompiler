#pragma once
// ABI-only definitions for 32-bit Windows SEH; guest pointers are uint32_t,
// never native host pointers. This does NOT implement handler dispatch.
#include <cstddef>
#include <cstdint>

namespace win32 {
namespace seh {

constexpr uint32_t kEndOfChain = 0xffffffffu;
constexpr uint32_t kMaximumExceptionParameters = 15;
constexpr uint32_t kExceptionNoncontinuable = 0x1;
constexpr uint32_t kExceptionUnwinding = 0x2;
constexpr uint32_t kExceptionExitUnwind = 0x4;
constexpr uint32_t kExceptionNestedCall = 0x10;
constexpr uint32_t kExceptionTargetUnwind = 0x20;
constexpr uint32_t kExceptionCollidedUnwind = 0x40;
constexpr uint32_t kDelayLoadModuleNotFound = 0xC06D007Eu;
constexpr uint32_t kDelayLoadProcNotFound = 0xC06D007Fu;

enum class Disposition : int32_t {
    ContinueExecution = 0,
    ContinueSearch = 1,
    NestedException = 2,
    CollidedUnwind = 3,
};

#pragma pack(push, 4)
struct RegistrationRecord32 {
    uint32_t next;
    uint32_t handler;
};
struct ExceptionRecord32 {
    uint32_t code;
    uint32_t flags;
    uint32_t record;
    uint32_t address;
    uint32_t parameterCount;
    uint32_t information[kMaximumExceptionParameters];
};
#pragma pack(pop)
static_assert(sizeof(RegistrationRecord32) == 8, "x86 SEH registration ABI");
static_assert(offsetof(RegistrationRecord32, handler) == 4, "x86 SEH handler offset");
static_assert(sizeof(ExceptionRecord32) == 80, "x86 EXCEPTION_RECORD ABI");
static_assert(offsetof(ExceptionRecord32, address) == 12, "x86 exception address");
static_assert(offsetof(ExceptionRecord32, information) == 20, "x86 exception parameters");

} // namespace seh
} // namespace win32
