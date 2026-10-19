// fuzz_fp.inc — генераторы тестов x87 и MMX/SSE для cpu_fuzz.cpp.

// Адреса операндов в области данных (выровнены на 16)
constexpr uint32_t FA = ORACLE_DATA_BASE + 0x100;
constexpr uint32_t FB = ORACLE_DATA_BASE + 0x110;
constexpr uint32_t FC = ORACLE_DATA_BASE + 0x120;
constexpr uint32_t FD = ORACLE_DATA_BASE + 0x130;

uint64_t InterestingDouble(Rng& r) {
    static const double vals[] = {0.0, -0.0, 1.0, -1.0, 2.0, 0.5, 3.0, 10.0, -7.25, 0.1, 1e10, -1e10, 1e300, -1e300,
                                  1e-300, 123456.789, 2147483647.0, 2147483648.0, -2147483648.0, -2147483649.0,
                                  32767.5, -32768.5, 0.5, 1.5, 2.5, -2.5, 4294967296.0, 9.2e18, -9.3e18, 65535.0};
    switch (r.Below(10)) {
        case 0: return 0x7FF0000000000000ull | (r.Chance(50) ? 0x8000000000000000ull : 0); // ±inf
        case 1: return 0x7FF8000000000000ull | r.Below(1000) | (r.Chance(50) ? 0x8000000000000000ull : 0); // QNaN
        case 2: return 0x7FF0000000000001ull + r.Below(1000);                               // SNaN
        case 3: return static_cast<uint64_t>(r.Below(1u << 20)) | (static_cast<uint64_t>(r.Next() & 0xFFFF) << 20); // денормал
        case 4: case 5: { // случайное в разумном диапазоне
            double d = (static_cast<double>(r.Next()) - 2147483648.0) / (1u << r.Below(20));
            uint64_t u; std::memcpy(&u, &d, 8); return u;
        }
        default: { double d = vals[r.Below(sizeof(vals) / sizeof(vals[0]))]; uint64_t u; std::memcpy(&u, &d, 8); return u; }
    }
}
uint32_t InterestingFloat(Rng& r) {
    static const float vals[] = {0.0f, -0.0f, 1.0f, -1.0f, 2.0f, 0.5f, 3.0f, 10.0f, -7.25f, 0.1f, 1e10f, -1e10f,
                                 3e38f, -3e38f, 1e-38f, 123456.789f, 2147483520.0f, 2147483648.0f, -2147483648.0f,
                                 -2147483904.0f, 1.5f, 2.5f, -2.5f, 16777217.0f};
    switch (r.Below(10)) {
        case 0: return 0x7F800000u | (r.Chance(50) ? 0x80000000u : 0);
        case 1: return 0x7FC00000u | r.Below(1000) | (r.Chance(50) ? 0x80000000u : 0);
        case 2: return 0x7F800001u + r.Below(1000);
        case 3: return r.Below(1u << 23) | (r.Chance(50) ? 0x80000000u : 0);
        case 4: case 5: { float f = (static_cast<float>(r.Next()) - 2147483648.0f) / static_cast<float>(1u << r.Below(20)); uint32_t u; std::memcpy(&u, &f, 4); return u; }
        default: { float f = vals[r.Below(sizeof(vals) / sizeof(vals[0]))]; uint32_t u; std::memcpy(&u, &f, 4); return u; }
    }
}

void PatchDouble(Builder& g, uint32_t addr, uint64_t v) {
    g.Patch(addr, static_cast<uint32_t>(v));
    g.Patch(addr + 4, static_cast<uint32_t>(v >> 32));
}

// Инструкция x87 с операндом [abs] (mod=00 rm=101 disp32)
void FpuMem(Builder& g, uint8_t op, uint8_t reg, uint32_t addr) {
    g.Byte(op);
    g.Byte(static_cast<uint8_t>((reg << 3) | 5));
    g.Imm(addr, 4);
}
void FpuReg(Builder& g, uint8_t op, uint8_t reg, uint8_t i) {
    g.Byte(op);
    g.Byte(static_cast<uint8_t>(0xC0 | (reg << 3) | i));
}

