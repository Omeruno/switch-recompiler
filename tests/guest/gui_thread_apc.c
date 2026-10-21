#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <stdio.h>
#define CHECK(x) do { if (!(x)) { printf("FAIL apc line %d error %lu callback=%ld phase=%ld\n",__LINE__,GetLastError(),failure,phase); return __LINE__; } } while (0)
#define REQUIRE(x) do { if (!(x)) { failure=__LINE__; return; } } while (0)
static HANDLE gate,ready,done,token,semaphore;
static DWORD workerId,tls;
static volatile LONG failure,phase,used,entered;
static DWORD order[32];
static VOID CALLBACK Callback(ULONG_PTR data) {
    REQUIRE(GetCurrentThreadId()==workerId);
    if (data==1) REQUIRE(TlsSetValue(tls,(LPVOID)123));
    REQUIRE(TlsGetValue(tls)==(LPVOID)123);
    order[used++]=(DWORD)data;
    if (data==6 || data==7) REQUIRE(SetEvent(token));
    if (data==1) { SetEvent(ready); REQUIRE(WaitForSingleObject(gate,2000)==WAIT_OBJECT_0); }
    if (data==20) { REQUIRE(SleepEx(0,TRUE)==WAIT_IO_COMPLETION); order[used++]=22; }
    if (data==99) ExitThread(77);
}
static DWORD WINAPI Worker(LPVOID unused) {
    HANDLE objects[2];
    (void)unused;entered=1;
    if (used!=3 || order[0]!=1 || order[1]!=2 || order[2]!=3 || failure) return 10;
    objects[0]=token;objects[1]=semaphore;
    if (SleepEx(2,TRUE)!=0 || WaitForSingleObjectEx(token,2,TRUE)!=WAIT_TIMEOUT) return 23;
    if (WaitForMultipleObjectsEx(2,objects,TRUE,2,TRUE)!=WAIT_TIMEOUT) return 24;
    if (WaitForSingleObject(semaphore,0)!=WAIT_OBJECT_0 || !ReleaseSemaphore(semaphore,1,NULL)) return 25;
    if (WaitForSingleObjectEx(token,0,FALSE)!=WAIT_TIMEOUT || WaitForMultipleObjectsEx(2,objects,FALSE,0,FALSE)!=WAIT_OBJECT_0+1) return 26;
    if (!ReleaseSemaphore(semaphore,1,NULL)) return 27;
    phase=1;SetEvent(ready);
    if (WaitForSingleObject(gate,2000)!=WAIT_OBJECT_0 || used!=3) return 11;
    if (SleepEx(0,TRUE)!=WAIT_IO_COMPLETION || used!=4 || order[3]!=4) return 12;
    phase=2;SetEvent(ready);
    if (SleepEx(INFINITE,TRUE)!=WAIT_IO_COMPLETION || used!=5) return 13;
    phase=3;SetEvent(ready);
    if (WaitForSingleObjectEx(token,2000,TRUE)!=WAIT_IO_COMPLETION || used!=6) return 14;
    if (WaitForSingleObject(token,0)!=WAIT_OBJECT_0) return 15;
    phase=4;SetEvent(ready);
    if (WaitForMultipleObjectsEx(2,objects,TRUE,2000,TRUE)!=WAIT_IO_COMPLETION || used!=7) return 16;
    if (WaitForSingleObject(token,0)!=WAIT_OBJECT_0 || WaitForSingleObject(semaphore,0)!=WAIT_OBJECT_0) return 17;
    phase=5;SetEvent(ready);
    if (MsgWaitForMultipleObjectsEx(0,NULL,2000,QS_POSTMESSAGE,MWMO_ALERTABLE)!=WAIT_IO_COMPLETION || used!=8) return 18;
    if (!QueueUserAPC(Callback,GetCurrentThread(),20) || !QueueUserAPC(Callback,GetCurrentThread(),21)) return 19;
    if (SleepEx(0,TRUE)!=WAIT_IO_COMPLETION || used!=11 || order[8]!=20 || order[9]!=21 || order[10]!=22) return 20;
    phase=6;SetEvent(ready);
    // Pending APCs are dropped on normal return from a nonalertable wait.
    if (WaitForSingleObject(gate,2000)!=WAIT_OBJECT_0 || used!=11) return 21;
    SetEvent(done);return failure?22:0;
}
static DWORD WINAPI NeverEnter(LPVOID unused) { (void)unused;entered=2;return 99; }
static int Next(LONG expected) { return WaitForSingleObject(ready,2000)==WAIT_OBJECT_0 && phase==expected; }
int main(void) {
    HANDLE thread,exiting;
    DWORD code;
    tls=TlsAlloc();CHECK(tls!=TLS_OUT_OF_INDEXES);
    gate=CreateEventA(NULL,FALSE,FALSE,NULL);ready=CreateEventA(NULL,FALSE,FALSE,NULL);
    done=CreateEventA(NULL,TRUE,FALSE,NULL);token=CreateEventA(NULL,FALSE,FALSE,NULL);
    semaphore=CreateSemaphoreA(NULL,1,1,NULL);CHECK(gate && ready && done && token && semaphore);
    thread=CreateThread(NULL,0,Worker,NULL,CREATE_SUSPENDED,&workerId);CHECK(thread);
    CHECK(QueueUserAPC(Callback,thread,1) && QueueUserAPC(Callback,thread,2));
    Sleep(2);CHECK(!used && !entered);
    CHECK(ResumeThread(thread)==1 && WaitForSingleObject(ready,2000)==WAIT_OBJECT_0 && used==1 && !entered);
    CHECK(QueueUserAPC(Callback,thread,3));Sleep(2);CHECK(used==1);
    CHECK(SetEvent(gate) && Next(1) && entered==1);
    CHECK(QueueUserAPC(Callback,thread,4));Sleep(2);CHECK(used==3);
    CHECK(SetEvent(gate) && Next(2));
    CHECK(QueueUserAPC(Callback,thread,5) && Next(3));
    // APC takes precedence at an alertable wait without consuming a ready event.
    CHECK(QueueUserAPC(Callback,thread,6) && Next(4));
    CHECK(QueueUserAPC(Callback,thread,7) && Next(5));
    CHECK(QueueUserAPC(Callback,thread,8) && Next(6));
    CHECK(QueueUserAPC(Callback,thread,9) && SetEvent(gate));
    CHECK(WaitForSingleObject(thread,2000)==WAIT_OBJECT_0 && GetExitCodeThread(thread,&code) && code==0 && used==11 && !failure);
    CHECK(WaitForSingleObject(done,0)==WAIT_OBJECT_0);
    CHECK(!QueueUserAPC(Callback,thread,10) && GetLastError()==ERROR_GEN_FAILURE);
    CHECK(!QueueUserAPC(Callback,gate,10) && GetLastError()==ERROR_INVALID_HANDLE);
    CloseHandle(thread);
    CHECK(!QueueUserAPC(Callback,thread,10) && GetLastError()==ERROR_INVALID_HANDLE);
    exiting=CreateThread(NULL,0,NeverEnter,NULL,CREATE_SUSPENDED,&workerId);CHECK(exiting);
    // First APC exits before entry; the remaining queued callback cannot run.
    CHECK(QueueUserAPC(Callback,exiting,1) && QueueUserAPC(Callback,exiting,99) && QueueUserAPC(Callback,exiting,100));
    CHECK(SetEvent(gate) && ResumeThread(exiting)==1);
    CHECK(WaitForSingleObject(exiting,2000)==WAIT_OBJECT_0 && GetExitCodeThread(exiting,&code) && code==77);
    CHECK(entered==1 && used==13 && order[11]==1 && order[12]==99 && !failure);
    CHECK(!QueueUserAPC(Callback,exiting,10) && GetLastError()==ERROR_GEN_FAILURE);
    CloseHandle(exiting);CloseHandle(gate);CloseHandle(ready);CloseHandle(done);CloseHandle(token);CloseHandle(semaphore);TlsFree(tls);
    puts("PASSED: user APC FIFO/startup delivery, owner TLS, nonalertable waits, SleepEx/object/MsgWait interruption, nested APCs and exit cleanup");
    return 0;
}
