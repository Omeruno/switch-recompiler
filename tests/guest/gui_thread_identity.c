/* Main GuestThread identity must match its x86 NT_TIB/TEB values. */
#include <windows.h>
#include <stdio.h>
int main(void) {
    DWORD tid=GetCurrentThreadId(), tebTid=0, tebSelf=0;
    __asm__ __volatile__("movl %%fs:0x24,%0" : "=r"(tebTid));
    __asm__ __volatile__("movl %%fs:0x18,%0" : "=r"(tebSelf));
    if (!tid || tid != tebTid || !tebSelf) {
        printf("GuestThread FAILED API=%lu TEB=%lu SELF=0x%lx\n",
               (unsigned long)tid,(unsigned long)tebTid,(unsigned long)tebSelf);
        return 1;
    }
    puts("PASSED");
    return 0;
}
