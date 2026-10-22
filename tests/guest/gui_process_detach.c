#define _WIN32_WINNT 0x0501
#include "lifecycle_shared.h"
#include <stdio.h>
__declspec(dllimport) DWORD LifeTopAnchor(void);
static HANDLE ready,gate,timer;
static DWORD tls;
static VOID CALLBACK NeverRun(ULONG_PTR data) { (void)data; life_state()->failure|=2048; }
static DWORD WINAPI Finished(LPVOID arg) { (void)arg;return 73; }
static DWORD WINAPI NeverStart(LPVOID arg) { (void)arg;life_state()->failure|=4096;return 74; }
static DWORD WINAPI Blocked(LPVOID arg) {
    (void)arg;SetEvent(ready);
    WaitForSingleObject(gate,5000);
    life_state()->failure|=8192;return 75;
}
static void NTAPI ExeTls(PVOID module,DWORD reason,PVOID reserved) {
    (void)module;
    life_record(5,1,reason,reason==DLL_PROCESS_DETACH?(DWORD)TlsGetValue(tls):0,(DWORD)reserved);
}
PIMAGE_TLS_CALLBACK exe_callback __attribute__((used,section(".CRT$XLB")))=ExeTls;
static VOID CALLBACK ExitApc(ULONG_PTR arg) {
    LifeState* state=life_state();(void)arg;
    state->exitOwner=GetCurrentThreadId();
    ExitProcess(0);
    state->failure|=16384;
}
static DWORD WINAPI Exiter(LPVOID arg) {
    (void)arg;
    if(!TlsSetValue(tls,(LPVOID)84) || !QueueUserAPC(ExitApc,GetCurrentThread(),0))return 76;
    SleepEx(2000,TRUE);
    life_state()->failure|=32768;return 77;
}
#define CHECK(x) do { if(!(x)) { printf("FAIL process detach line %d error %lu\n",__LINE__,GetLastError());return __LINE__; } } while(0)
int main(void) {
    LifeState* state=life_state();HANDLE exiter;LARGE_INTEGER due;
    CHECK(LifeTopAnchor()==2 && LoadLibraryA("lifecycle_disabled.dll"));
    CHECK(!LoadLibraryA("lifecycle_failed.dll"));
    tls=TlsAlloc();CHECK(tls!=TLS_OUT_OF_INDEXES && TlsSetValue(tls,(LPVOID)42));
    ready=CreateEventA(NULL,TRUE,FALSE,NULL);gate=CreateEventA(NULL,TRUE,FALSE,NULL);
    timer=CreateWaitableTimerA(NULL,FALSE,NULL);CHECK(ready && gate && timer);
    due.QuadPart=-600000000;CHECK(SetWaitableTimer(timer,&due,1000,NULL,NULL,FALSE));
    state->exitFinished=CreateThread(NULL,0,Finished,NULL,0,NULL);CHECK(state->exitFinished);
    CHECK(WaitForSingleObject(state->exitFinished,2000)==WAIT_OBJECT_0);
    state->exitSuspended=CreateThread(NULL,0,NeverStart,NULL,CREATE_SUSPENDED,NULL);CHECK(state->exitSuspended);
    state->exitBlocked=CreateThread(NULL,0,Blocked,NULL,0,NULL);CHECK(state->exitBlocked);
    CHECK(WaitForSingleObject(ready,2000)==WAIT_OBJECT_0 && QueueUserAPC(NeverRun,state->exitBlocked,0));
    state->exitTest=1;state->exitOwner=GetCurrentThreadId();
    puts("PASSED: process detach setup with initialized/failed/disabled DLLs, parked/suspended/finished peers and pending APC/timer");
    if(state->exitMode==3) { for(;;)Sleep(10); } /* host-abort control */
    if(state->exitMode==2)return 0;
    if(state->exitMode==1) {
        exiter=CreateThread(NULL,0,Exiter,NULL,0,NULL);CHECK(exiter);
        WaitForSingleObject(exiter,5000);return 78;
    }
    CHECK(QueueUserAPC(ExitApc,GetCurrentThread(),0));
    SleepEx(2000,TRUE);return 79;
}
