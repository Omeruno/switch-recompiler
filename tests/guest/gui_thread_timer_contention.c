#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <stdio.h>
#define CHECK(x) do { if (!(x)) { printf("FAIL timer contention line %d error %lu wins %ld\n",__LINE__,GetLastError(),wins); return __LINE__; } } while (0)
static HANDLE ready,done,doomed;
static volatile LONG wins;
static BOOL Arm(HANDLE timer,LONG period) {
    LARGE_INTEGER due;due.QuadPart=-10000;
    return SetWaitableTimer(timer,&due,period,NULL,NULL,FALSE);
}
static DWORD WINAPI Waiter(LPVOID timer) {
    if(!ReleaseSemaphore(ready,1,NULL))return 1;
    if(WaitForSingleObject((HANDLE)timer,2000)!=WAIT_OBJECT_0)return 2;
    InterlockedIncrement(&wins);SetEvent(done);return 0;
}
static DWORD WINAPI ClosePeer(LPVOID unused) {
    (void)unused;Sleep(1);return CloseHandle(doomed)?0:1;
}
static int Join(HANDLE t) {
    DWORD code=999;
    int ok=WaitForSingleObject(t,2000)==WAIT_OBJECT_0 && GetExitCodeThread(t,&code) && !code;
    CloseHandle(t);return ok;
}
int main(void) {
    HANDLE timer,alias,threads[3],gate,sem,pair[3],first,closer;
    DWORD before;unsigned i,mode;
    ready=CreateSemaphoreA(NULL,0,3,NULL);done=CreateEventA(NULL,FALSE,FALSE,NULL);
    timer=CreateWaitableTimerA(NULL,FALSE,NULL);CHECK(ready && done && timer);
    CHECK(DuplicateHandle(GetCurrentProcess(),timer,GetCurrentProcess(),&alias,0,FALSE,DUPLICATE_SAME_ACCESS));
    for(i=0;i<3;++i) { threads[i]=CreateThread(NULL,0,Waiter,i==1?alias:timer,0,NULL);CHECK(threads[i]); }
    for(i=0;i<3;++i)CHECK(WaitForSingleObject(ready,2000)==WAIT_OBJECT_0);
    for(i=1;i<=3;++i) {
        CHECK(Arm(timer,0) && WaitForSingleObject(done,2000)==WAIT_OBJECT_0);
        Sleep(1);CHECK(wins==(LONG)i); /* one auto-reset expiration, one winner */
    }
    for(i=0;i<3;++i)CHECK(Join(threads[i]));
    CHECK(WaitForSingleObject(timer,0)==WAIT_TIMEOUT);
    CloseHandle(alias);CloseHandle(timer);
    timer=CreateWaitableTimerA(NULL,TRUE,NULL);CHECK(timer);wins=0;
    for(i=0;i<3;++i) { threads[i]=CreateThread(NULL,0,Waiter,timer,0,NULL);CHECK(threads[i]); }
    for(i=0;i<3;++i)CHECK(WaitForSingleObject(ready,2000)==WAIT_OBJECT_0);
    CHECK(Arm(timer,0));
    for(i=0;i<3;++i)CHECK(Join(threads[i]));
    CHECK(wins==3 && WaitForSingleObject(timer,0)==WAIT_OBJECT_0);
    CloseHandle(timer);
    /* Repeated periodic wakeups cannot renew wait-all's original deadline or
       consume any ready timer/semaphore until the unsignaled gate is ready. */
    timer=CreateWaitableTimerA(NULL,FALSE,NULL);gate=CreateEventA(NULL,FALSE,FALSE,NULL);
    sem=CreateSemaphoreA(NULL,1,1,NULL);CHECK(timer && gate && sem);
    pair[0]=timer;pair[1]=sem;pair[2]=gate;
    for(mode=0;mode<2;++mode) {
        CHECK(Arm(timer,2));before=GetTickCount();
        CHECK((mode ? WaitForMultipleObjectsEx(3,pair,TRUE,30,FALSE) : WaitForMultipleObjects(3,pair,TRUE,30))==WAIT_TIMEOUT);
        CHECK(GetTickCount()-before>=30 && GetTickCount()-before<1000);
        CHECK(CancelWaitableTimer(timer));
        CHECK(WaitForSingleObject(timer,0)==WAIT_OBJECT_0 && WaitForSingleObject(timer,0)==WAIT_TIMEOUT);
        CHECK(WaitForSingleObject(sem,0)==WAIT_OBJECT_0 && ReleaseSemaphore(sem,1,NULL));
    }
    CHECK(SetEvent(gate) && Arm(timer,0));
    CHECK(WaitForMultipleObjects(3,pair,TRUE,2000)==WAIT_OBJECT_0);
    CHECK(WaitForSingleObject(timer,0)==WAIT_TIMEOUT && WaitForSingleObject(sem,0)==WAIT_TIMEOUT);
    CloseHandle(timer);CloseHandle(gate);CloseHandle(sem);
    /* Runtime defensive closed-HANDLE behavior must end the whole transaction.
       An old failed wait's deadline must never affect a fresh infinite wait. */
    for(mode=0;mode<2;++mode) {
        first=CreateEventA(NULL,FALSE,FALSE,NULL);doomed=CreateEventA(NULL,FALSE,FALSE,NULL);
        CHECK(first && doomed);pair[0]=first;pair[1]=doomed;
        closer=CreateThread(NULL,0,ClosePeer,NULL,0,NULL);CHECK(closer);
        CHECK((mode ? WaitForMultipleObjectsEx(2,pair,TRUE,5,FALSE) : WaitForMultipleObjects(2,pair,TRUE,5))==WAIT_FAILED);
        CHECK(GetLastError()==ERROR_INVALID_HANDLE && Join(closer));
        Sleep(10);CHECK(SetEvent(first));
        CHECK(WaitForSingleObject(first,INFINITE)==WAIT_OBJECT_0);
        CHECK(WaitForSingleObject(first,0)==WAIT_TIMEOUT);CloseHandle(first);
    }
    CloseHandle(ready);CloseHandle(done);
    puts("PASSED: competing timer aliases, one/all reset wakeups, periodic wait-all deadline/token preservation and closed-wait retry isolation");
    return 0;
}
