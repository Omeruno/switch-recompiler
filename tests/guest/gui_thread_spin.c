/* A pure busy-spin must not starve another guest Win32 thread.
   No Sleep(), SwitchToThread(), event waits, or host blocking calls. */
#include <windows.h>
#include <stdio.h>

static volatile LONG published;
static DWORD WINAPI Publish(LPVOID ignored) {
    (void)ignored;
    published=0x13579;
    return 17;
}
int main(void) {
    DWORD id=0, code=0;
    HANDLE h=CreateThread(NULL,0,Publish,NULL,0,&id);
    if(!h) { puts("SPIN_CREATE_FAILED"); return 1; }
    unsigned i=0;
    for(;i<2000000u && published!=0x13579;++i) {
        /* Deliberately CPU-bound x86 guest instructions. */
        __asm__ __volatile__("" ::: "memory");
    }
    if(published!=0x13579) {
        printf("SPIN_STARVED_OTHER_THREAD: %u\n",i);
        return 2;
    }
    if(WaitForSingleObject(h,INFINITE)!=WAIT_OBJECT_0 ||
       !GetExitCodeThread(h,&code) || code!=17 || !CloseHandle(h)) {
        puts("SPIN_THREAD_COMPLETION_FAILED");
        return 3;
    }
    puts("PASSED: preemptive guest quantum breaks busy spin");
    return 0;
}
