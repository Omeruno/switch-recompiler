#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <stdio.h>
#define CHECK(x) do { if (!(x)) { printf("FAIL signalwait line %d error %lu\n",__LINE__,GetLastError()); return __LINE__; } } while (0)
static HANDLE request,reply,notice,blocked;
static volatile LONG callbacks,callbackFailure;
static VOID CALLBACK Apc(ULONG_PTR data) {
    if (data && WaitForSingleObject((HANDLE)data,0)!=WAIT_OBJECT_0) callbackFailure=1;
    ++callbacks;
}
static DWORD WINAPI PingPong(LPVOID unused) {
    unsigned i; (void)unused;
    for (i=0;i<32;++i)
        if (SignalObjectAndWait(request,reply,2000,FALSE)!=WAIT_OBJECT_0) return i+1;
    return 0;
}
static DWORD WINAPI Respond(LPVOID unused) {
    (void)unused;
    if (WaitForSingleObject(notice,2000)!=WAIT_OBJECT_0) return 1;
    Sleep(2);
    return SetEvent(reply)?0:2;
}
static DWORD WINAPI CloseWhileWaiting(LPVOID unused) {
    (void)unused;
    if (WaitForSingleObject(notice,2000)!=WAIT_OBJECT_0) return 1;
    return CloseHandle(blocked)?0:2;
}
static DWORD WINAPI Abandon(LPVOID object) {
    return WaitForSingleObject((HANDLE)object,2000)==WAIT_OBJECT_0 ? 0:1;
}
static int Join(HANDLE t) {
    DWORD code=999;
    int ok=WaitForSingleObject(t,3000)==WAIT_OBJECT_0 && GetExitCodeThread(t,&code) && !code;
    CloseHandle(t);return ok;
}
int main(void) {
    HANDLE event,sem,mutex,thread,restricted,alias,zero;
    DWORD before; unsigned i;
    event=CreateEventA(NULL,FALSE,FALSE,NULL);
    sem=CreateSemaphoreA(NULL,0,2,NULL);
    mutex=CreateMutexA(NULL,TRUE,NULL);
    reply=CreateEventA(NULL,FALSE,FALSE,NULL);
    CHECK(event && sem && mutex && reply);
    CHECK(SignalObjectAndWait(event,event,0,FALSE)==WAIT_OBJECT_0);
    CHECK(WaitForSingleObject(event,0)==WAIT_TIMEOUT);
    CHECK(SignalObjectAndWait(sem,sem,0,FALSE)==WAIT_OBJECT_0);
    CHECK(WaitForSingleObject(sem,0)==WAIT_TIMEOUT);
    CHECK(WaitForSingleObject(mutex,0)==WAIT_OBJECT_0); /* recursion 2 */
    CHECK(SignalObjectAndWait(mutex,mutex,0,FALSE)==WAIT_OBJECT_0);
    CHECK(ReleaseMutex(mutex) && ReleaseMutex(mutex));
    CHECK(!ReleaseMutex(mutex) && GetLastError()==ERROR_NOT_OWNER);
    CHECK(SetEvent(reply));
    CHECK(SignalObjectAndWait(mutex,reply,0,FALSE)==WAIT_FAILED && GetLastError()==ERROR_NOT_OWNER);
    CHECK(WaitForSingleObject(reply,0)==WAIT_OBJECT_0); /* failure cannot consume wait */
    CHECK(ReleaseSemaphore(sem,2,NULL) && SetEvent(reply));
    CHECK(SignalObjectAndWait(sem,reply,0,FALSE)==WAIT_FAILED && GetLastError()==ERROR_TOO_MANY_POSTS);
    CHECK(!ReleaseSemaphore(sem,1,NULL) && GetLastError()==ERROR_TOO_MANY_POSTS);
    CHECK(WaitForSingleObject(reply,0)==WAIT_OBJECT_0);
    CHECK(WaitForSingleObject(sem,0)==WAIT_OBJECT_0 && WaitForSingleObject(sem,0)==WAIT_OBJECT_0);
    CHECK(DuplicateHandle(GetCurrentProcess(),event,GetCurrentProcess(),&restricted,SYNCHRONIZE,FALSE,0));
    CHECK(DuplicateHandle(GetCurrentProcess(),reply,GetCurrentProcess(),&zero,0,FALSE,0));
    CHECK(SignalObjectAndWait(restricted,reply,0,FALSE)==WAIT_FAILED && GetLastError()==ERROR_ACCESS_DENIED);
    CHECK(WaitForSingleObject(event,0)==WAIT_TIMEOUT);
    CHECK(SignalObjectAndWait(event,zero,0,FALSE)==WAIT_FAILED && GetLastError()==ERROR_ACCESS_DENIED);
    CHECK(WaitForSingleObject(event,0)==WAIT_TIMEOUT); /* runtime preflight policy */
    CHECK(SignalObjectAndWait(event,(HANDLE)123,0,FALSE)==WAIT_FAILED && GetLastError()==ERROR_INVALID_HANDLE);
    CHECK(WaitForSingleObject(event,0)==WAIT_TIMEOUT);
    CHECK(SignalObjectAndWait(GetCurrentThread(),reply,0,FALSE)==WAIT_FAILED && GetLastError()==ERROR_INVALID_HANDLE);
    CloseHandle(restricted);CloseHandle(zero);
    CHECK(DuplicateHandle(GetCurrentProcess(),event,GetCurrentProcess(),&alias,0,FALSE,DUPLICATE_SAME_ACCESS));
    CHECK(SignalObjectAndWait(event,alias,0,FALSE)==WAIT_OBJECT_0);
    CloseHandle(alias);
    /* Repeated real blocking handshakes must never re-signal after resume. */
    request=CreateSemaphoreA(NULL,0,64,NULL);CHECK(request);
    thread=CreateThread(NULL,0,PingPong,NULL,0,NULL);CHECK(thread);
    for (i=0;i<32;++i) {
        CHECK(WaitForSingleObject(request,2000)==WAIT_OBJECT_0);
        CHECK(WaitForSingleObject(request,0)==WAIT_TIMEOUT);
        CHECK(SetEvent(reply));
    }
    CHECK(Join(thread) && WaitForSingleObject(request,0)==WAIT_TIMEOUT);
    before=GetTickCount();
    CHECK(SignalObjectAndWait(request,reply,10,FALSE)==WAIT_TIMEOUT);
    CHECK(GetTickCount()-before>=10 && GetTickCount()-before<1000);
    CHECK(WaitForSingleObject(request,0)==WAIT_OBJECT_0 && WaitForSingleObject(request,0)==WAIT_TIMEOUT);
    notice=CreateEventA(NULL,FALSE,FALSE,NULL);CHECK(notice);
    /* A pending APC observes signal first, without consuming wait-side token. */
    CHECK(SetEvent(reply) && QueueUserAPC(Apc,GetCurrentThread(),(ULONG_PTR)notice));
    CHECK(SignalObjectAndWait(notice,reply,2000,TRUE)==WAIT_IO_COMPLETION);
    CHECK(callbacks==1 && !callbackFailure && WaitForSingleObject(reply,0)==WAIT_OBJECT_0);
    CHECK(QueueUserAPC(Apc,GetCurrentThread(),0));
    thread=CreateThread(NULL,0,Respond,NULL,0,NULL);CHECK(thread);
    CHECK(SignalObjectAndWait(notice,reply,2000,FALSE)==WAIT_OBJECT_0);
    CHECK(callbacks==1 && Join(thread));
    CHECK(SleepEx(0,TRUE)==WAIT_IO_COMPLETION && callbacks==2);
    /* HANDLE closure during a wait follows runtime's defensive failure policy. */
    blocked=CreateEventA(NULL,FALSE,FALSE,NULL);CHECK(blocked);
    thread=CreateThread(NULL,0,CloseWhileWaiting,NULL,0,NULL);CHECK(thread);
    CHECK(SignalObjectAndWait(notice,blocked,2000,FALSE)==WAIT_FAILED && GetLastError()==ERROR_INVALID_HANDLE);
    CHECK(Join(thread));
    thread=CreateThread(NULL,0,Abandon,mutex,0,NULL);CHECK(thread && Join(thread));
    CHECK(SignalObjectAndWait(event,mutex,0,FALSE)==WAIT_ABANDONED);
    CHECK(ReleaseMutex(mutex) && WaitForSingleObject(event,0)==WAIT_OBJECT_0);
    CloseHandle(event);CloseHandle(sem);CloseHandle(mutex);CloseHandle(reply);CloseHandle(request);CloseHandle(notice);
    puts("PASSED: SignalObjectAndWait one-shot signal, blocking handshakes, timeout, APC, aliases, rights, overflow, closure and abandonment");
    return 0;
}
