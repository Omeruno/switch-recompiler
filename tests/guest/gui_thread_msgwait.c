#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <stdio.h>
#define CHECK(x) do { if (!(x)) { printf("FAIL msgwait line %d error %lu\n",__LINE__,GetLastError()); return __LINE__; } } while (0)
static DWORD mainId;
static HANDLE event;
static HWND window;
static volatile LONG calls;
static LRESULT CALLBACK Proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    if (msg==WM_APP+3) { if (GetCurrentThreadId()!=mainId) return -1; calls++; return 414; }
    return DefWindowProcA(hwnd,msg,wp,lp);
}
static DWORD WINAPI Worker(LPVOID mode) {
    Sleep(3);
    if (mode==(LPVOID)1) return SetEvent(event)?0:1;
    if (mode==(LPVOID)2) return PostThreadMessageA(mainId,WM_APP+2,55,0)?0:2;
    return SendMessageA(window,WM_APP+3,0,0)==414?0:3;
}
static DWORD WINAPI Abandon(LPVOID mutex) {
    return WaitForSingleObject((HANDLE)mutex,2000)==WAIT_OBJECT_0?0:4;
}
static int Join(HANDLE thread) {
    DWORD code=99;
    int ok=WaitForSingleObject(thread,2000)==WAIT_OBJECT_0 && GetExitCodeThread(thread,&code) && code==0;
    CloseHandle(thread);return ok;
}
int main(void) {
    HANDLE objects[2],thread,mutex,semaphore;
    MSG msg;
    DWORD status;
    UINT_PTR timer;
    WNDCLASSA cls={0};
    mainId=GetCurrentThreadId();
    PeekMessageA(&msg,NULL,0,0,PM_NOREMOVE);
    event=CreateEventA(NULL,FALSE,TRUE,NULL);
    objects[0]=event;objects[1]=CreateSemaphoreA(NULL,1,1,NULL);
    semaphore=objects[1];
    CHECK(event && objects[1]);
    CHECK(MsgWaitForMultipleObjects(2,objects,FALSE,0,QS_POSTMESSAGE)==WAIT_OBJECT_0);
    CHECK(MsgWaitForMultipleObjectsEx(2,objects,0,QS_POSTMESSAGE,0)==WAIT_OBJECT_0+1);
    CHECK(MsgWaitForMultipleObjectsEx(0,NULL,0,QS_POSTMESSAGE,0)==WAIT_TIMEOUT);
    CHECK(MsgWaitForMultipleObjectsEx(64,objects,0,QS_POSTMESSAGE,0)==WAIT_FAILED && GetLastError()==ERROR_INVALID_PARAMETER);
    CHECK(MsgWaitForMultipleObjectsEx(1,NULL,0,QS_POSTMESSAGE,0)==WAIT_FAILED);
    CHECK(MsgWaitForMultipleObjectsEx(0,NULL,0,QS_POSTMESSAGE,8)==WAIT_FAILED && GetLastError()==ERROR_INVALID_PARAMETER);
    { HANDLE duplicate[2]={event,event}; CHECK(MsgWaitForMultipleObjectsEx(2,duplicate,0,QS_POSTMESSAGE,0)==WAIT_FAILED && GetLastError()==ERROR_INVALID_PARAMETER); }
    { HANDLE bad=(HANDLE)0x123; CHECK(MsgWaitForMultipleObjectsEx(1,&bad,0,QS_POSTMESSAGE,0)==WAIT_FAILED && GetLastError()==ERROR_INVALID_HANDLE); }
    CHECK(PostMessageA(NULL,WM_APP+1,44,0));
    status=GetQueueStatus(QS_POSTMESSAGE);
    CHECK((HIWORD(status)&QS_POSTMESSAGE) && (LOWORD(status)&QS_POSTMESSAGE));
    status=GetQueueStatus(QS_POSTMESSAGE);
    CHECK((HIWORD(status)&QS_POSTMESSAGE) && !(LOWORD(status)&QS_POSTMESSAGE));
    CHECK(MsgWaitForMultipleObjectsEx(0,NULL,0,QS_POSTMESSAGE,0)==WAIT_TIMEOUT);
    CHECK(MsgWaitForMultipleObjectsEx(0,NULL,0,QS_POSTMESSAGE,MWMO_INPUTAVAILABLE)==WAIT_OBJECT_0);
    CHECK(PeekMessageA(&msg,NULL,0,0,PM_NOREMOVE) && msg.wParam==44);
    CHECK(MsgWaitForMultipleObjects(0,NULL,FALSE,0,QS_POSTMESSAGE)==WAIT_TIMEOUT);
    CHECK(PostMessageA(NULL,WM_APP+1,45,0));
    CHECK(MsgWaitForMultipleObjects(0,NULL,FALSE,0,QS_POSTMESSAGE)==WAIT_OBJECT_0);
    CHECK(PeekMessageA(&msg,NULL,0,0,PM_REMOVE) && msg.wParam==44);
    CHECK(PeekMessageA(&msg,NULL,0,0,PM_REMOVE) && msg.wParam==45);
    CHECK(SetEvent(event) && ReleaseSemaphore(objects[1],1,NULL));
    // Missing queue input must not consume either kernel object in wait-all.
    CHECK(MsgWaitForMultipleObjectsEx(2,objects,3,QS_POSTMESSAGE,MWMO_WAITALL)==WAIT_TIMEOUT);
    CHECK(PostMessageA(NULL,WM_APP+1,46,0));
    CHECK(MsgWaitForMultipleObjectsEx(2,objects,20,QS_POSTMESSAGE,MWMO_WAITALL)==WAIT_OBJECT_0);
    CHECK(WaitForSingleObject(event,0)==WAIT_TIMEOUT && WaitForSingleObject(objects[1],0)==WAIT_TIMEOUT);
    CHECK(PeekMessageA(&msg,NULL,0,0,PM_REMOVE) && msg.wParam==46);
    mutex=CreateMutexA(NULL,FALSE,NULL);CHECK(mutex);
    thread=CreateThread(NULL,0,Abandon,mutex,0,NULL);CHECK(thread && Join(thread));
    objects[1]=mutex;CHECK(SetEvent(event) && PostMessageA(NULL,WM_APP+1,47,0));
    CHECK(MsgWaitForMultipleObjectsEx(2,objects,20,QS_POSTMESSAGE,MWMO_WAITALL)==WAIT_ABANDONED_0+1);
    CHECK(ReleaseMutex(mutex) && PeekMessageA(&msg,NULL,0,0,PM_REMOVE));
    thread=CreateThread(NULL,0,Worker,(LPVOID)1,0,NULL);CHECK(thread);
    CHECK(MsgWaitForMultipleObjects(1,&event,FALSE,2000,QS_POSTMESSAGE)==WAIT_OBJECT_0 && Join(thread));
    thread=CreateThread(NULL,0,Worker,(LPVOID)2,0,NULL);CHECK(thread);
    CHECK(MsgWaitForMultipleObjectsEx(1,&event,2000,QS_POSTMESSAGE,0)==WAIT_OBJECT_0+1 && Join(thread));
    CHECK(PeekMessageA(&msg,NULL,0,0,PM_REMOVE) && msg.message==WM_APP+2 && msg.wParam==55);
    timer=SetTimer(NULL,9,3,NULL);CHECK(timer);
    CHECK(MsgWaitForMultipleObjectsEx(0,NULL,20,QS_POSTMESSAGE,0)==WAIT_TIMEOUT);
    CHECK(MsgWaitForMultipleObjectsEx(0,NULL,20,QS_TIMER,0)==WAIT_OBJECT_0);
    CHECK(MsgWaitForMultipleObjectsEx(0,NULL,0,QS_TIMER,0)==WAIT_TIMEOUT);
    CHECK(MsgWaitForMultipleObjectsEx(0,NULL,0,QS_TIMER,MWMO_INPUTAVAILABLE)==WAIT_OBJECT_0);
    CHECK(PeekMessageA(&msg,NULL,WM_TIMER,WM_TIMER,PM_REMOVE) && msg.message==WM_TIMER);
    CHECK(KillTimer(NULL,timer));
    cls.lpfnWndProc=Proc;cls.hInstance=GetModuleHandleA(NULL);cls.lpszClassName="MsgWaitTest";
    CHECK(RegisterClassA(&cls));
    window=CreateWindowA(cls.lpszClassName,"wait",0,0,0,50,50,NULL,NULL,cls.hInstance,NULL);CHECK(window);
    thread=CreateThread(NULL,0,Worker,NULL,0,NULL);CHECK(thread);
    CHECK(MsgWaitForMultipleObjectsEx(0,NULL,2000,QS_SENDMESSAGE,0)==WAIT_OBJECT_0 && calls==0);
    PeekMessageA(&msg,NULL,WM_APP+100,WM_APP+100,PM_NOREMOVE);
    CHECK(calls==1 && Join(thread));
    CHECK(PostMessageA(NULL,WM_APP+1,48,0) && PeekMessageA(&msg,NULL,0,0,PM_NOREMOVE));
    thread=CreateThread(NULL,0,Worker,(LPVOID)2,0,NULL);CHECK(thread);
    CHECK(WaitMessage() && Join(thread));
    CHECK(PeekMessageA(&msg,NULL,0,0,PM_REMOVE) && msg.wParam==48);
    CHECK(PeekMessageA(&msg,NULL,0,0,PM_REMOVE) && msg.wParam==55);
    CHECK(DestroyWindow(window));CloseHandle(event);CloseHandle(mutex);CloseHandle(semaphore);
    puts("PASSED: MsgWait HANDLE/input waits, atomic wait-all, abandonment, queue change bits, timers and sent-message wakeups");
    return 0;
}
