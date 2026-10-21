/* Finite Win32 waits must expire or complete early without blocking other
   virtual guest CPUs or resetting the duration on each HLE retry. */
#include <windows.h>
#include <stdio.h>
static HANDLE gate;
static DWORD WINAPI Setter(LPVOID ignored) {
    (void)ignored;
    Sleep(4);
    return SetEvent(gate) ? 42 : 41;
}
int main(void) {
    gate=CreateEventA(NULL,TRUE,FALSE,NULL);
    if(!gate) return 1;
    if(WaitForSingleObject(gate,0)!=WAIT_TIMEOUT) return 2;
    if(WaitForSingleObject(gate,2)!=WAIT_TIMEOUT) {
        puts("FINITE_TIMEOUT_FAILED");return 3;
    }
    DWORD id=0,code=0;
    HANDLE t=CreateThread(NULL,0,Setter,NULL,0,&id);
    if(!t) return 4;
    if(WaitForSingleObject(gate,100)!=WAIT_OBJECT_0) {
        puts("FINITE_EARLY_SIGNAL_FAILED");return 5;
    }
    if(WaitForSingleObject(t,100)!=WAIT_OBJECT_0 ||
       !GetExitCodeThread(t,&code) || code!=42) {
        puts("FINITE_THREAD_WAIT_FAILED");return 6;
    }
    if(!CloseHandle(t)||!CloseHandle(gate))return 7;
    puts("PASSED: finite wait timeout and early signal");
    return 0;
}
