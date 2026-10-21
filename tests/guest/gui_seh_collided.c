/* Win32 x86 collided unwind: handler redirects to a validated older SEH frame. */
#include <windows.h>
#include <stdio.h>
struct Registration { struct Registration *next; void *handler; };
static struct Registration *replacementFrame;
static volatile int innerCalls, skippedCalls, outerCalls, errors;

static int __cdecl OnInner(EXCEPTION_RECORD *e,void *f,CONTEXT *c,void *d) {
    (void)f;(void)c;
    innerCalls++;
    if (!(e->ExceptionFlags & EXCEPTION_UNWINDING) || !d) errors++;
    *(void**)d = replacementFrame;
    return 3; /* ExceptionCollidedUnwind */
}
static int __cdecl OnSkipped(EXCEPTION_RECORD *e,void *f,CONTEXT *c,void *d) {
    (void)e;(void)f;(void)c;(void)d;
    skippedCalls++;
    return 1;
}
static int __cdecl OnOuter(EXCEPTION_RECORD *e,void *f,CONTEXT *c,void *d) {
    (void)f;(void)c;(void)d;
    outerCalls++;
    if (!(e->ExceptionFlags & EXCEPTION_COLLIDED_UNWIND)) errors++;
    return 1;
}
int main(void) {
    typedef void (WINAPI *UnwindFn)(void*,void*,EXCEPTION_RECORD*,void*);
    HMODULE k=GetModuleHandleA("kernel32.dll");
    UnwindFn unwind=k?(UnwindFn)GetProcAddress(k,"RtlUnwind"):NULL;
    struct Registration target,outer,skip,inner;
    void *old=0,*head=0;
    if(!unwind)return 1;
    __asm__ __volatile__("movl %%fs:0,%0":"=r"(old));
    target.next=(struct Registration*)old; target.handler=(void*)&OnOuter;
    outer.next=&target; outer.handler=(void*)&OnOuter;
    skip.next=&outer; skip.handler=(void*)&OnSkipped;
    inner.next=&skip; inner.handler=(void*)&OnInner;
    replacementFrame=&outer;
    __asm__ __volatile__("movl %0,%%fs:0"::"r"(&inner):"memory");
    unwind(&target, &&resumed, NULL, NULL);
    errors++;
resumed:
    __asm__ __volatile__("movl %%fs:0,%0":"=r"(head));
    __asm__ __volatile__("movl %0,%%fs:0"::"r"(old):"memory");
    if(head!=&target || innerCalls!=1 || skippedCalls!=0 ||
       outerCalls!=1 || errors) {
        printf("Collided unwind FAILED inner=%d skipped=%d outer=%d errors=%d\n",
               innerCalls,skippedCalls,outerCalls,errors);
        return 1;
    }
    puts("PASSED");
    return 0;
}
