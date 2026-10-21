/* Guest sleep must never block another runnable guest CPU. */
#include <windows.h>
#include <stdio.h>
static volatile LONG phase;
static DWORD WINAPI Sleeper(LPVOID unused) {
    (void)unused;
    phase=1;
    Sleep(100);
    phase=3;
    return 29;
}
int main(void) {
    DWORD id=0, code=0;
    HANDLE t=CreateThread(NULL,0,Sleeper,NULL,0,&id);
    if(!t) { puts("SLEEP_CREATE_FAILED");return 1; }
    /* The main thread spins while the worker sleeps. A host-thread Sleep
       would stall this entire emulated process and hide the bug. */
    for(unsigned i=0;i<800000u && phase==0;++i) {}
    if(phase==0) {puts("SLEEP_WORKER_NOT_STARTED");return 2;}
    if(WaitForSingleObject(t,INFINITE)!=WAIT_OBJECT_0 ||
       !GetExitCodeThread(t,&code) || code!=29 || phase!=3) {
        puts("SLEEP_WAKE_FAILED");return 3;
    }
    if(!CloseHandle(t)){puts("SLEEP_CLOSE_FAILED");return 4;}
    puts("PASSED: cooperative guest Sleep and worker wake");
    return 0;
}
