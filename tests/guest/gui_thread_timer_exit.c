#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <stdio.h>
#define CHECK(x) do { if (!(x)) { printf("FAIL timer exit line %d error %lu failure %ld\n",__LINE__,GetLastError(),failure); return __LINE__; } } while (0)
static HANDLE timer,mutex;
static HWND window;
static DWORD owner,tls;
static volatile LONG failure,callbacks,afterExit,afterWait,destroyed,unwanted;
static VOID CALLBACK NeverRun(ULONG_PTR data) { (void)data;++unwanted; }
static LRESULT CALLBACK Proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    if(msg==WM_NCDESTROY) {
        if(GetCurrentThreadId()!=owner || TlsGetValue(tls)!=(LPVOID)456)failure=__LINE__;
        ++destroyed;
    }
    return DefWindowProcA(hwnd,msg,wp,lp);
}
static VOID CALLBACK TimerApc(LPVOID data,DWORD low,DWORD high) {
    (void)low;(void)high;
    if(GetCurrentThreadId()!=owner || TlsGetValue(tls)!=(LPVOID)456 || data!=(LPVOID)123) { failure=__LINE__;return; }
    ++callbacks;
    if(!QueueUserAPC(NeverRun,GetCurrentThread(),1)) { failure=__LINE__;return; }
    ExitThread(77);
    afterExit=1;
}
static DWORD WINAPI Worker(LPVOID unused) {
    LARGE_INTEGER due;(void)unused;owner=GetCurrentThreadId();
    if(!TlsSetValue(tls,(LPVOID)456))return 1;
    window=CreateWindowExA(0,"TimerExitWnd","",WS_POPUP,0,0,10,10,NULL,NULL,GetModuleHandleA(NULL),NULL);
    mutex=CreateMutexA(NULL,TRUE,NULL);timer=CreateWaitableTimerA(NULL,FALSE,NULL);
    if(!window || !mutex || !timer)return 2;
    due.QuadPart=-10000;
    if(!SetWaitableTimer(timer,&due,2,TimerApc,(LPVOID)123,FALSE))return 3;
    SleepEx(2000,TRUE);
    afterWait=1;return 4;
}
int main(void) {
    WNDCLASSA cls={0};HANDLE thread;DWORD code=999;
    cls.lpfnWndProc=Proc;cls.lpszClassName="TimerExitWnd";cls.hInstance=GetModuleHandleA(NULL);
    CHECK(RegisterClassA(&cls));tls=TlsAlloc();CHECK(tls!=TLS_OUT_OF_INDEXES);
    thread=CreateThread(NULL,0,Worker,NULL,0,NULL);CHECK(thread);
    CHECK(WaitForSingleObject(thread,2000)==WAIT_OBJECT_0 && GetExitCodeThread(thread,&code) && code==77);
    CHECK(callbacks==1 && !failure && !afterExit && !afterWait && !unwanted && destroyed==1 && !IsWindow(window));
    CHECK(WaitForSingleObject(mutex,0)==WAIT_ABANDONED && ReleaseMutex(mutex));
    CHECK(WaitForSingleObject(timer,0)==WAIT_OBJECT_0); /* exit cancellation retains signal */
    Sleep(10);CHECK(SleepEx(0,TRUE)==0 && callbacks==1 && !unwanted);
    CHECK(WaitForSingleObject(timer,0)==WAIT_TIMEOUT);
    CloseHandle(thread);CloseHandle(mutex);CloseHandle(timer);TlsFree(tls);UnregisterClassA("TimerExitWnd",cls.hInstance);
    puts("PASSED: ExitThread inside timer APC, no guest continuation, queued APC drop, owner-window destruction, mutex abandonment and timer cancellation");
    return 0;
}