void FpuSetup(Builder& g, Rng& r) {
    g.b_.compareFpu = true;
    g.b_.flagMask = F_ALL | F_DF;
    // Точность 53 бита; режим округления случайный; все исключения замаскированы
    g.b_.t.fpuControl = static_cast<uint16_t>(0x027F | (r.Below(4) << 10));
    PatchDouble(g, FA, InterestingDouble(r));
    PatchDouble(g, FB, InterestingDouble(r));
}

void GenFpuArith(Builder& g, Rng& r, bool) {
    FpuSetup(g, r);
    FpuMem(g, 0xDD, 0, FA); // fld qword [A]
    uint8_t reg = static_cast<uint8_t>(r.Below(8));
    switch (r.Below(4)) {
        case 0: FpuMem(g, 0xDC, reg, FB); g.b_.name = "fpu_m64"; break;       // f?? qword [B]
        case 1: g.Patch(FB, InterestingFloat(r)); FpuMem(g, 0xD8, reg, FB); g.b_.name = "fpu_m32"; break;
        case 2: FpuMem(g, 0xDD, 0, FB); FpuReg(g, 0xD8, reg, 1); g.b_.name = "fpu_reg"; break; // f?? st0, st1
        default: FpuMem(g, 0xDD, 0, FB);
                 if (reg == 2 || reg == 3) FpuReg(g, 0xDC, reg, 1); else FpuReg(g, r.Chance(50) ? 0xDC : 0xDE, reg, 1);
                 g.b_.name = "fpu_regr"; break;
    }
    FpuMem(g, 0xDD, 3, FC); // fstp qword [C]
}

void GenFpuCompare(Builder& g, Rng& r, bool) {
    FpuSetup(g, r);
    FpuMem(g, 0xDD, 0, FA);
    FpuMem(g, 0xDD, 0, FB);
    switch (r.Below(7)) {
        case 0: FpuReg(g, 0xD8, 2, 1); break;        // fcom st1
        case 1: FpuReg(g, 0xDD, 4, 1); break;        // fucom st1
        case 2: FpuReg(g, 0xDB, 6, 1); break;        // fcomi
        case 3: FpuReg(g, 0xDB, 5, 1); break;        // fucomi
        case 4: FpuReg(g, 0xDF, 6, 1); break;        // fcomip
        case 5: g.Byte(0xDE); g.Byte(0xD9); break;   // fcompp
        default: g.Byte(0xDA); g.Byte(0xE9); break;  // fucompp
    }
    g.Byte(0xDF); g.Byte(0xE0);                      // fnstsw ax
    g.b_.name = "fpu_cmp";
}

void GenFpuUnary(Builder& g, Rng& r, bool) {
    FpuSetup(g, r);
    FpuMem(g, 0xDD, 0, FB);
    FpuMem(g, 0xDD, 0, FA);
    static const uint8_t ops[] = {0xE0, 0xE1, 0xE4, 0xE5, 0xFA, 0xFC, 0xF4, 0xF8, 0xF5, 0xFD, 0xE8, 0xEE, 0xEB, 0xC9, 0xC1, 0xF6, 0xF7};
    uint8_t op = ops[r.Below(sizeof(ops))];
    g.Byte(0xD9); g.Byte(op);
    FpuMem(g, 0xDD, 3, FC);
    g.b_.name = "fpu_unary";
}

void GenFpuConvert(Builder& g, Rng& r, bool) {
    FpuSetup(g, r);
    switch (r.Below(6)) {
        case 0: FpuMem(g, 0xDD, 0, FA); FpuMem(g, 0xDB, 3, FC); g.b_.name = "fistp32"; break;
        case 1: FpuMem(g, 0xDD, 0, FA); FpuMem(g, 0xDF, 3, FC); g.b_.name = "fistp16"; break;
        case 2: FpuMem(g, 0xDD, 0, FA); FpuMem(g, 0xDF, 7, FC); g.b_.name = "fistp64"; break;
        case 3: FpuMem(g, 0xDD, 0, FA); FpuMem(g, 0xD9, 3, FC); g.b_.name = "fstp32"; break;
        case 4: { static const uint8_t ld[][2] = {{0xDB, 0}, {0xDF, 0}, {0xDF, 5}};
                  int k = static_cast<int>(r.Below(3));
                  g.Patch(FA, r.Next()); g.Patch(FA + 4, r.Chance(50) ? r.Next() : 0);
                  FpuMem(g, ld[k][0], ld[k][1], FA); FpuMem(g, 0xDD, 3, FC); g.b_.name = "fild"; break; }
        default: FpuMem(g, 0xDD, 0, FA); FpuMem(g, 0xDB, 7, FC); g.b_.name = "fstp80"; break;
    }
}

