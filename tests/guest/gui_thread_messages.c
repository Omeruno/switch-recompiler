#include <windows.h>
#include <stdio.h>
#define CHECK(x) do { if (!(x)) { printf("FAIL queue line %d error %lu\n",__LINE__,GetLastError()); return __LINE__; } } while (0)
static HANDLE gate,ready;
static DWORD mainId,workerId;
static HWND workerWindow;
static UINT_PTR timer;
static volatile LONG workerTimers,mainTimers;
static void CALLBACK WorkerTimer(HWND h,UINT m,UINT_PTR id,DWORD time) {
    (void)h;(void)m;(void)id;(void)time;
    if (GetCurrentThreadId()==workerId) workerTimers++;
}
static void CALLBACK MainTimer(HWND h,UINT m,UINT_PTR id,DWORD time) {
    (void)h;(void)m;(void)id;(void)time;
    if (GetCurrentThreadId()==mainId) mainTimers++;
}
static DWORD WINAPI Worker(LPVOID unused) {
    MSG msg;
    BYTE keys[256]={0};
    (void)unused;
    CHECK(WaitForSingleObject(gate,2000)==WAIT_OBJECT_0);
    PeekMessageA(&msg,NULL,0,0,PM_NOREMOVE); // creates this thread's queue
    workerWindow=CreateWindowA("STATIC","worker",0,0,0,20,20,NULL,NULL,GetModuleHandleA(NULL),NULL);
    CHECK(workerWindow);
    keys['A']=0x80;
    CHECK(SetKeyboardState(keys));
    timer=SetTimer(NULL,42,10,WorkerTimer);
    CHECK(timer && SetEvent(ready));
    CHECK(GetMessageA(&msg,(HWND)-1,WM_APP+2,WM_APP+2)==1);
    CHECK(!msg.hwnd && msg.message==WM_APP+2 && msg.wParam==222);
    CHECK(GetKeyState('A') & 0x8000);
    PostQuitMessage(77);
    CHECK(PeekMessageA(&msg,NULL,WM_APP+99,WM_APP+99,PM_NOREMOVE));
    CHECK(msg.message==WM_QUIT && msg.wParam==77);
    CHECK(GetMessageA(&msg,(HWND)-1,WM_APP+99,WM_APP+99)==0 && msg.wParam==77);
    CHECK(GetMessageA(&msg,(HWND)-1,WM_TIMER,WM_TIMER)==1);
    CHECK(msg.message==WM_TIMER && msg.lParam==(LPARAM)WorkerTimer && msg.wParam==timer);
    DispatchMessageA(&msg);
    CHECK(workerTimers==1 && !mainTimers && KillTimer(NULL,timer));
    CHECK(PostThreadMessageA(mainId,WM_APP+3,333,0));
    return 0; // owned window must disappear on thread exit
}
int main(void) {
    HANDLE thread;
    DWORD code,pid;
    MSG msg;
    BYTE keys[256]={0};
    UINT_PTR ownTimer;
    mainId=GetCurrentThreadId();
    PeekMessageA(&msg,NULL,0,0,PM_NOREMOVE);
    CHECK(SetKeyboardState(keys));
    gate=CreateEventA(NULL,TRUE,FALSE,NULL);ready=CreateEventA(NULL,TRUE,FALSE,NULL);
    CHECK(gate && ready);
    thread=CreateThread(NULL,0,Worker,NULL,CREATE_SUSPENDED,&workerId);
    CHECK(thread);
    CHECK(!PostThreadMessageA(workerId,WM_APP+2,0,0) && GetLastError()==ERROR_INVALID_THREAD_ID);
    CHECK(ResumeThread(thread)==1 && SetEvent(gate));
    CHECK(WaitForSingleObject(ready,2000)==WAIT_OBJECT_0);
    CHECK(GetWindowThreadProcessId(workerWindow,&pid)==workerId && pid==GetCurrentProcessId());
    CHECK(!DestroyWindow(workerWindow) && IsWindow(workerWindow));
    CHECK(PostMessageA(NULL,WM_APP+1,111,0));
    ownTimer=SetTimer(NULL,42,10,MainTimer);
    CHECK(ownTimer);
    CHECK(PostThreadMessageA(workerId,WM_APP+2,222,0));
    CHECK(WaitForSingleObject(thread,2000)==WAIT_OBJECT_0);
    CHECK(GetExitCodeThread(thread,&code) && code==0);
    CHECK(!IsWindow(workerWindow));
    CHECK(!(GetKeyState('A') & 0x8000));
    CHECK(PeekMessageA(&msg,NULL,WM_APP+1,WM_APP+1,PM_REMOVE) && msg.wParam==111);
    CHECK(PeekMessageA(&msg,(HWND)-1,WM_APP+3,WM_APP+3,PM_REMOVE) && msg.wParam==333);
    CHECK(!PeekMessageA(&msg,NULL,WM_QUIT,WM_QUIT,PM_REMOVE));
    CHECK(GetMessageA(&msg,(HWND)-1,WM_TIMER,WM_TIMER)==1 && msg.lParam==(LPARAM)MainTimer);
    DispatchMessageA(&msg);
    CHECK(mainTimers==1 && workerTimers==1 && KillTimer(NULL,ownTimer));
    CHECK(!PostThreadMessageA(workerId,WM_APP+2,0,0) && GetLastError()==ERROR_INVALID_THREAD_ID);
    CHECK(!PostMessageA(workerWindow,WM_APP,0,0) && GetLastError()==ERROR_INVALID_WINDOW_HANDLE);
    CloseHandle(thread);CloseHandle(gate);CloseHandle(ready);
    puts("PASSED: per-thread posted/quit/timer/key queues, HWND ownership and GUI thread teardown");
    return 0;
}
