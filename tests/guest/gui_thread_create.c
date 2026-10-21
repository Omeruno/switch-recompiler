/* End-to-end guest Win32 worker: regular CreateThread and thread return.
   Must work under the regular winrun execution loop, not --probe flags. */
#include <windows.h>
#include <stdio.h>

static volatile LONG sharedValue;
static DWORD WINAPI Worker(LPVOID parameter) {
    sharedValue=(LONG)(DWORD_PTR)parameter;
    return 42;
}
int main(void) {
    DWORD threadId=0, exitCode=259;
    HANDLE thread=CreateThread(NULL,0,Worker,(LPVOID)0x12345u,0,&threadId);
    if(!thread || !threadId) {
        puts("CREATE_THREAD_FAILED");
        return 1;
    }
    DWORD waitResult=WaitForSingleObject(thread,INFINITE);
    if(waitResult!=WAIT_OBJECT_0 || sharedValue!=0x12345) {
        printf("THREAD_WAIT_FAILED result=%lu value=%ld\n",
               (unsigned long)waitResult,(long)sharedValue);
        return 2;
    }
    if(!GetExitCodeThread(thread,&exitCode)) {
        puts("GET_EXIT_CODE_FAILED");
        return 3;
    }
    if(exitCode!=42) {
        printf("WORKER_RETURN_FAILED code=%lu\n",(unsigned long)exitCode);
        return 4;
    }
    if(!CloseHandle(thread)) {
        puts("THREAD_CLOSE_FAILED");return 5;
    }
    if(CloseHandle(thread) || GetLastError()!=ERROR_INVALID_HANDLE) {
        puts("THREAD_DOUBLE_CLOSE_NOT_REJECTED");return 6;
    }
    if(WaitForSingleObject(thread,0)!=WAIT_FAILED ||
       GetLastError()!=ERROR_INVALID_HANDLE) {
        puts("THREAD_CLOSED_HANDLE_WAIT_NOT_REJECTED");return 7;
    }
    puts("PASSED: guest thread wait, exit code and HANDLE close");
    return 0;
}