void GenFpuMisc(Builder& g, Rng& r, bool) {
    FpuSetup(g, r);
    FpuMem(g, 0xDD, 0, FA);
    FpuMem(g, 0xDD, 0, FB);
    switch (r.Below(5)) {
        case 0: { uint8_t k = static_cast<uint8_t>(r.Below(8)); FpuReg(g, k < 4 ? 0xDA : 0xDB, k & 3, 1); g.b_.name = "fcmov"; break; }
        case 1: FpuReg(g, 0xD9, 1, 1); g.b_.name = "fxch"; break;
        case 2: FpuReg(g, 0xD9, 0, 1); g.b_.name = "fldsti"; break;
        case 3: FpuReg(g, 0xDD, 3, 1); g.b_.name = "fstpsti"; break;
        default: FpuReg(g, 0xDD, 0, 1); g.b_.name = "ffree"; break;
    }
    FpuMem(g, 0xDD, 3, FC);
}

const GenEntry kFpuGens[] = {
    {"fpu", GenFpuArith, 10}, {"fpu", GenFpuCompare, 4}, {"fpu", GenFpuUnary, 6},
    {"fpu", GenFpuConvert, 6}, {"fpu", GenFpuMisc, 3},
};

// ===== SSE =====

void SseSetup(Builder& g, Rng& r) {
    g.b_.compareXmm = true;
    for (int i = 0; i < 8; ++i) {
        uint32_t kind = r.Below(3);
        for (int k = 0; k < 4; ++k) {
            if (kind == 0) g.b_.t.xmmInit[i][k] = InterestingFloat(r);
            else if (kind == 1) { if (k % 2 == 0) { uint64_t d = InterestingDouble(r); g.b_.t.xmmInit[i][k] = static_cast<uint32_t>(d); g.b_.t.xmmInit[i][k + 1] = static_cast<uint32_t>(d >> 32); } }
            else g.b_.t.xmmInit[i][k] = RandomValue(r);
        }
    }
    uint32_t mx = 0x1F80 | (r.Below(4) << 13);
    if (r.Chance(10)) mx |= 1u << 6;  // DAZ
    if (r.Chance(10)) mx |= 1u << 15; // FTZ
    g.b_.t.mxcsr = mx;
    // Данные под операнды в памяти: вещественные значения
    for (int k = 0; k < 4; ++k) g.Patch(FA + k * 4, InterestingFloat(r));
    uint64_t d = InterestingDouble(r);
    g.Patch(FB, static_cast<uint32_t>(d)); g.Patch(FB + 4, static_cast<uint32_t>(d >> 32));
    d = InterestingDouble(r);
    g.Patch(FB + 8, static_cast<uint32_t>(d)); g.Patch(FB + 12, static_cast<uint32_t>(d >> 32));
}

// Операнд SSE: регистр или память [abs] в FA/FB (выровнено)
void SseModRM(Builder& g, Rng& r, uint8_t reg, bool allowMem = true, bool regOnly = false) {
    if (regOnly || !allowMem || r.Chance(55)) {
        g.ModRMReg(reg, static_cast<uint8_t>(r.Below(8)));
    } else {
        g.Byte(static_cast<uint8_t>((reg << 3) | 5));
        g.Imm(r.Chance(50) ? FA : FB, 4);
    }
}

