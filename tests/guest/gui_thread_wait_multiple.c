/* Win32 x86 guest end-to-end multiple wait: any, all, finite timeout,
   signal after cooperative sleep, and invalid HANDLE rejection. */
#include <windows.h>
#include <stdio.h>
static HANDLE gates[2];
static DWORD WINAPI Setter(LPVOID unused) {
    (void)unused;
    Sleep(3);
    if (!SetEvent(gates[1])) return 10;
    Sleep(3);
    if (!SetEvent(gates[0])) return 11;
    return 42;
}
int main(void) {
    gates[0]=CreateEventA(NULL,TRUE,FALSE,NULL);
    gates[1]=CreateEventA(NULL,TRUE,FALSE,NULL);
    if (!gates[0] || !gates[1]) return 1;
    if (WaitForMultipleObjects(2,gates,FALSE,0)!=WAIT_TIMEOUT) return 2;
    if (WaitForMultipleObjects(2,gates,TRUE,2)!=WAIT_TIMEOUT) {
        puts("ALL_TIMEOUT_FAILED"); return 3;
    }
    DWORD id=0,code=0;
    HANDLE worker=CreateThread(NULL,0,Setter,NULL,0,&id);
    if (!worker) return 4;
    if (WaitForMultipleObjects(2,gates,FALSE,100)!=WAIT_OBJECT_0+1) {
        puts("ANY_INDEX_FAILED"); return 5;
    }
    if (WaitForMultipleObjects(2,gates,TRUE,100)!=WAIT_OBJECT_0) {
        puts("ALL_WAKE_FAILED"); return 6;
    }
    if (WaitForSingleObject(worker,INFINITE)!=WAIT_OBJECT_0 ||
        !GetExitCodeThread(worker,&code) || code!=42) return 7;
    HANDLE invalid[2]={gates[0],(HANDLE)0xdeadbeefu};
    if (WaitForMultipleObjects(2,invalid,FALSE,0)!=WAIT_FAILED) {
        puts("INVALID_HANDLE_NOT_REJECTED"); return 8;
    }
    if (!CloseHandle(worker) || !CloseHandle(gates[0]) || !CloseHandle(gates[1]))
        return 9;
    puts("PASSED: guest WaitForMultipleObjects any/all, timed and invalid handles");
    return 0;
}
