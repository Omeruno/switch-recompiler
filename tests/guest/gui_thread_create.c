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
    volatile unsigned spin;
    if(!thread || !threadId) {
        puts("CREATE_THREAD_FAILED");
        return 1;
    }
    /* Cooperative guest execution: main spins and workers must still run. */
    for(spin=0;spin<500000u && sharedValue!=0x12345; ++spin) {}
    if(sharedValue!=0x12345) {
        printf("WORKER_NEVER_RAN id=%lu\n",(unsigned long)threadId);
        return 2;
    }
    for(spin=0;spin<500000u; ++spin) {
        if(!GetExitCodeThread(thread,&exitCode)) {
            puts("GET_EXIT_CODE_FAILED");
            return 3;
        }
        if(exitCode!=STILL_ACTIVE)break;
    }
    if(exitCode!=42) {
        printf("WORKER_RETURN_FAILED code=%lu\n",(unsigned long)exitCode);
        return 4;
    }
    puts("PASSED: real Win32 CreateThread and worker stdcall return");
    return 0;
}
