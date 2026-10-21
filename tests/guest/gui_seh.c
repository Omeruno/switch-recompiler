/* Genuine x86 SEH registration via FS:[0], no compiler __try extension. */
#include <windows.h>
#include <stdio.h>

struct Registration {
    struct Registration *next;
    void *handler;
};
static volatile int caught;
static DWORD expectedCode=0xE0424442u;

static int __cdecl TestHandler(EXCEPTION_RECORD *exception,
                                void *frame, CONTEXT *context, void *dispatch) {
    (void)frame; (void)dispatch;
    if (exception->ExceptionCode != expectedCode) return 1; /* ContinueSearch */
    if (context->Eip == 0 || context->Esp == 0) return 1;
    caught++;
    return 0; /* ExceptionContinueExecution */
}
int main(void) {
    struct Registration registration;
    void *old = 0;
    __asm__ __volatile__("movl %%fs:0, %0" : "=r"(old));
    registration.next=(struct Registration*)old;
    registration.handler=(void*)&TestHandler;
    __asm__ __volatile__("movl %0, %%fs:0" : : "r"(&registration) : "memory");
    RaiseException(expectedCode,0,0,NULL);
    __asm__ __volatile__("movl %0, %%fs:0" : : "r"(old) : "memory");
    if(caught!=1){printf("SEH failed caught=%d\n",caught);return 1;}
    puts("PASSED");
    return 0;
}
