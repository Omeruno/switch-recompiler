#pragma once
// fp_util.h
//
// Вещественная арифметика с семантикой x86 поверх хостового IEEE 754.
//
// Хост (x86-64 в тестах, ARM64 на Switch) считает базовые операции так же
// побитно, но расходится с x86 в особых случаях:
//  - NaN: x86 возвращает первый NaN-операнд (с поднятым битом «тихий»),
//    а для недопустимых операций — «indefinite» (отрицательный тихий NaN);
//    ARM по умолчанию даёт положительный NaN и другие правила выбора;
//  - перевод в целое при переполнении/NaN: x86 даёт 0x80000000
//    («integer indefinite»), ARM насыщает;
//  - флаги исключений: читаем через <cfenv>, это одинаково на обоих.
// Поэтому эти случаи обрабатываются здесь явно.

#include <cfenv>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace cpu {
namespace fp {

inline uint32_t Bits(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }
inline uint64_t Bits(double d) { uint64_t u; std::memcpy(&u, &d, 8); return u; }
inline float F32(uint32_t u) { float f; std::memcpy(&f, &u, 4); return f; }
inline double F64(uint64_t u) { double d; std::memcpy(&d, &u, 8); return d; }

constexpr uint32_t kIndefinite32 = 0xFFC00000u;
constexpr uint64_t kIndefinite64 = 0xFFF8000000000000ull;

inline bool IsNaN(float f) { return (Bits(f) & 0x7FFFFFFFu) > 0x7F800000u; }
inline bool IsNaN(double d) { return (Bits(d) & 0x7FFFFFFFFFFFFFFFull) > 0x7FF0000000000000ull; }
inline bool IsSNaN(float f) { return IsNaN(f) && !(Bits(f) & 0x00400000u); }
inline bool IsSNaN(double d) { return IsNaN(d) && !(Bits(d) & 0x0008000000000000ull); }
inline float Quiet(float f) { return F32(Bits(f) | 0x00400000u); }
inline double Quiet(double d) { return F64(Bits(d) | 0x0008000000000000ull); }
inline bool IsDenormal(float f) { uint32_t b = Bits(f) & 0x7FFFFFFFu; return b && b < 0x00800000u; }
inline bool IsDenormal(double d) { uint64_t b = Bits(d) & 0x7FFFFFFFFFFFFFFFull; return b && b < 0x0010000000000000ull; }

// Флаги исключений в формате x86 (биты MXCSR / статуса x87)
enum : uint32_t { kIE = 1, kDE = 2, kZE = 4, kOE = 8, kUE = 16, kPE = 32 };

inline uint32_t HostFlagsToX86(int f) {
    uint32_t r = 0;
    if (f & FE_INVALID) r |= kIE;
    if (f & FE_DIVBYZERO) r |= kZE;
    if (f & FE_OVERFLOW) r |= kOE;
    if (f & FE_UNDERFLOW) r |= kUE;
    if (f & FE_INEXACT) r |= kPE;
    return r;
}

// Режим округления x86 (00 ближайшее, 01 вниз, 10 вверх, 11 к нулю)
inline int HostRounding(uint32_t rc) {
    switch (rc & 3) {
        case 1: return FE_DOWNWARD;
        case 2: return FE_UPWARD;
        case 3: return FE_TOWARDZERO;
        default: return FE_TONEAREST;
    }
}

// Выполняет операцию в заданном режиме округления и собирает флаги.
// Применение: fp::Scope s(rc); r = a + b; flags |= s.Flags();
class Scope {
public:
    explicit Scope(uint32_t rc) : rc_(rc & 3) {
        std::feclearexcept(FE_ALL_EXCEPT);
        if (rc_) { saved_ = std::fegetround(); std::fesetround(HostRounding(rc_)); }
    }
    ~Scope() { if (rc_) std::fesetround(saved_); }
    uint32_t Flags() const { return HostFlagsToX86(std::fetestexcept(FE_ALL_EXCEPT)); }
private:
    uint32_t rc_;
    int saved_ = FE_TONEAREST;
};

// Правило выбора NaN для бинарной операции SSE: если есть NaN-операнд,
// результат — первый (dest) NaN, иначе второй; оба тихие. IE — если
// какой-то из них сигнальный. Возвращает true, если результат уже готов.
template <typename T>
inline bool PropagateNaN(T a, T b, T& out, uint32_t& flags) {
    bool na = IsNaN(a), nb = IsNaN(b);
    if (!na && !nb) return false;
    if (IsSNaN(a) || IsSNaN(b)) flags |= kIE;
    out = na ? Quiet(a) : Quiet(b);
    return true;
}

template <typename T> inline T Indefinite();
template <> inline float Indefinite<float>() { return F32(kIndefinite32); }
template <> inline double Indefinite<double>() { return F64(kIndefinite64); }

// Базовая операция SSE с x86-семантикой NaN. op: 0 add 1 sub 2 mul 3 div
template <typename T>
inline T Arith(int op, T a, T b, uint32_t rc, uint32_t& flags) {
    T r;
    if (PropagateNaN(a, b, r, flags)) return r;
    Scope s(rc);
    switch (op) {
        case 0: r = a + b; break;
        case 1: r = a - b; break;
        case 2: r = a * b; break;
        default: r = a / b; break;
    }
    uint32_t f = s.Flags();
    if (IsNaN(r)) { r = Indefinite<T>(); f |= kIE; }
    flags |= f;
    return r;
}

template <typename T>
inline T Sqrt(T a, uint32_t rc, uint32_t& flags) {
    if (IsNaN(a)) { if (IsSNaN(a)) flags |= kIE; return Quiet(a); }
    Scope s(rc);
    T r = std::sqrt(a);
    uint32_t f = s.Flags();
    if (IsNaN(r)) { r = Indefinite<T>(); f |= kIE; }
    flags |= f;
    return r;
}

// Перевод в int32/int64 с x86-семантикой (indefinite при переполнении/NaN).
// truncate: CVTT*; иначе по режиму rc.
template <typename T>
inline int32_t ToInt32(T v, bool truncate, uint32_t rc, uint32_t& flags) {
    if (IsNaN(v)) { flags |= kIE; return INT32_MIN; }
    T r;
    if (truncate) r = std::trunc(v);
    else {
        switch (rc & 3) {
            case 1: r = std::floor(v); break;
            case 2: r = std::ceil(v); break;
            case 3: r = std::trunc(v); break;
            default: r = std::nearbyint(v); break; // хост по умолчанию — к ближайшему чётному
        }
    }
    if (!(r >= static_cast<T>(-2147483648.0) && r < static_cast<T>(2147483648.0))) {
        flags |= kIE;
        return INT32_MIN;
    }
    if (r != v) flags |= kPE;
    return static_cast<int32_t>(r);
}

template <typename T>
inline int64_t ToInt64(T v, bool truncate, uint32_t rc, uint32_t& flags) {
    if (IsNaN(v)) { flags |= kIE; return INT64_MIN; }
    T r;
    if (truncate) r = std::trunc(v);
    else {
        switch (rc & 3) {
            case 1: r = std::floor(v); break;
            case 2: r = std::ceil(v); break;
            case 3: r = std::trunc(v); break;
            default: r = std::nearbyint(v); break;
        }
    }
    if (!(r >= static_cast<T>(-9223372036854775808.0) && r < static_cast<T>(9223372036854775808.0))) {
        flags |= kIE;
        return INT64_MIN;
    }
    if (r != v) flags |= kPE;
    return static_cast<int64_t>(r);
}

inline int16_t ToInt16(double v, uint32_t rc, uint32_t& flags, bool truncate = false) {
    uint32_t f = 0;
    int32_t i = ToInt32(v, truncate, rc, f);
    if ((f & kIE) || i < -32768 || i > 32767) { flags |= kIE; return INT16_MIN; }
    flags |= f;
    return static_cast<int16_t>(i);
}

// float -> double без участия хоста: SNaN остаётся сигнальным (x86-хост
// при обычном приведении «гасит» его и поднимает флаг).
inline double FloatToDoubleExact(float f) {
    uint32_t b = Bits(f);
    if ((b & 0x7F800000u) == 0x7F800000u && (b & 0x007FFFFFu)) {
        return F64((static_cast<uint64_t>(b & 0x80000000u) << 32) | 0x7FF0000000000000ull |
                   (static_cast<uint64_t>(b & 0x007FFFFFu) << 29));
    }
    return static_cast<double>(f);
}

// --- 80-битный формат x87 <-> double ---

inline void DoubleToExt80(double d, uint8_t out[10]) {
    uint64_t b = Bits(d);
    uint16_t sign = static_cast<uint16_t>((b >> 63) << 15);
    int exp = static_cast<int>((b >> 52) & 0x7FF);
    uint64_t frac = b & 0x000FFFFFFFFFFFFFull;
    uint64_t mant;
    uint16_t e;
    if (exp == 0) {
        if (frac == 0) { mant = 0; e = 0; }
        else {
            // денормал double — нормальное число в 80-битном формате
            int shift = __builtin_clzll(frac) - 11;
            mant = frac << (shift + 11);
            e = static_cast<uint16_t>(1 - 1023 + 16383 - shift);
        }
    } else if (exp == 0x7FF) {
        e = 0x7FFF;
        mant = 0x8000000000000000ull | (frac << 11);
    } else {
        e = static_cast<uint16_t>(exp - 1023 + 16383);
        mant = 0x8000000000000000ull | (frac << 11);
    }
    std::memcpy(out, &mant, 8);
    uint16_t se = static_cast<uint16_t>(sign | e);
    std::memcpy(out + 8, &se, 2);
}

inline double Ext80ToDouble(const uint8_t in[10]) {
    uint64_t mant;
    uint16_t se;
    std::memcpy(&mant, in, 8);
    std::memcpy(&se, in + 8, 2);
    bool neg = (se & 0x8000) != 0;
    int e = se & 0x7FFF;
    double r;
    if (e == 0x7FFF) {
        uint64_t frac = (mant & 0x7FFFFFFFFFFFFFFFull) >> 11;
        if ((mant & 0x7FFFFFFFFFFFFFFFull) == 0) r = INFINITY;
        else r = F64(0x7FF0000000000000ull | frac | (frac ? 0 : 0x0008000000000000ull));
    } else if (mant == 0) {
        r = 0.0;
    } else {
        r = std::ldexp(static_cast<double>(mant), e - 16383 - 63);
    }
    return neg ? -r : r;
}

} // namespace fp
} // namespace cpu
