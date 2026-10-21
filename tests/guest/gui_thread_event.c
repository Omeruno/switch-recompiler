/* A Windows event must block the main guest until a worker signals it.
   This exercises scheduler yield, SetEvent wakeup, and auto-reset. */
#include <windows.h>
#include <stdio.h>
static HANDLE eventHandle;
static volatile LONG workerRan;
static DWORD WINAPI Worker(LPVOID arg) {
    (void)arg;
    workerRan=1;
    return SetEvent(eventHandle) ? 71 : 72;
}
int main(void) {
    DWORD id=0, result=0, code=0;
    eventHandle=CreateEventA(NULL,FALSE,FALSE,NULL);
    if(!eventHandle) { puts("EVENT_CREATE_FAILED");return 1; }
    HANDLE worker=CreateThread(NULL,0,Worker,NULL,0,&id);
    if(!worker) { puts("EVENT_THREAD_FAILED");return 2; }
    result=WaitForSingleObject(eventHandle,INFINITE);
    if(result!=WAIT_OBJECT_0 || workerRan!=1) {
        printf("EVENT_WAIT_FAILED result=%lu ran=%ld\n",(unsigned long)result,(long)workerRan);
        return 3;
    }
    if(WaitForSingleObject(eventHandle,0)!=WAIT_TIMEOUT) {
        puts("EVENT_AUTO_RESET_FAILED");return 4;
    }
    if(WaitForSingleObject(worker,INFINITE)!=WAIT_OBJECT_0 ||
       !GetExitCodeThread(worker,&code) || code!=71) {
        puts("EVENT_WORKER_COMPLETION_FAILED");return 5;
    }
    if(!CloseHandle(eventHandle)) { puts("EVENT_CLOSE_FAILED");return 6; }
    puts("PASSED: guest event wait, wake, auto-reset and worker completion");
    return 0;
}
