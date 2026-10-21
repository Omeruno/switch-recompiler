/* Both SEH handlers block, with handler CpuState copies alive on native stacks. */
#include <windows.h>
#include <stdio.h>
struct Frame { struct Frame* next; void* handler; };
static HANDLE fromWorker,fromMain;
static DWORD mainId,tlsIndex;
static volatile LONG handled,bad;
static int __cdecl Handler(EXCEPTION_RECORD* exception,void* frame,CONTEXT* context,void* dispatcher) {
    (void)frame;(void)context;(void)dispatcher;
    DWORD tid=GetCurrentThreadId();
    LPVOID expected=tid==mainId ? (LPVOID)0x1111 : (LPVOID)0x2222;
    if (exception->ExceptionCode!=0xe0420301) { ++bad; return 1; }
    if (tid==mainId) {
        if (WaitForSingleObject(fromWorker,INFINITE)!=WAIT_OBJECT_0 || !SetEvent(fromMain)) ++bad;
    } else {
        if (!SetEvent(fromWorker) || WaitForSingleObject(fromMain,INFINITE)!=WAIT_OBJECT_0) ++bad;
    }
    if (GetCurrentThreadId()!=tid || TlsGetValue(tlsIndex)!=expected) ++bad;
    InterlockedIncrement(&handled);
    return 0;
}
static void Raise(void) {
    struct Frame frame;
    __asm__ __volatile__("movl %%fs:0,%0" : "=r"(frame.next));
    frame.handler=(void*)Handler;
    __asm__ __volatile__("movl %0,%%fs:0" :: "r"(&frame) : "memory");
    RaiseException(0xe0420301,0,0,NULL);
    __asm__ __volatile__("movl %0,%%fs:0" :: "r"(frame.next) : "memory");
}
static DWORD WINAPI Worker(LPVOID unused) {
    (void)unused;
    if (!TlsSetValue(tlsIndex,(LPVOID)0x2222)) return 10;
    Raise();
    return bad ? 11 : 42;
}
int main(void) {
    mainId=GetCurrentThreadId();tlsIndex=TlsAlloc();
    fromWorker=CreateEventA(NULL,FALSE,FALSE,NULL);
    fromMain=CreateEventA(NULL,FALSE,FALSE,NULL);
    if (!fromWorker || !fromMain || !TlsSetValue(tlsIndex,(LPVOID)0x1111)) return 1;
    HANDLE worker=CreateThread(NULL,0,Worker,NULL,0,NULL);
    if (!worker) return 2;
    Raise();
    DWORD code=0;
    if (WaitForSingleObject(worker,INFINITE)!=WAIT_OBJECT_0 ||
        !GetExitCodeThread(worker,&code) || code!=42 || handled!=2 || bad) return 3;
    if (!CloseHandle(worker) || !CloseHandle(fromWorker) || !CloseHandle(fromMain) || !TlsFree(tlsIndex)) return 4;
    puts("PASSED: mutually waiting guest SEH handlers preserve CPU, TEB, TLS and handler frames");
    return 0;
}
