/* x86 RtlUnwind: terminate inner SEH frame and resume at target IP. */
#include <windows.h>
#include <stdio.h>

struct Registration { struct Registration *next; void *handler; };
static volatile int callbacks, wrongFlags, wrongExitFlags, returnedUnexpectedly;
static volatile int expectExit;

static int __cdecl UnwindHandler(EXCEPTION_RECORD *record, void *frame,
                                 CONTEXT *context, void *dispatcher) {
    (void)frame; (void)context; (void)dispatcher;
    callbacks++;
    if ((record->ExceptionFlags & EXCEPTION_UNWINDING)==0) wrongFlags++;
    if (((record->ExceptionFlags & EXCEPTION_EXIT_UNWIND)!=0) != (expectExit!=0)) wrongExitFlags++;
    return 1; /* ExceptionContinueSearch */
}
int main(void) {
    typedef void (WINAPI *RtlUnwindFn)(void *, void *, EXCEPTION_RECORD *, void *);
    HMODULE kernel=GetModuleHandleA("kernel32.dll");
    RtlUnwindFn unwind=kernel ? (RtlUnwindFn)GetProcAddress(kernel,"RtlUnwind") : NULL;
    struct Registration target, inner;
    void *old=0, *head=0;
    if(!unwind) { puts("missing KERNEL32!RtlUnwind"); return 1; }
    __asm__ __volatile__("movl %%fs:0, %0" : "=r"(old));
    target.next=(struct Registration *)old;
    target.handler=(void *)&UnwindHandler;
    inner.next=&target;
    inner.handler=(void *)&UnwindHandler;
    __asm__ __volatile__("movl %0, %%fs:0" : : "r"(&inner) : "memory");
    unwind(&target, &&resumed, NULL, (void *)0x1234u);
    returnedUnexpectedly++;
resumed:
    __asm__ __volatile__("movl %%fs:0, %0" : "=r"(head));
    __asm__ __volatile__("movl %0, %%fs:0" : : "r"(old) : "memory");
    if(head != &target || callbacks!=1 || wrongFlags || returnedUnexpectedly) {
        printf("RtlUnwind FAILED head=%p target=%p cb=%d flags=%d returned=%d\n",
               head,(void *)&target,callbacks,wrongFlags,returnedUnexpectedly);
        return 1;
    }
    /* Exit-unwind must notify both registered frames and clear FS:[0]. */
    callbacks=0;
    expectExit=1;
    __asm__ __volatile__("movl %0, %%fs:0" : : "r"(&inner) : "memory");
    unwind(NULL, &&exitResumed, NULL, (void *)0x4321u);
    returnedUnexpectedly++;
exitResumed:
    __asm__ __volatile__("movl %%fs:0, %0" : "=r"(head));
    __asm__ __volatile__("movl %0, %%fs:0" : : "r"(old) : "memory");
    if(head != old || callbacks!=2 || wrongFlags || wrongExitFlags || returnedUnexpectedly) {
        printf("Exit-unwind FAILED head=%p expected=%p cb=%d flags=%d exitflags=%d returned=%d\n",
               head,old,callbacks,wrongFlags,wrongExitFlags,returnedUnexpectedly);
        return 1;
    }
    puts("PASSED");
    return 0;
}
