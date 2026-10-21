#include <windows.h>
#include <stdio.h>
#define CHECK(x) do { if (!(x)) { printf("FAIL send line %d error %lu handler=%ld\n",__LINE__,GetLastError(),failure); return __LINE__; } } while (0)
#define REQUIRE(x) do { if (!(x)) { failure=__LINE__; return 0; } } while (0)
enum { PING=WM_APP+1,PONG,REPLY,BLOCK,EXIT_NOW,DESTROY_BUTTON };
static HWND mainWindow,workerWindow,button;
static HANDLE ready,continued,replied,blocked,unblock,blockDone;
static DWORD mainId,workerId,tlsIndex;
static volatile LONG failure,pings,replyDone,commands;
static LRESULT CALLBACK WindowProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    if (msg==PING) {
        REQUIRE(GetCurrentThreadId()==workerId && TlsGetValue(tlsIndex)==(LPVOID)222);
        REQUIRE(InSendMessage() && (InSendMessageEx(NULL)&ISMEX_SEND));
        pings++;
        REQUIRE(SendMessageA(mainWindow,PONG,wp,lp)==wp+lp);
        return 414;
    }
    if (msg==PONG) {
        REQUIRE(GetCurrentThreadId()==mainId && TlsGetValue(tlsIndex)==(LPVOID)111);
        REQUIRE(InSendMessage() && (InSendMessageEx(NULL)&ISMEX_SEND));
        return wp+lp;
    }
    if (msg==REPLY) {
        REQUIRE(ReplyMessage(515));
        REQUIRE((InSendMessageEx(NULL)&(ISMEX_SEND|ISMEX_REPLIED))==(ISMEX_SEND|ISMEX_REPLIED));
        REQUIRE(!ReplyMessage(616));
        SetEvent(replied);
        REQUIRE(WaitForSingleObject(continued,2000)==WAIT_OBJECT_0);
        replyDone=1;
        return 999;
    }
    if (msg==BLOCK) {
        REQUIRE(InSendMessageEx(NULL)&ISMEX_NOTIFY);
        SetEvent(blocked);
        REQUIRE(WaitForSingleObject(unblock,2000)==WAIT_OBJECT_0);
        SetEvent(blockDone);
        return 0;
    }
    if (msg==WM_COMMAND && (LOWORD(wp)==99)) {
        REQUIRE(GetCurrentThreadId()==mainId);
        commands++;
        // A -> worker BUTTON -> main WM_COMMAND -> worker destroys BUTTON.
        // Native button frames still retain Window*/control state afterward.
        REQUIRE(SendMessageA(workerWindow,DESTROY_BUTTON,0,0)==919);
        return 0;
    }
    if (msg==DESTROY_BUTTON) {
        REQUIRE(GetCurrentThreadId()==workerId && DestroyWindow(button));
        return 919;
    }
    if (msg==EXIT_NOW) ExitThread(88);
    return DefWindowProcA(hwnd,msg,wp,lp);
}
static DWORD WINAPI Worker(LPVOID unused) {
    MSG msg;
    (void)unused;
    workerId=GetCurrentThreadId();
    if (!TlsSetValue(tlsIndex,(LPVOID)222)) return 10;
    workerWindow=CreateWindowA("ThreadSend","worker",0,0,0,50,50,NULL,NULL,GetModuleHandleA(NULL),NULL);
    button=CreateWindowExA(WS_EX_NOPARENTNOTIFY,"BUTTON","click",WS_CHILD,0,0,20,20,mainWindow,(HMENU)99,GetModuleHandleA(NULL),NULL);
    if (!workerWindow || !button) return 11;
    SetEvent(ready);
    while (GetMessageA(&msg,NULL,0,0)>0) DispatchMessageA(&msg);
    return 12;
}
int main(void) {
    WNDCLASSA cls={0};
    HANDLE thread;
    MSG msg;
    DWORD code;
    DWORD_PTR result=0;
    LONG before;
    mainId=GetCurrentThreadId();tlsIndex=TlsAlloc();
    CHECK(tlsIndex!=TLS_OUT_OF_INDEXES && TlsSetValue(tlsIndex,(LPVOID)111));
    cls.lpfnWndProc=WindowProc;cls.hInstance=GetModuleHandleA(NULL);cls.lpszClassName="ThreadSend";
    CHECK(RegisterClassA(&cls));
    mainWindow=CreateWindowA(cls.lpszClassName,"main",0,0,0,50,50,NULL,NULL,cls.hInstance,NULL);
    ready=CreateEventA(NULL,TRUE,FALSE,NULL);continued=CreateEventA(NULL,TRUE,FALSE,NULL);
    replied=CreateEventA(NULL,TRUE,FALSE,NULL);blocked=CreateEventA(NULL,TRUE,FALSE,NULL);
    unblock=CreateEventA(NULL,TRUE,FALSE,NULL);blockDone=CreateEventA(NULL,TRUE,FALSE,NULL);
    CHECK(mainWindow && ready && continued && replied && blocked && unblock && blockDone);
    thread=CreateThread(NULL,0,Worker,NULL,0,NULL);
    CHECK(thread && WaitForSingleObject(ready,2000)==WAIT_OBJECT_0);
    CHECK(GetWindowThreadProcessId(workerWindow,NULL)==workerId && workerId!=mainId);
    CHECK(PostMessageA(mainWindow,WM_APP+99,123,0));
    CHECK(SendMessageA(workerWindow,PING,100,213)==414 && !failure);
    CHECK(!InSendMessage() && !ReplyMessage(0));
    CHECK(PeekMessageA(&msg,NULL,WM_APP+99,WM_APP+99,PM_REMOVE) && msg.wParam==123);
    CHECK(SendMessageA(workerWindow,REPLY,0,0)==515 && !replyDone);
    CHECK(WaitForSingleObject(replied,2000)==WAIT_OBJECT_0 && SetEvent(continued));
    CHECK(SendMessageA(workerWindow,PING,1,2)==414 && replyDone && !failure);
    SendMessageA(button,BM_CLICK,0,0);
    CHECK(commands==1 && !failure && !IsWindow(button));
    CHECK(SendNotifyMessageA(workerWindow,BLOCK,0,0));
    CHECK(WaitForSingleObject(blocked,2000)==WAIT_OBJECT_0);
    before=pings;
    SetLastError(0);
    CHECK(!SendMessageTimeoutA(workerWindow,PING,0,0,SMTO_NORMAL,3,&result));
    CHECK(GetLastError()==ERROR_TIMEOUT && pings==before);
    CHECK(SetEvent(unblock) && WaitForSingleObject(blockDone,2000)==WAIT_OBJECT_0);
    CHECK(SendMessageA(workerWindow,PING,1,2)==414 && pings==before+1);
    // SMTO_BLOCK suppresses the sender's reentrant PONG pump until timeout.
    CHECK(!SendMessageTimeoutA(workerWindow,PING,1,2,SMTO_BLOCK,3,&result));
    CHECK(GetLastError()==ERROR_TIMEOUT);
    PeekMessageA(&msg,NULL,WM_APP+200,WM_APP+200,PM_NOREMOVE);
    CHECK(SendMessageA(workerWindow,PING,1,2)==414 && !failure);
    SetLastError(0);
    CHECK(SendMessageA(workerWindow,EXIT_NOW,0,0)==0);
    CHECK(WaitForSingleObject(thread,2000)==WAIT_OBJECT_0);
    CHECK(GetExitCodeThread(thread,&code) && code==88 && !IsWindow(workerWindow));
    CHECK(!SendMessageTimeoutA(workerWindow,PING,0,0,SMTO_NORMAL,3,&result));
    CHECK(GetLastError()==ERROR_INVALID_WINDOW_HANDLE);
    CHECK(DestroyWindow(mainWindow) && TlsFree(tlsIndex));
    CloseHandle(thread);CloseHandle(ready);CloseHandle(continued);CloseHandle(replied);
    CloseHandle(blocked);CloseHandle(unblock);CloseHandle(blockDone);
    puts("PASSED: cross-thread SendMessage owner CPU/TLS, reentrant sends, ReplyMessage, notify, timeouts and window lifetime");
    return 0;
}
