#define _WIN32_WINNT 0x0501
#include "lifecycle_shared.h"
#include <stdio.h>
__declspec(dllimport) DWORD LifeTopAnchor(void);
static DWORD tls;
static VOID CALLBACK NeverRun(ULONG_PTR data) { (void)data;life_state()->failure|=2048; }
static DWORD WINAPI Finished(LPVOID data) { (void)data;return 73; }
static void NTAPI ExeTls(PVOID module,DWORD reason,PVOID reserved) {
    (void)module;
    life_record(5,1,reason,reason==DLL_PROCESS_DETACH?(DWORD)TlsGetValue(tls):0,(DWORD)reserved);
}
PIMAGE_TLS_CALLBACK exe_callback __attribute__((used,section(".CRT$XLB")))=ExeTls;
static VOID CALLBACK ExitApc(ULONG_PTR data) {
    (void)data;
    if(!QueueUserAPC(NeverRun,GetCurrentThread(),0))life_state()->failure|=4096;
    ExitThread(55);
    life_state()->failure|=8192;
}
static DWORD WINAPI Survivor(LPVOID data) {
    LifeState* state=life_state();DWORD code;(void)data;
    if(!TlsSetValue(tls,(LPVOID)84))return 80;
    if(state->exitMode==4) {
        if(WaitForSingleObject(state->exitDetachGate,2000)!=WAIT_OBJECT_0)return 81;
    } else if(WaitForSingleObject(state->exitBlocked,2000)!=WAIT_OBJECT_0 ||
              !GetExitCodeThread(state->exitBlocked,&code) || code!=73)return 82;
    if(!CreateMutexA(NULL,TRUE,NULL))return 83;
    if(state->exitMode==3) {
        if(!QueueUserAPC(ExitApc,GetCurrentThread(),0))return 84;
        SleepEx(2000,TRUE);state->failure|=16384;return 85;
    }
    if(!QueueUserAPC(NeverRun,GetCurrentThread(),0))return 86;
    if(state->exitMode==2)return 55;
    ExitThread(55);state->failure|=32768;return 87;
}
#define CHECK(x) do { if(!(x)) { printf("FAIL last thread line %d error %lu\n",__LINE__,GetLastError());return __LINE__; } } while(0)
int main(void) {
    LifeState* state=life_state();HANDLE worker,timer;LARGE_INTEGER due;
    CHECK(LifeTopAnchor()==2 && LoadLibraryA("lifecycle_disabled.dll"));
    CHECK(!LoadLibraryA("lifecycle_failed.dll"));
    tls=TlsAlloc();CHECK(tls!=TLS_OUT_OF_INDEXES && TlsSetValue(tls,(LPVOID)42));
    state->exitFinished=CreateThread(NULL,0,Finished,NULL,0,NULL);CHECK(state->exitFinished);
    CHECK(WaitForSingleObject(state->exitFinished,2000)==WAIT_OBJECT_0);
    state->exitSuspended=state->exitBlocked=state->exitFinished;
    state->exitDetachGate=CreateEventA(NULL,TRUE,FALSE,NULL);CHECK(state->exitDetachGate);
    timer=CreateWaitableTimerA(NULL,FALSE,NULL);CHECK(timer);
    due.QuadPart=-600000000;CHECK(SetWaitableTimer(timer,&due,1000,NULL,NULL,FALSE));
    state->exitTest=2;state->exitOwner=GetCurrentThreadId();
    puts("PASSED: last-thread fixture setup (exit code 55; post-exit inspection verifies process detach)");
    if(!state->exitMode) {
        CHECK(CreateMutexA(NULL,TRUE,NULL) && QueueUserAPC(NeverRun,GetCurrentThread(),0));
        ExitThread(55);state->failure|=65536;return 88;
    }
    CHECK(DuplicateHandle(GetCurrentProcess(),GetCurrentThread(),GetCurrentProcess(),&state->exitBlocked,0,FALSE,DUPLICATE_SAME_ACCESS));
    worker=CreateThread(NULL,0,Survivor,NULL,state->exitMode==5?CREATE_SUSPENDED:0,&state->exitOwner);CHECK(worker);
    ExitThread(73);state->failure|=131072;return 89;
}
