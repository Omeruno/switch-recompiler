#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <stdio.h>
#define CHECK(x) do { if (!(x)) { printf("FAIL timer line %d error %lu callbacks %ld failure %ld\n",__LINE__,GetLastError(),callbacks,failure); return __LINE__; } } while (0)
static volatile LONG callbacks,failure;
static DWORD owner,tls;
static ULONGLONG stamp;
static HANDLE callbackTimer,plainTimer,ready;
static VOID CALLBACK TimerApc(LPVOID data,DWORD low,DWORD high) {
    if (GetCurrentThreadId()!=owner || TlsGetValue(tls)!=(LPVOID)456 || data!=(LPVOID)123) failure=1;
    stamp=((ULONGLONG)high<<32)|low;
    ++callbacks;
}
static VOID CALLBACK UserApc(ULONG_PTR data) { if(data!=987) failure=2; ++callbacks; }
static BOOL Arm(HANDLE timer,LONGLONG ticks,LONG period,PTIMERAPCROUTINE callback) {
    LARGE_INTEGER due;due.QuadPart=ticks;
    return SetWaitableTimer(timer,&due,period,callback,(LPVOID)123,FALSE);
}
static DWORD WINAPI Setter(LPVOID unused) {
    (void)unused;
    if(!Arm(callbackTimer,-5000000,0,TimerApc) || !Arm(plainTimer,-5000000,0,NULL)) return 1;
    SetEvent(ready);return 0;
}
static int Join(HANDLE t) {
    DWORD code=999;
    int ok=WaitForSingleObject(t,2000)==WAIT_OBJECT_0 && GetExitCodeThread(t,&code) && !code;
    CloseHandle(t);return ok;
}
int main(void) {
    HANDLE timer,manual,named,opened,event,thread,alias,zero,pair[2];
    FILETIME ft;ULARGE_INTEGER now;DWORD flags;LONG saved;
    SECURITY_ATTRIBUTES sa={sizeof(sa),NULL,TRUE};
    owner=GetCurrentThreadId();tls=TlsAlloc();CHECK(tls!=TLS_OUT_OF_INDEXES && TlsSetValue(tls,(LPVOID)456));
    timer=CreateWaitableTimerA(NULL,FALSE,NULL);
    manual=CreateWaitableTimerW(NULL,TRUE,NULL);
    event=CreateEventA(NULL,FALSE,FALSE,NULL);CHECK(timer && manual && event);
    named=CreateWaitableTimerA(&sa,TRUE,"StageC.Timer");CHECK(named);
    CHECK(GetHandleInformation(named,&flags) && (flags&HANDLE_FLAG_INHERIT));
    opened=CreateWaitableTimerW(NULL,FALSE,L"Local\\StageC.Timer");CHECK(opened && GetLastError()==ERROR_ALREADY_EXISTS);
    CHECK(!CreateEventA(NULL,FALSE,FALSE,"StageC.Timer") && GetLastError()==ERROR_INVALID_HANDLE);
    CHECK(!OpenWaitableTimerA(SYNCHRONIZE,FALSE,"Missing.Timer") && GetLastError()==ERROR_FILE_NOT_FOUND);
    CHECK(Arm(named,-10000,0,NULL) && WaitForSingleObject(opened,INFINITE)==WAIT_OBJECT_0);
    CHECK(WaitForSingleObject(named,0)==WAIT_OBJECT_0); /* repeated Create preserved manual reset */
    CHECK(CancelWaitableTimer(opened) && WaitForSingleObject(named,0)==WAIT_OBJECT_0);
    CHECK(Arm(opened,-1000000,0,NULL) && WaitForSingleObject(named,0)==WAIT_TIMEOUT);
    CHECK(CancelWaitableTimer(named));
    CloseHandle(opened);
    opened=OpenWaitableTimerA(SYNCHRONIZE,FALSE,"StageC.Timer");CHECK(opened);
    CHECK(!Arm(opened,-1,0,NULL) && GetLastError()==ERROR_ACCESS_DENIED);
    CHECK(!CancelWaitableTimer(opened) && GetLastError()==ERROR_ACCESS_DENIED);
    CHECK(DuplicateHandle(GetCurrentProcess(),named,GetCurrentProcess(),&zero,0,FALSE,0));
    CHECK(WaitForSingleObject(zero,0)==WAIT_FAILED && GetLastError()==ERROR_ACCESS_DENIED);
    CHECK(!Arm(timer,-1,-1,NULL) && GetLastError()==ERROR_INVALID_PARAMETER);
    CHECK(!SetWaitableTimer(timer,(LARGE_INTEGER*)1,0,NULL,NULL,FALSE) && GetLastError()==ERROR_INVALID_PARAMETER);
    CHECK(!CancelWaitableTimer(event) && GetLastError()==ERROR_INVALID_HANDLE);
    CHECK(SignalObjectAndWait(timer,event,0,FALSE)==WAIT_FAILED && GetLastError()==ERROR_INVALID_HANDLE);
    CHECK(Arm(timer,-30000,0,NULL) && WaitForSingleObject(timer,INFINITE)==WAIT_OBJECT_0);
    CHECK(WaitForSingleObject(timer,0)==WAIT_TIMEOUT); /* auto-reset consumed */
    CHECK(Arm(timer,-10000,0,NULL));
    CHECK(DuplicateHandle(GetCurrentProcess(),timer,GetCurrentProcess(),&alias,0,FALSE,DUPLICATE_SAME_ACCESS));
    CHECK(WaitForSingleObject(alias,INFINITE)==WAIT_OBJECT_0 && WaitForSingleObject(timer,0)==WAIT_TIMEOUT);
    CloseHandle(alias);
    pair[0]=timer;pair[1]=event;
    CHECK(Arm(timer,-10000,0,NULL));
    CHECK(WaitForMultipleObjects(2,pair,TRUE,5)==WAIT_TIMEOUT);
    CHECK(WaitForSingleObject(timer,0)==WAIT_OBJECT_0); /* incomplete ALL cannot consume */
    CHECK(Arm(timer,-10000,0,NULL) && SetEvent(event));
    CHECK(WaitForMultipleObjects(2,pair,TRUE,2000)==WAIT_OBJECT_0);
    CHECK(Arm(timer,-10000,0,NULL));
    CHECK(MsgWaitForMultipleObjectsEx(1,&timer,2000,QS_POSTMESSAGE,0)==WAIT_OBJECT_0);
    CHECK(Arm(timer,-10000,0,NULL));
    CHECK(SignalObjectAndWait(event,timer,INFINITE,FALSE)==WAIT_OBJECT_0);
    CHECK(WaitForSingleObject(event,0)==WAIT_OBJECT_0);
    GetSystemTimeAsFileTime(&ft);now.LowPart=ft.dwLowDateTime;now.HighPart=ft.dwHighDateTime;
    CHECK(Arm(timer,(LONGLONG)(now.QuadPart+30000),0,NULL));
    CHECK(WaitForSingleObject(timer,INFINITE)==WAIT_OBJECT_0);
    CHECK(Arm(timer,1,0,NULL) && WaitForSingleObject(timer,INFINITE)==WAIT_OBJECT_0); /* past absolute */
    CHECK(Arm(timer,-10000,20,NULL));
    CHECK(WaitForSingleObject(timer,INFINITE)==WAIT_OBJECT_0);
    CHECK(WaitForSingleObject(timer,INFINITE)==WAIT_OBJECT_0 && CancelWaitableTimer(timer));
    CHECK(Arm(manual,-10000,20,NULL) && WaitForSingleObject(manual,INFINITE)==WAIT_OBJECT_0);
    CHECK(WaitForSingleObject(manual,0)==WAIT_OBJECT_0 && CancelWaitableTimer(manual));
    CHECK(WaitForSingleObject(manual,0)==WAIT_OBJECT_0);
    /* Timer APC executes three arguments on setter CPU/TEB/TLS, even when
       waiting alertably on an unrelated event. It does not consume the timer. */
    CHECK(Arm(timer,-10000,0,TimerApc));
    CHECK(WaitForSingleObjectEx(event,2000,TRUE)==WAIT_IO_COMPLETION);
    CHECK(callbacks==1 && !failure && stamp>=now.QuadPart);
    CHECK(WaitForSingleObject(timer,0)==WAIT_OBJECT_0);
    CHECK(Arm(timer,-10000,20,TimerApc));Sleep(100);
    saved=callbacks;CHECK(SleepEx(0,TRUE)==WAIT_IO_COMPLETION && callbacks==saved+1 && !failure);
    CHECK(CancelWaitableTimer(timer));
    CHECK(Arm(timer,-10000,0,TimerApc));Sleep(5);
    CHECK(QueueUserAPC(UserApc,GetCurrentThread(),987) && CancelWaitableTimer(timer));
    saved=callbacks;CHECK(SleepEx(0,TRUE)==WAIT_IO_COMPLETION && callbacks==saved+1 && !failure);
    CHECK(WaitForSingleObject(timer,0)==WAIT_OBJECT_0); /* cancel retains fired state */
    CHECK(Arm(timer,-10000,0,TimerApc));Sleep(5);
    CHECK(Arm(timer,-1000000,0,NULL));saved=callbacks;
    CHECK(SleepEx(0,TRUE)==0 && callbacks==saved && CancelWaitableTimer(timer));
    /* Callback timer cancels on setter termination; a plain timer survives. */
    callbackTimer=CreateWaitableTimerA(NULL,FALSE,NULL);plainTimer=CreateWaitableTimerA(NULL,FALSE,NULL);
    ready=CreateEventA(NULL,FALSE,FALSE,NULL);CHECK(callbackTimer && plainTimer && ready);
    thread=CreateThread(NULL,0,Setter,NULL,0,NULL);CHECK(thread && Join(thread));
    CHECK(WaitForSingleObject(plainTimer,INFINITE)==WAIT_OBJECT_0);
    CHECK(WaitForSingleObject(callbackTimer,0)==WAIT_TIMEOUT);
    CHECK(Arm(timer,-10000,0,TimerApc));Sleep(5);saved=callbacks;
    CHECK(CloseHandle(timer) && SleepEx(0,TRUE)==0 && callbacks==saved);
    CHECK(CloseHandle(zero) && CloseHandle(opened) && CloseHandle(named));
    CHECK(!OpenWaitableTimerA(SYNCHRONIZE,FALSE,"StageC.Timer") && GetLastError()==ERROR_FILE_NOT_FOUND);
    CloseHandle(manual);CloseHandle(event);CloseHandle(callbackTimer);CloseHandle(plainTimer);CloseHandle(ready);TlsFree(tls);
    puts("PASSED: waitable timers A/W/names/rights, relative/absolute/periodic/reset, idle waits/ALL/MsgWait, APC ABI/coalescing/cancel/rearm/exit/close");
    return 0;
}
