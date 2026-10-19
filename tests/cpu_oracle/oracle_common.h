/* oracle_common.h
 *
 * Общий формат тестов для сверки интерпретатора с настоящим процессором.
 * Используется и 32-битным оракулом (oracle.c, C), и хостовым прогоном
 * (cpu_fuzz.cpp, C++), поэтому написан на чистом C.
 *
 * Тест: начальные регистры и флаги, байты кода, сид для заполнения
 * области данных и несколько «заплаток» (адрес, значение) поверх неё.
 * Результат: как завершилась инструкция, регистры, флаги, хеш области
 * данных.
 */
#ifndef ORACLE_COMMON_H
#define ORACLE_COMMON_H

#include <stdint.h>

#define ORACLE_DATA_BASE 0x10000000u
#define ORACLE_DATA_SIZE 0x2000u
#define ORACLE_CODE_BASE 0x20000000u
#define ORACLE_CODE_MAX 48
#define ORACLE_MAX_PATCHES 8

/* Как закончился тест */
enum {
    ORACLE_OK = 0,
    ORACLE_SEGV = 1,   /* обращение к памяти / привилегированная инструкция */
    ORACLE_ILL = 2,    /* неизвестная инструкция */
    ORACLE_FPE = 3,    /* #DE: деление на ноль или переполнение частного */
    ORACLE_TRAP = 4,   /* INT3 */
    ORACLE_OTHER = 5,
};

#pragma pack(push, 1)
typedef struct {
    uint32_t regs[8];
    uint32_t eflags;
    uint32_t seed;
    uint32_t codeLen;
    uint8_t code[ORACLE_CODE_MAX];
    uint32_t patchCount;
    uint32_t patchAddr[ORACLE_MAX_PATCHES];
    uint32_t patchValue[ORACLE_MAX_PATCHES];
    /* x87/SSE: начальные значения (для тестов FPU/SSE) */
    uint32_t xmmInit[8][4];
    uint16_t fpuControl;
    uint16_t pad;
    uint32_t mxcsr;
} OracleCase;

typedef struct {
    uint32_t status;
    uint32_t regs[8];
    uint32_t eflags;
    uint64_t dataHash;
    uint32_t xmm[8][4];
    uint32_t mxcsr;
    uint16_t fpuStatus;
    uint16_t fpuControl;
    uint16_t fpuTag;
    uint16_t pad;
} OracleResult;
#pragma pack(pop)

/* Детерминированное заполнение области данных */
static inline uint32_t OracleRand(uint32_t* s) {
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x ? x : 0x9E3779B9u;
    return *s;
}

static inline void OracleFillData(uint8_t* data, const OracleCase* t) {
    uint32_t s = t->seed ? t->seed : 1;
    uint32_t i;
    for (i = 0; i < ORACLE_DATA_SIZE; i += 4) {
        uint32_t v = OracleRand(&s);
        data[i] = (uint8_t)v;
        data[i + 1] = (uint8_t)(v >> 8);
        data[i + 2] = (uint8_t)(v >> 16);
        data[i + 3] = (uint8_t)(v >> 24);
    }
    for (i = 0; i < t->patchCount && i < ORACLE_MAX_PATCHES; ++i) {
        uint32_t off = t->patchAddr[i] - ORACLE_DATA_BASE;
        if (off <= ORACLE_DATA_SIZE - 4) {
            data[off] = (uint8_t)t->patchValue[i];
            data[off + 1] = (uint8_t)(t->patchValue[i] >> 8);
            data[off + 2] = (uint8_t)(t->patchValue[i] >> 16);
            data[off + 3] = (uint8_t)(t->patchValue[i] >> 24);
        }
    }
}

static inline uint64_t OracleHash(const uint8_t* data, uint32_t len) {
    uint64_t h = 1469598103934665603ull;
    uint32_t i;
    for (i = 0; i < len; ++i) {
        h ^= data[i];
        h *= 1099511628211ull;
    }
    return h;
}

#endif
