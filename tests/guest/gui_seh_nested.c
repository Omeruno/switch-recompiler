/* Guest SEH: exception thrown from inside another exception handler. */
#include <windows.h>
#include <stdio.h>

struct Registration { struct Registration *next; void *handler; };
static volatile int outerCalls, nestedCalls, bad;
#define OUTER_CODE 0xE0420101u
#define INNER_CODE 0xE0420202u

static int __cdecl InnerHandler(EXCEPTION_RECORD *e, void *frame,
                                CONTEXT *context, void *dispatcher) {
    (void)frame; (void)context; (void)dispatcher;
    if(e->ExceptionCode!=INNER_CODE ||
       !(e->ExceptionFlags & EXCEPTION_NESTED_CALL)) {
        bad++;
        return 1;
    }
    nestedCalls++;
    return 0;
}
static int __cdecl OuterHandler(EXCEPTION_RECORD *e,void *frame,
                                CONTEXT *context,void *dispatcher) {
    (void)frame; (void)context; (void)dispatcher;
    if(e->ExceptionCode!=OUTER_CODE) { bad++; return 1; }
    outerCalls++;
    if(outerCalls>1) { bad++; return 1; }
    /* Currently active handler must not receive this exception again. */
    RaiseException(INNER_CODE,0,0,NULL);
    return 0;
}
int main(void) {
    struct Registration outer, inner;
    void *old;
    __asm__ __volatile__("movl %%fs:0, %0" : "=r"(old));
    inner.next=(struct Registration*)old;
    inner.handler=(void*)&InnerHandler;
    outer.next=&inner;
    outer.handler=(void*)&OuterHandler;
    __asm__ __volatile__("movl %0, %%fs:0" : : "r"(&outer) : "memory");
    RaiseException(OUTER_CODE,0,0,NULL);
    __asm__ __volatile__("movl %0, %%fs:0" : : "r"(old) : "memory");
    if(outerCalls!=1 || nestedCalls!=1 || bad) {
        printf("Nested SEH FAILED outer=%d nested=%d bad=%d\n",
               outerCalls,nestedCalls,bad);
        return 1;
    }
    puts("PASSED");
    return 0;
}
