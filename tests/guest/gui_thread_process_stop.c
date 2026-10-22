#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <stdio.h>
#define CHECK(x) do { if (!(x)) { printf("FAIL process stop line %d error %lu\n",__LINE__,GetLastError()); return __LINE__; } } while (0)
static HANDLE ready,gate,timer,plain;
static volatile LONG callbacks,unwanted;
static VOID CALLBACK NeverRun(ULONG_PTR data) { (void)data; ++unwanted; ExitProcess(91); }
static DWORD WINAPI NeverStart(LPVOID data) { (void)data; ++unwanted; ExitProcess(92); return 92; }
static VOID CALLBACK TimerApc(LPVOID data,DWORD low,DWORD high) {
    (void)data;(void)low;(void)high;
    ++callbacks;
    SetEvent(ready);
    if(WaitForSingleObject(gate,3000)!=WAIT_OBJECT_0)ExitProcess(93);
    ExitProcess(94); /* gate is never signaled: no guest continuation on stop */
}
static DWORD WINAPI Worker(LPVOID data) {
    LARGE_INTEGER due;(void)data;due.QuadPart=-10000;
    if(!SetWaitableTimer(timer,&due,10,TimerApc,NULL,FALSE))return 1;
    SleepEx(3000,TRUE);
    ExitProcess(95);return 95;
}
static VOID CALLBACK ExitApc(ULONG_PTR data) {
    if(data!=123 || callbacks!=1 || unwanted)ExitProcess(96);
    puts("PASSED: ExitProcess from APC with parked timer callback, pending APC, active plain timer and suspended worker");
    ExitProcess(0);
    ExitProcess(97);
}
int main(void) {
    HANDLE worker,suspended;LARGE_INTEGER due;
    ready=CreateEventA(NULL,TRUE,FALSE,NULL);gate=CreateEventA(NULL,TRUE,FALSE,NULL);
    timer=CreateWaitableTimerA(NULL,FALSE,NULL);plain=CreateWaitableTimerA(NULL,FALSE,NULL);
    CHECK(ready && gate && timer && plain);
    due.QuadPart=-600000000;
    CHECK(SetWaitableTimer(plain,&due,1000,NULL,NULL,FALSE));
    suspended=CreateThread(NULL,0,NeverStart,NULL,CREATE_SUSPENDED,NULL);CHECK(suspended);
    worker=CreateThread(NULL,0,Worker,NULL,0,NULL);CHECK(worker);
    CHECK(WaitForSingleObject(ready,2000)==WAIT_OBJECT_0 && callbacks==1);
    CHECK(QueueUserAPC(NeverRun,worker,0));
    CHECK(QueueUserAPC(ExitApc,GetCurrentThread(),123));
    SleepEx(2000,TRUE);
    return 98;
}
