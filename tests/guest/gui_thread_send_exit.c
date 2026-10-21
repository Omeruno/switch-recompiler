#include <windows.h>
#include <stdio.h>
#define CHECK(x) do { if (!(x)) { printf("FAIL send exit line %d error %lu\n",__LINE__,GetLastError()); return __LINE__; } } while (0)
static HWND window;
static HANDLE ready,stop;
static DWORD ownerId;
static volatile LONG senderStarted,handled,destroyed,badOwner;
static LRESULT CALLBACK Proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    if (msg==WM_APP+1) { handled++; if (GetCurrentThreadId()!=ownerId) badOwner=1; return 99; }
    if (msg==WM_DESTROY) { destroyed++; if (GetCurrentThreadId()!=ownerId) badOwner=1; }
    return DefWindowProcA(hwnd,msg,wp,lp);
}
static DWORD WINAPI Owner(LPVOID unused) {
    (void)unused;
    ownerId=GetCurrentThreadId();
    window=CreateWindowA("SendExit","",0,0,0,30,30,NULL,NULL,GetModuleHandleA(NULL),NULL);
    CHECK(window && SetEvent(ready));
    // Kernel waits must not dispatch sent or posted USER32 messages.
    CHECK(WaitForSingleObject(stop,2000)==WAIT_OBJECT_0);
    return 73;
}
static DWORD WINAPI Sender(LPVOID unused) {
    DWORD error;
    LRESULT result;
    (void)unused;
    senderStarted=1;
    result=SendMessageA(window,WM_APP+1,0,0);
    error=GetLastError();
    CHECK(result==0 && (error==ERROR_INVALID_WINDOW_HANDLE || error==ERROR_INVALID_THREAD_ID));
    return 0;
}
int main(void) {
    WNDCLASSA cls={0};
    HANDLE owner,sender;
    DWORD code,start;
    cls.lpfnWndProc=Proc;cls.hInstance=GetModuleHandleA(NULL);cls.lpszClassName="SendExit";
    CHECK(RegisterClassA(&cls));
    ready=CreateEventA(NULL,TRUE,FALSE,NULL);stop=CreateEventA(NULL,TRUE,FALSE,NULL);
    CHECK(ready && stop);
    owner=CreateThread(NULL,0,Owner,NULL,0,NULL);
    CHECK(owner && WaitForSingleObject(ready,2000)==WAIT_OBJECT_0);
    CHECK(PostMessageA(window,WM_APP+1,0,0));
    sender=CreateThread(NULL,0,Sender,NULL,0,NULL);
    CHECK(sender);
    start=GetTickCount();
    while (!senderStarted && GetTickCount()-start<2000) Sleep(0);
    CHECK(senderStarted);
    Sleep(5);
    CHECK(!handled && GetExitCodeThread(sender,&code) && code==STILL_ACTIVE);
    CHECK(SetEvent(stop) && WaitForSingleObject(owner,2000)==WAIT_OBJECT_0);
    CHECK(WaitForSingleObject(sender,2000)==WAIT_OBJECT_0 && GetExitCodeThread(sender,&code) && code==0);
    CHECK(!IsWindow(window) && !handled && !badOwner && destroyed==1);
    CloseHandle(owner);CloseHandle(sender);CloseHandle(ready);CloseHandle(stop);
    puts("PASSED: kernel wait does not pump USER32, owner exit cancels pending sends and destroys its windows");
    return 0;
}
