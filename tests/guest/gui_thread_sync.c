/* Inter-thread mutex ownership and semaphore counting under cooperative x86. */
#include <windows.h>
#include <stdio.h>
static HANDLE gate, ready;
static volatile LONG stage;
static DWORD WINAPI Worker(LPVOID unused) {
    (void)unused;
    if(WaitForSingleObject(gate,INFINITE)!=WAIT_OBJECT_0)return 10;
    stage=1;
    if(!ReleaseMutex(gate))return 11;
    if(!ReleaseSemaphore(ready,1,NULL))return 12;
    return 57;
}
int main(void) {
    gate=CreateMutexA(NULL,TRUE,NULL);
    ready=CreateSemaphoreA(NULL,0,1,NULL);
    if(!gate || !ready){puts("CREATE_SYNC_FAILED");return 1;}
    if(ReleaseSemaphore(ready,2,NULL) || GetLastError()!=ERROR_INVALID_PARAMETER) {
        puts("SEMAPHORE_MAX_NOT_ENFORCED");return 2;
    }
    DWORD id=0,code=0;
    HANDLE worker=CreateThread(NULL,0,Worker,NULL,0,&id);
    if(!worker){puts("CREATE_WORKER_FAILED");return 3;}
    if(!ReleaseMutex(gate)){puts("RELEASE_MUTEX_FAILED");return 4;}
    if(WaitForSingleObject(ready,INFINITE)!=WAIT_OBJECT_0 || stage!=1) {
        puts("SEMAPHORE_WAKE_FAILED");return 5;
    }
    if(WaitForSingleObject(ready,0)!=WAIT_TIMEOUT) {
        puts("SEMAPHORE_COUNT_NOT_CONSUMED");return 6;
    }
    if(WaitForSingleObject(worker,INFINITE)!=WAIT_OBJECT_0 ||
       !GetExitCodeThread(worker,&code) || code!=57) {
        puts("WORKER_EXIT_FAILED");return 7;
    }
    if(!CloseHandle(worker)||!CloseHandle(ready)||!CloseHandle(gate)){
        puts("CLOSE_SYNC_FAILED");return 8;
    }
    puts("PASSED: guest mutex ownership, semaphore count and wake");
    return 0;
}
