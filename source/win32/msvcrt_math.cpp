// msvcrt_math.cpp — msvcrt.dll: математика.
//
// Обычные функции (sin, pow, ...) — cdecl с double на стеке (8 байт),
// результат в ST(0). Функции _CI* (их вызывает код MSVC 6) берут аргументы
// из стека x87 и возвращают результат туда же; _ftol переводит ST(0) в
// 64-битное целое с отбрасыванием дробной части.

#include <cmath>
#include <cstring>

#include "msvcrt.h"

namespace win32 {
namespace crt {
namespace {

using namespace hle;

double ArgD(cpu::CpuState& c, cpu::IGuestMemory& m, int slot) {
    uint64_t b = Arg(c, m, slot) | (static_cast<uint64_t>(Arg(c, m, slot + 1)) << 32);
    double d;
    std::memcpy(&d, &b, 8);
    return d;
}

int Tag(double v) { return v == 0.0 ? 1 : (std::isnan(v) || std::isinf(v)) ? 2 : 0; }
void Push(cpu::CpuState& c, double v) {
    c.fpuTop = static_cast<uint8_t>((c.fpuTop - 1) & 7);
    c.st[c.fpuTop] = v;
    c.fpuTag = static_cast<uint16_t>((c.fpuTag & ~(3u << (c.fpuTop * 2))) | (static_cast<unsigned>(Tag(v)) << (c.fpuTop * 2)));
}
double PopST(cpu::CpuState& c) {
    double v = c.st[c.fpuTop];
    c.fpuTag = static_cast<uint16_t>(c.fpuTag | (3u << (c.fpuTop * 2)));
    c.fpuTop = static_cast<uint8_t>((c.fpuTop + 1) & 7);
    return v;
}
HostCallOutcome RetD(cpu::CpuState& c, double v) { Push(c, v); return Ret(0); }

// Ошибки области определения — errno, как в msvcrt
double Check(cpu::IGuestMemory& m, double r, double x) {
    if (std::isnan(r) && !std::isnan(x)) SetErrno(m, kEDOM);
    else if (std::isinf(r) && !std::isinf(x)) SetErrno(m, kERANGE);
    return r;
}

#define UNARY(name, expr) \
    HostCallOutcome C_##name(cpu::CpuState& c, cpu::IGuestMemory& m) { double x = ArgD(c, m, 0); return RetD(c, Check(m, expr, x)); } \
    [[maybe_unused]] HostCallOutcome CI_##name(cpu::CpuState& c, cpu::IGuestMemory& m) { double x = PopST(c); Push(c, Check(m, expr, x)); return Ret(0); }

UNARY(sin, std::sin(x))
UNARY(cos, std::cos(x))
UNARY(tan, std::tan(x))
UNARY(asin, std::asin(x))
UNARY(acos, std::acos(x))
UNARY(atan, std::atan(x))
UNARY(sinh, std::sinh(x))
UNARY(cosh, std::cosh(x))
UNARY(tanh, std::tanh(x))
UNARY(exp, std::exp(x))
UNARY(log, std::log(x))
UNARY(log10, std::log10(x))
UNARY(sqrt, std::sqrt(x))
UNARY(fabs, std::fabs(x))
UNARY(floor, std::floor(x))
UNARY(ceil, std::ceil(x))
UNARY(logb, std::logb(x))

HostCallOutcome C_pow(cpu::CpuState& c, cpu::IGuestMemory& m) { double x = ArgD(c, m, 0), y = ArgD(c, m, 2); return RetD(c, Check(m, std::pow(x, y), x)); }
HostCallOutcome C_atan2(cpu::CpuState& c, cpu::IGuestMemory& m) { return RetD(c, std::atan2(ArgD(c, m, 0), ArgD(c, m, 2))); }
HostCallOutcome C_fmod(cpu::CpuState& c, cpu::IGuestMemory& m) { return RetD(c, std::fmod(ArgD(c, m, 0), ArgD(c, m, 2))); }
HostCallOutcome C_hypot(cpu::CpuState& c, cpu::IGuestMemory& m) { return RetD(c, std::hypot(ArgD(c, m, 0), ArgD(c, m, 2))); }
HostCallOutcome C_copysign(cpu::CpuState& c, cpu::IGuestMemory& m) { return RetD(c, std::copysign(ArgD(c, m, 0), ArgD(c, m, 2))); }
HostCallOutcome C_nextafter(cpu::CpuState& c, cpu::IGuestMemory& m) { return RetD(c, std::nextafter(ArgD(c, m, 0), ArgD(c, m, 2))); }
HostCallOutcome C_chgsign(cpu::CpuState& c, cpu::IGuestMemory& m) { return RetD(c, -ArgD(c, m, 0)); }
// double ldexp(double, int); double _scalb(double, long)
HostCallOutcome C_ldexp(cpu::CpuState& c, cpu::IGuestMemory& m) { return RetD(c, std::ldexp(ArgD(c, m, 0), static_cast<int32_t>(Arg(c, m, 2)))); }
// double frexp(double, int*)
HostCallOutcome C_frexp(cpu::CpuState& c, cpu::IGuestMemory& m) {
    int e = 0;
    double r = std::frexp(ArgD(c, m, 0), &e);
    m.Write32(Arg(c, m, 2), static_cast<uint32_t>(e));
    return RetD(c, r);
}
// double modf(double, double*)
HostCallOutcome C_modf(cpu::CpuState& c, cpu::IGuestMemory& m) {
    double ip;
    double r = std::modf(ArgD(c, m, 0), &ip);
    uint64_t b;
    std::memcpy(&b, &ip, 8);
    m.Write32(Arg(c, m, 2), static_cast<uint32_t>(b));
    m.Write32(Arg(c, m, 2) + 4, static_cast<uint32_t>(b >> 32));
    return RetD(c, r);
}
HostCallOutcome C_isnan(cpu::CpuState& c, cpu::IGuestMemory& m) { return Ret(std::isnan(ArgD(c, m, 0)) ? 1 : 0); }
HostCallOutcome C_finite(cpu::CpuState& c, cpu::IGuestMemory& m) { return Ret(std::isfinite(ArgD(c, m, 0)) ? 1 : 0); }
// int _fpclass(double): _FPCLASS_SNAN 1, QNAN 2, NINF 4, NN 8, ND 16, NZ 32, PZ 64, PD 128, PN 256, PINF 512
HostCallOutcome C_fpclass(cpu::CpuState& c, cpu::IGuestMemory& m) {
    double x = ArgD(c, m, 0);
    uint64_t b;
    std::memcpy(&b, &x, 8);
    bool neg = std::signbit(x);
    if (std::isnan(x)) return Ret((b & 0x0008000000000000ull) ? 2 : 1);
    if (std::isinf(x)) return Ret(neg ? 4 : 512);
    if (x == 0.0) return Ret(neg ? 32 : 64);
    if (std::fpclassify(x) == FP_SUBNORMAL) return Ret(neg ? 16 : 128);
    return Ret(neg ? 8 : 256);
}

// _CI* с двумя аргументами: ST(1) — первый, ST(0) — второй
HostCallOutcome CI_pow(cpu::CpuState& c, cpu::IGuestMemory& m) { double y = PopST(c), x = PopST(c); Push(c, Check(m, std::pow(x, y), x)); return Ret(0); }
HostCallOutcome CI_atan2(cpu::CpuState& c, cpu::IGuestMemory&) { double x = PopST(c), y = PopST(c); Push(c, std::atan2(y, x)); return Ret(0); }
HostCallOutcome CI_fmod(cpu::CpuState& c, cpu::IGuestMemory&) { double y = PopST(c), x = PopST(c); Push(c, std::fmod(x, y)); return Ret(0); }

// _ftol: ST(0) -> EDX:EAX с отбрасыванием дробной части (и снятием со стека)
HostCallOutcome C_ftol(cpu::CpuState& c, cpu::IGuestMemory&) {
    double v = PopST(c);
    int64_t r;
    if (std::isnan(v) || v >= 9223372036854775808.0 || v < -9223372036854775808.0) r = INT64_MIN;
    else r = static_cast<int64_t>(v);
    c.regs[2] = static_cast<uint32_t>(static_cast<uint64_t>(r) >> 32);
    return Ret(static_cast<uint32_t>(r));
}
// _ftol2_sse: то же, но значение может быть и в ST(0) — компилятор кладёт его туда
HostCallOutcome C_ftol2(cpu::CpuState& c, cpu::IGuestMemory& m) { return C_ftol(c, m); }

// _adj_fdiv_* (обход ошибки FDIV Pentium): у нас _adjust_fdiv = 0, и
// компилятор их не вызывает; если вызовут — это обычное деление.
HostCallOutcome C_adj_fdiv_m64(cpu::CpuState& c, cpu::IGuestMemory& m) {
    // fdiv qword [arg]: ST0 /= m64. Аргумент — 8 байт на стеке (stdcall)
    double d = ArgD(c, m, 0);
    c.st[c.fpuTop] /= d;
    return Ret(0);
}

#define CDECL_ENTRY(name, fn) {"msvcrt.dll", name, 0, CallConv::Cdecl, &fn}
#define BOTH(name) CDECL_ENTRY(#name, C_##name), CDECL_ENTRY("_CI" #name, CI_##name)
const HostFunctionEntry kEntries[] = {
    BOTH(sin), BOTH(cos), BOTH(tan), BOTH(asin), BOTH(acos), BOTH(atan), BOTH(sinh), BOTH(cosh), BOTH(tanh),
    BOTH(exp), BOTH(log), BOTH(log10), BOTH(sqrt),
    CDECL_ENTRY("fabs", C_fabs), CDECL_ENTRY("floor", C_floor), CDECL_ENTRY("ceil", C_ceil), CDECL_ENTRY("_logb", C_logb),
    CDECL_ENTRY("pow", C_pow), CDECL_ENTRY("_CIpow", CI_pow),
    CDECL_ENTRY("atan2", C_atan2), CDECL_ENTRY("_CIatan2", CI_atan2),
    CDECL_ENTRY("fmod", C_fmod), CDECL_ENTRY("_CIfmod", CI_fmod),
    CDECL_ENTRY("_hypot", C_hypot), CDECL_ENTRY("hypot", C_hypot), CDECL_ENTRY("_copysign", C_copysign),
    CDECL_ENTRY("_nextafter", C_nextafter), CDECL_ENTRY("_chgsign", C_chgsign), CDECL_ENTRY("ldexp", C_ldexp),
    CDECL_ENTRY("_scalb", C_ldexp), CDECL_ENTRY("frexp", C_frexp), CDECL_ENTRY("modf", C_modf),
    CDECL_ENTRY("_isnan", C_isnan), CDECL_ENTRY("_finite", C_finite), CDECL_ENTRY("_fpclass", C_fpclass),
    CDECL_ENTRY("_ftol", C_ftol), CDECL_ENTRY("_ftol2", C_ftol2), CDECL_ENTRY("_ftol2_sse", C_ftol2),
    {"msvcrt.dll", "_adj_fdiv_m64", 8, CallConv::Stdcall, &C_adj_fdiv_m64},
};
const bool kRegistered = RegisterHostFunctions(kEntries, sizeof(kEntries) / sizeof(kEntries[0]));

} // namespace
} // namespace crt
} // namespace win32
