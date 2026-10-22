#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <stdio.h>
#define CHECK(x) do { if (!(x)) { printf("FAIL timer reentrant line %d error %lu count %ld failure %ld\n",__LINE__,GetLastError(),calls,failure); return __LINE__; } } while (0)
#define REQUIRE(x) do { if (!(x)) { failure=__LINE__; return; } } while (0)
static HANDLE timer,armed,releaseWorker;
static DWORD mainId,workerId,tls;
static volatile LONG calls,oldCalls,failure,mode;
static VOID CALLBACK Callback(LPVOID,DWORD,DWORD);
static BOOL Arm(LONGLONG delay,LONG period,PTIMERAPCROUTINE callback,ULONG_PTR data) {
    LARGE_INTEGER due;due.QuadPart=delay;
    return SetWaitableTimer(timer,&due,period,callback,(LPVOID)data,FALSE);
}
static VOID CALLBACK OldCallback(LPVOID data,DWORD low,DWORD high) {
    (void)data;(void)low;(void)high;++oldCalls;
}
static VOID CALLBACK Callback(LPVOID data,DWORD low,DWORD high) {
    HANDLE probe;
    (void)low;(void)high;
    REQUIRE(GetCurrentThreadId()==mainId && TlsGetValue(tls)==(LPVOID)123);
    ++calls;
    if(mode==1) { REQUIRE(data==(LPVOID)1);REQUIRE(CancelWaitableTimer(timer)); }
    else if(mode==2) {
        if(data==(LPVOID)11) { REQUIRE(Arm(-10000,0,Callback,22)); }
        else { REQUIRE(data==(LPVOID)22);REQUIRE(CancelWaitableTimer(timer)); }
    } else if(mode==3) {
        REQUIRE(CloseHandle(timer));timer=NULL;
        REQUIRE(!OpenWaitableTimerA(SYNCHRONIZE,FALSE,"StageC.Close.InCallback") && GetLastError()==ERROR_FILE_NOT_FOUND);
        /* Prove source lifetime ended BEFORE returning from this callback. */
        probe=CreateEventA(NULL,FALSE,FALSE,"StageC.Close.InCallback");
        REQUIRE(probe && GetLastError()!=ERROR_ALREADY_EXISTS);REQUIRE(CloseHandle(probe));
    } else if(mode==4) { REQUIRE(data==(LPVOID)44);REQUIRE(CancelWaitableTimer(timer)); }
    else failure=__LINE__;
}
static DWORD WINAPI OldSetter(LPVOID unused) {
    (void)unused;
    if(!TlsSetValue(tls,(LPVOID)456) || !Arm(-10000,0,OldCallback,33))return 1;
    SetEvent(armed);
    if(WaitForSingleObject(releaseWorker,2000)!=WAIT_OBJECT_0)return 2;
    /* The replacement belongs to main; no old timer APC may survive here. */
    if(SleepEx(0,TRUE)!=0 || oldCalls || TlsGetValue(tls)!=(LPVOID)456)return 3;
    return 0;
}
static int Join(HANDLE t) {
    DWORD code=999;
    int ok=WaitForSingleObject(t,2000)==WAIT_OBJECT_0 && GetExitCodeThread(t,&code) && !code;
    CloseHandle(t);return ok;
}
int main(void) {
    HANDLE worker;LONG saved;
    mainId=GetCurrentThreadId();tls=TlsAlloc();CHECK(tls!=TLS_OUT_OF_INDEXES && TlsSetValue(tls,(LPVOID)123));
    timer=CreateWaitableTimerA(NULL,FALSE,NULL);CHECK(timer);
    mode=1;CHECK(Arm(-10000,2,Callback,1));
    CHECK(SleepEx(2000,TRUE)==WAIT_IO_COMPLETION && calls==1 && !failure);
    Sleep(10);CHECK(SleepEx(0,TRUE)==0 && calls==1);
    mode=2;CHECK(Arm(-10000,0,Callback,11));
    CHECK(SleepEx(2000,TRUE)==WAIT_IO_COMPLETION && (calls==2 || calls==3) && !failure);
    /* A dispatch drains all queued APCs; the rearmed expiry may arrive before
       the first alertable call returns on a heavily delayed host. */
    if(calls==2)CHECK(SleepEx(2000,TRUE)==WAIT_IO_COMPLETION);
    CHECK(calls==3 && !failure);
    CHECK(CloseHandle(timer));
    timer=CreateWaitableTimerA(NULL,FALSE,"StageC.Close.InCallback");CHECK(timer);
    mode=3;CHECK(Arm(-10000,0,Callback,0));
    CHECK(SleepEx(2000,TRUE)==WAIT_IO_COMPLETION && calls==4 && !failure && !timer);
    CHECK(SleepEx(0,TRUE)==0);
    timer=CreateWaitableTimerA(NULL,FALSE,NULL);armed=CreateEventA(NULL,FALSE,FALSE,NULL);
    releaseWorker=CreateEventA(NULL,FALSE,FALSE,NULL);CHECK(timer && armed && releaseWorker);
    worker=CreateThread(NULL,0,OldSetter,NULL,0,&workerId);CHECK(worker);
    CHECK(WaitForSingleObject(armed,2000)==WAIT_OBJECT_0);Sleep(5);
    mode=4;saved=calls;CHECK(Arm(-10000,0,Callback,44));
    CHECK(SetEvent(releaseWorker) && Join(worker) && !oldCalls);
    /* Exit of the OLD setter must not cancel the NEW owner's armed timer. */
    CHECK(SleepEx(2000,TRUE)==WAIT_IO_COMPLETION && calls==saved+1 && !failure);
    CHECK(WaitForSingleObject(timer,0)==WAIT_OBJECT_0);
    CHECK(CloseHandle(timer));CloseHandle(armed);CloseHandle(releaseWorker);TlsFree(tls);
    puts("PASSED: timer callback cancel/self-rearm/last-close lifetime, pending APC owner retarget, old-setter exit and owner TLS");
    return 0;
}
