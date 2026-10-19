/* oracle.c — исполняет тесты (OracleCase) на настоящем процессоре.
 *
 * Сборка: gcc -m32 -O1 -no-pie oracle.c oracle_entry.S -o oracle
 * Запуск: oracle < cases.bin > results.bin
 *         oracle -d < one_case.bin   (плюс сырая область данных после теста)
 *
 * Только для Linux x86/x86-64 с поддержкой 32-битных программ.
 */
#define _GNU_SOURCE
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "oracle_common.h"

uint32_t g_in[8], g_inFlags, g_out[8], g_outFlags;
uint32_t g_hostEsp, g_retAddr, g_codeAddr = ORACLE_CODE_BASE, g_saveStackTop;
uint32_t g_xmmIn[32] __attribute__((aligned(16)));
uint32_t g_xmmOut[32] __attribute__((aligned(16)));
uint32_t g_mxcsrIn, g_mxcsrOut;
uint16_t g_fpuCwIn;
uint8_t g_fenvOut[28];
static uint8_t g_saveStack[256] __attribute__((aligned(16)));

extern void run_case(void);
extern void oracle_return(void);

static sigjmp_buf g_jump;
static volatile int g_signal;

static void OnSignal(int sig, siginfo_t* info, void* uctx) {
    (void)info;
    (void)uctx;
    g_signal = sig;
    siglongjmp(g_jump, 1);
}

static uint8_t* EmitMovStore(uint8_t* p, int reg, uint32_t addr) {
    /* mov [addr], reg32 */
    if (reg == 0) {
        *p++ = 0xA3;
    } else {
        *p++ = 0x89;
        *p++ = (uint8_t)(0x05 | (reg << 3));
    }
    memcpy(p, &addr, 4);
    return p + 4;
}

int main(int argc, char** argv) {
    int dump = argc > 1 && strcmp(argv[1], "-d") == 0;
    uint8_t* data = mmap((void*)ORACLE_DATA_BASE, ORACLE_DATA_SIZE, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    uint8_t* code = mmap((void*)ORACLE_CODE_BASE, 0x1000, PROT_READ | PROT_WRITE | PROT_EXEC,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (data == MAP_FAILED || code == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    stack_t ss;
    ss.ss_sp = malloc(65536);
    ss.ss_size = 65536;
    ss.ss_flags = 0;
    sigaltstack(&ss, NULL);
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = OnSignal;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_NODEFER;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
    sigaction(SIGFPE, &sa, NULL);
    sigaction(SIGTRAP, &sa, NULL);

    g_saveStackTop = (uint32_t)(uintptr_t)(g_saveStack + sizeof(g_saveStack));
    g_retAddr = (uint32_t)(uintptr_t)oracle_return;

    OracleCase t;
    while (fread(&t, sizeof(t), 1, stdin) == 1) {
        OracleResult r;
        memset(&r, 0, sizeof(r));
        OracleFillData(data, &t);

        uint32_t len = t.codeLen > ORACLE_CODE_MAX ? ORACLE_CODE_MAX : t.codeLen;
        memcpy(code, t.code, len);
        uint8_t* p = code + len;
        for (int i = 0; i < 8; ++i) p = EmitMovStore(p, i, (uint32_t)(uintptr_t)&g_out[i]);
        uint32_t a = (uint32_t)(uintptr_t)&g_saveStackTop;
        *p++ = 0x8B; *p++ = 0x25; memcpy(p, &a, 4); p += 4;          /* mov esp, [g_saveStackTop] */
        *p++ = 0x9C;                                                  /* pushfd */
        a = (uint32_t)(uintptr_t)&g_outFlags;
        *p++ = 0x8F; *p++ = 0x05; memcpy(p, &a, 4); p += 4;          /* pop dword [g_outFlags] */
        a = (uint32_t)(uintptr_t)&g_retAddr;
        *p++ = 0xFF; *p++ = 0x25; memcpy(p, &a, 4); p += 4;          /* jmp [g_retAddr] */

        memcpy(g_in, t.regs, sizeof(g_in));
        g_inFlags = t.eflags;
        memcpy(g_xmmIn, t.xmmInit, sizeof(g_xmmIn));
        g_mxcsrIn = t.mxcsr ? t.mxcsr : 0x1F80;
        g_fpuCwIn = t.fpuControl ? t.fpuControl : 0x027F;

        g_signal = 0;
        if (sigsetjmp(g_jump, 1) == 0) {
            run_case();
            r.status = ORACLE_OK;
            memcpy(r.regs, g_out, sizeof(r.regs));
            r.eflags = g_outFlags;
            memcpy(r.xmm, g_xmmOut, sizeof(r.xmm));
            r.mxcsr = g_mxcsrOut;
            memcpy(&r.fpuControl, g_fenvOut + 0, 2);
            memcpy(&r.fpuStatus, g_fenvOut + 4, 2);
            memcpy(&r.fpuTag, g_fenvOut + 8, 2);
        } else {
            __asm__ volatile("cld; fninit");
            uint32_t mx = 0x1F80;
            __asm__ volatile("ldmxcsr %0" : : "m"(mx));
            switch (g_signal) {
                case SIGSEGV: case SIGBUS: r.status = ORACLE_SEGV; break;
                case SIGILL: r.status = ORACLE_ILL; break;
                case SIGFPE: r.status = ORACLE_FPE; break;
                case SIGTRAP: r.status = ORACLE_TRAP; break;
                default: r.status = ORACLE_OTHER; break;
            }
        }
        r.dataHash = OracleHash(data, ORACLE_DATA_SIZE);
        fwrite(&r, sizeof(r), 1, stdout);
        if (dump) fwrite(data, ORACLE_DATA_SIZE, 1, stdout);
    }
    return 0;
}
