/* SEH CONTEXT must round-trip SSE state and lazy EFLAGS. */
#include <windows.h>
#include <stdio.h>
#include <string.h>

struct Registration { struct Registration *next; void *handler; };
static volatile int handlerCalls,bad;
static const DWORD code=0xE042F00Du;
static const BYTE initialXmm[16]={0x10,0x20,0x30,0x40,1,2,3,4,5,6,7,8,9,10,11,12};
static const BYTE restoredXmm[16]={0xA1,0xB2,0xC3,0xD4,12,11,10,9,8,7,6,5,4,3,2,1};
static volatile DWORD requestedMxcsr=0x1F80u;

static int __cdecl Handler(EXCEPTION_RECORD *e,void *f,CONTEXT *ctx,void *dispatcher) {
    (void)f;(void)dispatcher;
    handlerCalls++;
    if(e->ExceptionCode!=code || !ctx || !(ctx->ContextFlags&CONTEXT_EXTENDED_REGISTERS) ||
       !(ctx->ContextFlags&CONTEXT_FLOATING_POINT)) {
        bad++;
        return 1;
    }
    if(memcmp(ctx->ExtendedRegisters+160,initialXmm,16)!=0) bad++;
    memcpy(ctx->ExtendedRegisters+160,restoredXmm,16);
    /* MXCSR resides at +24 in the FXSAVE-compatible extended-registers image. */
    memcpy(ctx->ExtendedRegisters+24,(const void *)&requestedMxcsr,sizeof(DWORD));
    return 0;
}
int main(void) {
    struct Registration reg;
    void *old=0;
    BYTE actual[16]={};
    DWORD actualMxcsr=0;
    __asm__ __volatile__("movl %%fs:0,%0":"=r"(old));
    reg.next=(struct Registration*)old;
    reg.handler=(void*)&Handler;
    __asm__ __volatile__("movl %0,%%fs:0"::"r"(&reg):"memory");
    __asm__ __volatile__("movdqu %0, %%xmm0"::"m"(initialXmm[0]):"xmm0");
    RaiseException(code,0,0,NULL);
    __asm__ __volatile__("movdqu %%xmm0, %0":"=m"(actual[0]));
    __asm__ __volatile__("stmxcsr %0":"=m"(actualMxcsr));
    __asm__ __volatile__("movl %0,%%fs:0"::"r"(old):"memory");
    if(handlerCalls!=1 || bad || memcmp(actual,restoredXmm,16)!=0 ||
       (actualMxcsr&0xFFC0u)!=(requestedMxcsr&0xFFC0u)) {
        printf("CONTEXT SSE FAILED calls=%d bad=%d mxcsr=%lx\n",
               handlerCalls,bad,(unsigned long)actualMxcsr);
        return 1;
    }
    puts("PASSED");
    return 0;
}
