/* ExitThread on the initial guest must leave its worker alive. */
#include <windows.h>
#include <stdio.h>
static HANDLE initial;
static DWORD WINAPI Survivor(LPVOID unused) {
    (void)unused;
    DWORD code=0;
    if (WaitForSingleObject(initial,INFINITE)!=WAIT_OBJECT_0) return 10;
    if (!GetExitCodeThread(initial,&code) || code!=73) return 11;
    if (!CloseHandle(initial)) return 12;
    puts("PASSED: initial ExitThread preserves worker and last-thread process exit");
    return 0;
}
int main(void) {
    if (!DuplicateHandle(GetCurrentProcess(),GetCurrentThread(),GetCurrentProcess(),
                         &initial,0,FALSE,DUPLICATE_SAME_ACCESS)) return 1;
    HANDLE worker=CreateThread(NULL,0,Survivor,NULL,CREATE_SUSPENDED,NULL);
    if (!worker || ResumeThread(worker)!=1 || !CloseHandle(worker)) return 2;
    ExitThread(73);
    return 3;
}