struct SseOp { uint8_t pfx; uint8_t op; uint8_t imm; uint8_t form; const char* name; };
// form: 0 xmm,xmm/m  1 только регистры  2 xmm,r/m32  3 r32,xmm/m  4 MMX (mm, mm/m64)  5 imm-сдвиг (только reg)
const SseOp kSseOps[] = {
    {0, 0x58, 0, 0, "addps"}, {0x66, 0x58, 0, 0, "addpd"}, {0xF3, 0x58, 0, 0, "addss"}, {0xF2, 0x58, 0, 0, "addsd"},
    {0, 0x59, 0, 0, "mulps"}, {0x66, 0x59, 0, 0, "mulpd"}, {0xF3, 0x59, 0, 0, "mulss"}, {0xF2, 0x59, 0, 0, "mulsd"},
    {0, 0x5C, 0, 0, "subps"}, {0x66, 0x5C, 0, 0, "subpd"}, {0xF3, 0x5C, 0, 0, "subss"}, {0xF2, 0x5C, 0, 0, "subsd"},
    {0, 0x5E, 0, 0, "divps"}, {0x66, 0x5E, 0, 0, "divpd"}, {0xF3, 0x5E, 0, 0, "divss"}, {0xF2, 0x5E, 0, 0, "divsd"},
    {0, 0x5D, 0, 0, "minps"}, {0x66, 0x5D, 0, 0, "minpd"}, {0xF3, 0x5D, 0, 0, "minss"}, {0xF2, 0x5D, 0, 0, "minsd"},
    {0, 0x5F, 0, 0, "maxps"}, {0x66, 0x5F, 0, 0, "maxpd"}, {0xF3, 0x5F, 0, 0, "maxss"}, {0xF2, 0x5F, 0, 0, "maxsd"},
    {0, 0x51, 0, 0, "sqrtps"}, {0x66, 0x51, 0, 0, "sqrtpd"}, {0xF3, 0x51, 0, 0, "sqrtss"}, {0xF2, 0x51, 0, 0, "sqrtsd"},
    {0, 0xC2, 1, 0, "cmpps"}, {0x66, 0xC2, 1, 0, "cmppd"}, {0xF3, 0xC2, 1, 0, "cmpss"}, {0xF2, 0xC2, 1, 0, "cmpsd"},
    {0, 0x2E, 0, 0, "ucomiss"}, {0x66, 0x2E, 0, 0, "ucomisd"}, {0, 0x2F, 0, 0, "comiss"}, {0x66, 0x2F, 0, 0, "comisd"},
    {0, 0x54, 0, 0, "andps"}, {0x66, 0x55, 0, 0, "andnpd"}, {0, 0x56, 0, 0, "orps"}, {0x66, 0x57, 0, 0, "xorpd"},
    {0, 0x14, 0, 0, "unpcklps"}, {0x66, 0x15, 0, 0, "unpckhpd"}, {0, 0xC6, 1, 0, "shufps"}, {0x66, 0xC6, 1, 0, "shufpd"},
    {0, 0x10, 0, 0, "movups"}, {0xF3, 0x10, 0, 0, "movss"}, {0xF2, 0x10, 0, 0, "movsd"}, {0, 0x28, 0, 0, "movaps"},
    {0, 0x12, 0, 0, "movlps"}, {0, 0x16, 0, 0, "movhps"}, {0x66, 0x12, 0, 0, "movlpd"}, {0xF2, 0x12, 0, 0, "movddup"},
    {0xF3, 0x12, 0, 0, "movsldup"}, {0xF3, 0x16, 0, 0, "movshdup"},
    {0, 0x5A, 0, 0, "cvtps2pd"}, {0x66, 0x5A, 0, 0, "cvtpd2ps"}, {0xF3, 0x5A, 0, 0, "cvtss2sd"}, {0xF2, 0x5A, 0, 0, "cvtsd2ss"},
    {0, 0x5B, 0, 0, "cvtdq2ps"}, {0x66, 0x5B, 0, 0, "cvtps2dq"}, {0xF3, 0x5B, 0, 0, "cvttps2dq"},
    {0xF3, 0xE6, 0, 0, "cvtdq2pd"}, {0x66, 0xE6, 0, 0, "cvttpd2dq"}, {0xF2, 0xE6, 0, 0, "cvtpd2dq"},
    {0xF3, 0x2A, 0, 2, "cvtsi2ss"}, {0xF2, 0x2A, 0, 2, "cvtsi2sd"},
    {0xF3, 0x2C, 0, 3, "cvttss2si"}, {0xF3, 0x2D, 0, 3, "cvtss2si"}, {0xF2, 0x2C, 0, 3, "cvttsd2si"}, {0xF2, 0x2D, 0, 3, "cvtsd2si"},
    {0x66, 0x50, 0, 3, "movmskpd"}, {0, 0x50, 0, 3, "movmskps"}, {0x66, 0xD7, 0, 3, "pmovmskb"},
    {0x66, 0x6E, 0, 2, "movd"}, {0x66, 0x6F, 0, 0, "movdqa"}, {0xF3, 0x6F, 0, 0, "movdqu"}, {0xF3, 0x7E, 0, 0, "movq"},
    {0x66, 0x70, 1, 0, "pshufd"}, {0xF3, 0x70, 1, 0, "pshufhw"}, {0xF2, 0x70, 1, 0, "pshuflw"},
    {0x66, 0x60, 0, 0, "punpcklbw"}, {0x66, 0x61, 0, 0, "punpcklwd"}, {0x66, 0x62, 0, 0, "punpckldq"}, {0x66, 0x6C, 0, 0, "punpcklqdq"},
    {0x66, 0x68, 0, 0, "punpckhbw"}, {0x66, 0x6A, 0, 0, "punpckhdq"}, {0x66, 0x6D, 0, 0, "punpckhqdq"},
    {0x66, 0x63, 0, 0, "packsswb"}, {0x66, 0x67, 0, 0, "packuswb"}, {0x66, 0x6B, 0, 0, "packssdw"},
    {0x66, 0x64, 0, 0, "pcmpgtb"}, {0x66, 0x66, 0, 0, "pcmpgtd"}, {0x66, 0x74, 0, 0, "pcmpeqb"}, {0x66, 0x75, 0, 0, "pcmpeqw"},
    {0x66, 0xFC, 0, 0, "paddb"}, {0x66, 0xFD, 0, 0, "paddw"}, {0x66, 0xFE, 0, 0, "paddd"}, {0x66, 0xD4, 0, 0, "paddq"},
    {0x66, 0xF8, 0, 0, "psubb"}, {0x66, 0xFA, 0, 0, "psubd"}, {0x66, 0xFB, 0, 0, "psubq"},
    {0x66, 0xEC, 0, 0, "paddsb"}, {0x66, 0xED, 0, 0, "paddsw"}, {0x66, 0xDC, 0, 0, "paddusb"}, {0x66, 0xDD, 0, 0, "paddusw"},
    {0x66, 0xE8, 0, 0, "psubsb"}, {0x66, 0xE9, 0, 0, "psubsw"}, {0x66, 0xD8, 0, 0, "psubusb"}, {0x66, 0xD9, 0, 0, "psubusw"},
    {0x66, 0xD5, 0, 0, "pmullw"}, {0x66, 0xE5, 0, 0, "pmulhw"}, {0x66, 0xE4, 0, 0, "pmulhuw"}, {0x66, 0xF4, 0, 0, "pmuludq"},
    {0x66, 0xF5, 0, 0, "pmaddwd"}, {0x66, 0xF6, 0, 0, "psadbw"}, {0x66, 0xE0, 0, 0, "pavgb"}, {0x66, 0xE3, 0, 0, "pavgw"},
    {0x66, 0xDA, 0, 0, "pminub"}, {0x66, 0xDE, 0, 0, "pmaxub"}, {0x66, 0xEA, 0, 0, "pminsw"}, {0x66, 0xEE, 0, 0, "pmaxsw"},
    {0x66, 0xDB, 0, 0, "pand"}, {0x66, 0xDF, 0, 0, "pandn"}, {0x66, 0xEB, 0, 0, "por"}, {0x66, 0xEF, 0, 0, "pxor"},
    {0x66, 0xD1, 0, 0, "psrlw"}, {0x66, 0xD2, 0, 0, "psrld"}, {0x66, 0xD3, 0, 0, "psrlq"}, {0x66, 0xE1, 0, 0, "psraw"},
    {0x66, 0xE2, 0, 0, "psrad"}, {0x66, 0xF1, 0, 0, "psllw"}, {0x66, 0xF2, 0, 0, "pslld"}, {0x66, 0xF3, 0, 0, "psllq"},
    {0x66, 0x71, 1, 5, "psxw_imm"}, {0x66, 0x72, 1, 5, "psxd_imm"}, {0x66, 0x73, 1, 5, "psxq_imm"},
    {0x66, 0xC4, 1, 2, "pinsrw"}, {0x66, 0xC5, 1, 3, "pextrw"},
    {0, 0xFC, 0, 4, "mmx_paddb"}, {0, 0xD5, 0, 4, "mmx_pmullw"}, {0, 0x63, 0, 4, "mmx_packsswb"}, {0, 0x75, 0, 4, "mmx_pcmpeqw"},
    {0, 0xF5, 0, 4, "mmx_pmaddwd"}, {0, 0xE9, 0, 4, "mmx_psubsw"}, {0, 0xD3, 0, 4, "mmx_psrlq"}, {0, 0x62, 0, 4, "mmx_punpckldq"},
    {0, 0xDE, 0, 4, "mmx_pmaxub"}, {0, 0xF6, 0, 4, "mmx_psadbw"}, {0, 0x70, 1, 4, "mmx_pshufw"},
};

