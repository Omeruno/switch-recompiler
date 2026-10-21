/* WndProc waits on its worker; worker's own nested WndProc waits back on
   the initial thread. This requires saved native stacks, not recursive pumping. */
#include <windows.h>
#include <stdio.h>
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d error %lu\n",__LINE__,GetLastError()); return __LINE__; } } while (0)
static HANDLE fromWorker,fromMain;
static DWORD tlsIndex,mainId,workerId;
static volatile LONG mainReturned,workerReturned;
static volatile LONG spinFlag;
static LRESULT CALLBACK WindowProc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    if (message==WM_APP+1) {
        DWORD tid=GetCurrentThreadId();
        SetLastError(1234);
        if (WaitForSingleObject(fromWorker,INFINITE)!=WAIT_OBJECT_0) return 10;
        if (GetCurrentThreadId()!=tid || tid!=mainId || TlsGetValue(tlsIndex)!=(LPVOID)0x1111) return 11;
        if (!SetEvent(fromMain)) return 12;
        Sleep(2);
        mainReturned=1;
        return 71;
    }
    if (message==WM_APP+2) {
        DWORD tid=GetCurrentThreadId();
        if (!SetEvent(fromWorker)) return 20;
        if (WaitForSingleObject(fromMain,INFINITE)!=WAIT_OBJECT_0) return 21;
        if (GetCurrentThreadId()!=tid || tid!=workerId || TlsGetValue(tlsIndex)!=(LPVOID)0x2222) return 22;
        workerReturned=1;
        return 72;
    }
    if (message==WM_APP+3) {
        MSG msg;
        if (GetMessageA(&msg,window,WM_APP+4,WM_APP+4)!=1 || msg.message!=WM_APP+4) return 40;
        return msg.wParam;
    }
    if (message==WM_APP+5) { while (!spinFlag) {} return 75; }
    if (message==WM_APP+6)
        return WaitForSingleObject(fromWorker,2)==WAIT_TIMEOUT ? 76 : 41;
    return DefWindowProcA(window,message,wp,lp);
}
static DWORD WINAPI Worker(LPVOID unused) {
    (void)unused;
    workerId=GetCurrentThreadId();
    if (!TlsSetValue(tlsIndex,(LPVOID)0x2222)) return 30;
    HWND window=CreateWindowA("CallbackWait","worker",0,0,0,100,100,NULL,NULL,GetModuleHandleA(NULL),NULL);
    if (!window) return 31;
    LRESULT result=SendMessageA(window,WM_APP+2,0,0);
    DestroyWindow(window);
    return result==72 ? 42 : (DWORD)result;
}
static DWORD WINAPI PostWorker(LPVOID window) {
    Sleep(2);
    return PostMessageA((HWND)window,WM_APP+4,74,0) ? 42 : 43;
}
static DWORD WINAPI PublishFlag(LPVOID unused) {
    (void)unused;
    Sleep(2); spinFlag=1;
    return 42;
}
int main(void) {
    WNDCLASSA cls={0};
    cls.lpfnWndProc=WindowProc; cls.hInstance=GetModuleHandleA(NULL); cls.lpszClassName="CallbackWait";
    CHECK(RegisterClassA(&cls));
    fromWorker=CreateEventA(NULL,FALSE,FALSE,NULL);
    fromMain=CreateEventA(NULL,FALSE,FALSE,NULL);
    tlsIndex=TlsAlloc(); mainId=GetCurrentThreadId();
    CHECK(fromWorker && fromMain && tlsIndex!=TLS_OUT_OF_INDEXES);
    CHECK(TlsSetValue(tlsIndex,(LPVOID)0x1111));
    HWND window=CreateWindowA("CallbackWait","main",0,0,0,100,100,NULL,NULL,cls.hInstance,NULL);
    CHECK(window);
    HANDLE worker=CreateThread(NULL,0,Worker,NULL,0,NULL);
    CHECK(worker);
    CHECK(SendMessageA(window,WM_APP+1,0,0)==71);
    CHECK(WaitForSingleObject(worker,INFINITE)==WAIT_OBJECT_0);
    DWORD exitCode=0;
    CHECK(GetExitCodeThread(worker,&exitCode) && exitCode==42);
    CHECK(mainReturned && workerReturned && GetCurrentThreadId()==mainId);
    CHECK(CloseHandle(worker));
    worker=CreateThread(NULL,0,PostWorker,window,0,NULL);
    CHECK(worker && SendMessageA(window,WM_APP+3,0,0)==74);
    CHECK(WaitForSingleObject(worker,INFINITE)==WAIT_OBJECT_0 && CloseHandle(worker));
    worker=CreateThread(NULL,0,PublishFlag,NULL,0,NULL);
    CHECK(worker && SendMessageA(window,WM_APP+5,0,0)==75);
    CHECK(WaitForSingleObject(worker,INFINITE)==WAIT_OBJECT_0 && CloseHandle(worker));
    CHECK(SendMessageA(window,WM_APP+6,0,0)==76);
    CHECK(CloseHandle(fromWorker) && CloseHandle(fromMain));
    CHECK(DestroyWindow(window) && TlsFree(tlsIndex));
    puts("PASSED: mutually waiting nested WndProcs preserve native stacks, TEB and TLS");
    return 0;
}
