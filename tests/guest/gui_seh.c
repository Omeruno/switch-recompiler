/* End-to-end single-thread x86 SEH via FS:[0], no compiler __try. */
#include <windows.h>
#include <stdio.h>

struct Registration { struct Registration *next; void *handler; };
static volatile int firstCalls, secondCalls, unexpected;
static const DWORD expectedCode=0xE0424442u;
static const DWORD expectedParams[2]={0x1234ABCDu,0x55667788u};

static int __cdecl FirstHandler(EXCEPTION_RECORD *record, void *frame,
                                CONTEXT *ctx, void *dispatcher) {
    (void)frame; (void)dispatcher;
    firstCalls++;
    if (record->ExceptionCode!=expectedCode || record->NumberParameters!=2 ||
        record->ExceptionInformation[0]!=expectedParams[0] ||
        record->ExceptionInformation[1]!=expectedParams[1] ||
        ctx->Eip==0 || ctx->Esp==0) unexpected++;
    return 1; /* ExceptionContinueSearch */
}
static int __cdecl SecondHandler(EXCEPTION_RECORD *record, void *frame,
                                 CONTEXT *ctx, void *dispatcher) {
    (void)frame; (void)dispatcher;
    secondCalls++;
    if (record->ExceptionCode!=expectedCode || !ctx->Eip) unexpected++;
    return 0; /* ExceptionContinueExecution */
}
int main(void) {
    struct Registration first, second;
    void *original=0;
    __asm__ __volatile__("movl %%fs:0, %0" : "=r"(original));
    second.next=(struct Registration*)original;
    second.handler=(void*)&SecondHandler;
    first.next=&second;
    first.handler=(void*)&FirstHandler;
    __asm__ __volatile__("movl %0, %%fs:0" : : "r"(&first) : "memory");
    RaiseException(expectedCode,0,2,expectedParams);
    __asm__ __volatile__("movl %0, %%fs:0" : : "r"(original) : "memory");
    if(firstCalls!=1 || secondCalls!=1 || unexpected) {
        printf("SEH FAILED first=%d second=%d unexpected=%d\n",
               firstCalls,secondCalls,unexpected);
        return 1;
    }
    puts("PASSED");
    return 0;
}