void GenSse(Builder& g, Rng& r, bool) {
    SseSetup(g, r);
    const SseOp& o = kSseOps[r.Below(sizeof(kSseOps) / sizeof(kSseOps[0]))];
    uint8_t reg = static_cast<uint8_t>(r.Below(8));
    if (o.form == 4) {
        // MMX: mm0 и mm1 из памяти, операция, результат — в память
        g.Byte(0x0F); g.Byte(0x6F); g.Byte(0x05); g.Imm(FA, 4); // movq mm0, [A]
        g.Byte(0x0F); g.Byte(0x6F); g.Byte(0x0D); g.Imm(FB, 4); // movq mm1, [B]
        g.Byte(0x0F); g.Byte(o.op);
        if (r.Chance(60)) g.Byte(0xC1); else { g.Byte(0x05); g.Imm(FB, 4); } // mm0, mm1 / mm0, [B]
        if (o.imm) g.Imm(r.Below(256), 1);
        g.Byte(0x0F); g.Byte(0x7F); g.Byte(0x05); g.Imm(FC, 4); // movq [C], mm0
        g.Byte(0x0F); g.Byte(0x77);                             // emms
        g.b_.name = o.name;
        return;
    }
    if (o.pfx) g.Byte(o.pfx);
    g.Byte(0x0F);
    g.Byte(o.op);
    switch (o.form) {
        case 0: SseModRM(g, r, reg); break;
        case 1: SseModRM(g, r, reg, false, true); break;
        case 2: { // xmm, r/m32
            if (r.Chance(50)) g.ModRMReg(reg, static_cast<uint8_t>(r.Below(8)));
            else { g.Byte(static_cast<uint8_t>((reg << 3) | 5)); g.Imm(FA + 4 * r.Below(4), 4); }
            break;
        }
        case 3: SseModRM(g, r, reg, o.op != 0x50 && o.op != 0xD7 && o.op != 0xC5, o.op == 0x50 || o.op == 0xD7 || o.op == 0xC5); break;
        case 5: { static const uint8_t subs71[] = {2, 4, 6}, subs73[] = {2, 3, 6, 7};
                  uint8_t sub = o.op == 0x73 ? subs73[r.Below(4)] : subs71[r.Below(3)];
                  g.ModRMReg(sub, static_cast<uint8_t>(r.Below(8))); break; }
    }
    if (o.imm) {
        uint32_t imm = r.Below(256);
        if (o.op == 0xC2) imm = r.Below(8);
        else if (o.form == 5 && r.Chance(60)) imm = r.Below(20);
        g.Imm(imm, 1);
    }
    g.b_.name = o.name;
}

const GenEntry kSseGens[] = {{"sse", GenSse, 1}};
