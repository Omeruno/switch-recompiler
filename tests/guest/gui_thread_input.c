#include <windows.h>
#include <stdio.h>
#include "acceptance_trace.h"
#define CHECK(x) do{if(!(x)){ACCEPT_FAIL(#x);printf("FAIL input line %d error %lu handler %ld\n",__LINE__,GetLastError(),failure);ExitProcess(1);}}while(0)
#define REQUIRE(x) do{if(!(x)){ACCEPT_FAIL(#x);failure=__LINE__;return 0;}}while(0)
enum { SNAPSHOT=WM_APP+1,TAKE_CAPTURE,PROBE,EXIT_FOCUS,EXIT_CAPTURE };
/* The native inspector enables platform-event probes; normal EXE execution
   remains a complete, unattended state/reentrancy/lifecycle test on Switch. */
__declspec(dllexport) volatile DWORD InputTestProbe,InputTestStage;
static HWND a,a1,a2,a3,b,b1,doomed,crossChild;
static DWORD mainId,workerId,tls;
static HANDLE ready;
static volatile LONG failure,redirectFocus,destroyFocus,redirectCapture,exitFocus,exitCapture,afterExit;
static volatile LONG setA2,captureChanges;
static void input_probe(DWORD stage){
    MSG msg;unsigned keys=0,chars=0,moves=0,down=0,up=0;
    InputTestStage=stage;Sleep(5); // advance the normal platform poll throttle
    while(PeekMessageA(&msg,NULL,WM_KEYFIRST,WM_KEYLAST,PM_REMOVE)){
        CHECK(stage==2 && msg.hwnd==b1);
        if(msg.message==WM_KEYDOWN){CHECK(msg.wParam=='B');++keys;}
        if(msg.message==WM_CHAR){CHECK(msg.wParam=='b');++chars;}
    }
    while(PeekMessageA(&msg,NULL,WM_MOUSEFIRST,WM_MOUSELAST,PM_REMOVE)){
        CHECK((stage==3 && msg.hwnd==b)||(stage==4 && msg.hwnd==b1));
        moves+=msg.message==WM_MOUSEMOVE;down+=msg.message==WM_LBUTTONDOWN;up+=msg.message==WM_LBUTTONUP;
    }
    if(stage==2)CHECK(keys==1 && chars==1);
    if(stage==3)CHECK(moves==1);
    if(stage==4)CHECK(moves==1 && down==1 && up==1);
    InputTestStage=0;
}
static LRESULT CALLBACK proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    if(msg==WM_SETFOCUS || msg==WM_KILLFOCUS || msg==WM_CAPTURECHANGED){
        DWORD owner=GetWindowThreadProcessId(hwnd,NULL);
        REQUIRE(GetCurrentThreadId()==owner && TlsGetValue(tls)==(LPVOID)(owner==mainId?111:222));
    }
    if(msg==WM_KILLFOCUS && redirectFocus && hwnd==a1){redirectFocus=0;SetFocus(a3);}
    if(msg==WM_KILLFOCUS && destroyFocus && hwnd==a1){destroyFocus=0;DestroyWindow(a2);}
    if(msg==WM_SETFOCUS && hwnd==a2)++setA2;
    if(msg==WM_CAPTURECHANGED){
        ++captureChanges;
        if(hwnd==a1 && redirectCapture){redirectCapture=0;SetCapture(a3);}
        if(hwnd==b1 && exitCapture){ExitThread(63);++afterExit;}
    }
    if(msg==WM_SETFOCUS && hwnd==b1 && exitFocus){ExitThread(62);++afterExit;}
    if(msg==WM_MOUSEACTIVATE && hwnd==doomed){DestroyWindow(doomed);return MA_ACTIVATE;}
    if(msg==SNAPSHOT){
        REQUIRE(GetCurrentThreadId()==workerId && GetFocus()==b1 && GetActiveWindow()==b);
        REQUIRE(GetForegroundWindow()==a && !GetCapture());
        REQUIRE(ReleaseCapture() && !GetCapture());
        REQUIRE(!SetCapture(NULL) && !SetFocus(a1) && !SetCapture(a1) && !SetActiveWindow(a));
        REQUIRE(!DestroyWindow(a1) && GetFocus()==b1 && GetActiveWindow()==b);
        return 101;
    }
    if(msg==TAKE_CAPTURE){SetCapture(b1);REQUIRE(GetCapture()==b1);return 102;}
    if(msg==PROBE){input_probe((DWORD)wp);return 103;}
    if(msg==EXIT_FOCUS){exitFocus=1;SetFocus(b);SetFocus(b1);++afterExit;return 0;}
    if(msg==EXIT_CAPTURE){exitCapture=1;ReleaseCapture();++afterExit;return 0;}
    return DefWindowProcA(hwnd,msg,wp,lp);
}
static DWORD WINAPI worker(LPVOID unused){
    MSG msg;(void)unused;ACCEPT_STEP("worker begin");workerId=GetCurrentThreadId();TlsSetValue(tls,(LPVOID)222);
    b=CreateWindowA("ThreadInput","worker",WS_OVERLAPPEDWINDOW,400,20,200,180,NULL,NULL,GetModuleHandleA(NULL),NULL);
    b1=CreateWindowExA(WS_EX_NOPARENTNOTIFY,"ThreadInput","worker child",WS_CHILD|WS_VISIBLE,10,10,100,80,b,NULL,GetModuleHandleA(NULL),NULL);
    ACCEPT_STEP("worker windows created");
    CHECK(b && b1);ShowWindow(b,SW_SHOWNOACTIVATE);
    SetActiveWindow(b);SetFocus(b1);
    crossChild=CreateWindowExA(WS_EX_NOPARENTNOTIFY,"ThreadInput","cross parent",WS_CHILD,0,0,5,5,a,NULL,GetModuleHandleA(NULL),NULL);
    CHECK(crossChild && !SetFocus(crossChild) && !SetActiveWindow(b1) && !SetForegroundWindow(b1) && GetForegroundWindow()==a);
    CHECK(GetFocus()==b1 && GetActiveWindow()==b);
    ACCEPT_STEP("worker ready");SetEvent(ready);
    while(GetMessageA(&msg,NULL,0,0)>0)DispatchMessageA(&msg);
    return 64;
}
static HANDLE start_worker(void){
    HANDLE thread;ResetEvent(ready);exitFocus=exitCapture=0;
    thread=CreateThread(NULL,0,worker,NULL,0,NULL);
    CHECK(thread && WaitForSingleObject(ready,5000)==WAIT_OBJECT_0);return thread;
}
int main(void){
#ifndef ACCEPTANCE_AGGREGATE
    AcceptanceTraceBegin("standalone GUI input");
#endif
    ACCEPT_STEP("input begin");
    WNDCLASSA cls={0};GUITHREADINFO info={0};HANDLE thread;DWORD code;MSG msg;unsigned keys=0,chars=0;
    mainId=GetCurrentThreadId();tls=TlsAlloc();CHECK(tls!=TLS_OUT_OF_INDEXES && TlsSetValue(tls,(LPVOID)111));
    cls.lpfnWndProc=proc;cls.hInstance=GetModuleHandleA(NULL);cls.lpszClassName="ThreadInput";CHECK(RegisterClassA(&cls));
    a=CreateWindowA(cls.lpszClassName,"main",WS_OVERLAPPEDWINDOW,20,20,250,180,NULL,NULL,cls.hInstance,NULL);
    a1=CreateWindowExA(WS_EX_NOPARENTNOTIFY,cls.lpszClassName,"one",WS_CHILD|WS_VISIBLE,10,10,60,60,a,NULL,cls.hInstance,NULL);
    a2=CreateWindowExA(WS_EX_NOPARENTNOTIFY,cls.lpszClassName,"two",WS_CHILD|WS_VISIBLE,80,10,60,60,a,NULL,cls.hInstance,NULL);
    a3=CreateWindowExA(WS_EX_NOPARENTNOTIFY,cls.lpszClassName,"three",WS_CHILD|WS_VISIBLE,150,10,60,60,a,NULL,cls.hInstance,NULL);
    ready=CreateEventA(NULL,TRUE,FALSE,NULL);CHECK(a && a1 && a2 && a3 && ready);
    ACCEPT_STEP("main windows created");
    ShowWindow(a,SW_SHOW);CHECK(SetForegroundWindow(a));SetFocus(a1);SetCapture(a1);
    ACCEPT_STEP("create first worker");thread=start_worker();
    CHECK(GetFocus()==a1 && GetActiveWindow()==a && GetCapture()==a1 && GetForegroundWindow()==a);
    ACCEPT_STEP("cross-thread snapshot");
    CHECK(SendMessageA(b,SNAPSHOT,0,0)==101 && !failure && GetCapture()==a1);
    ACCEPT_STEP("GUIThreadInfo and foreign queues");
    info.cbSize=sizeof(info);CHECK(GetGUIThreadInfo(workerId,&info) && info.hwndActive==b && info.hwndFocus==b1 && !info.hwndCapture);
    CHECK(GetGUIThreadInfo(0,&info) && info.hwndActive==a && info.hwndFocus==a1 && info.hwndCapture==a1);
    info.cbSize=47;CHECK(!GetGUIThreadInfo(mainId,&info) && GetLastError()==ERROR_INVALID_PARAMETER);info.cbSize=sizeof(info);
    CHECK(!GetGUIThreadInfo(0x12345678,&info));
    CHECK(!SetFocus(b1) && !SetCapture(b1) && GetFocus()==a1 && GetCapture()==a1);
    EnableWindow(a2,FALSE);CHECK(!SetFocus(a2) && GetLastError()==ERROR_INVALID_PARAMETER && GetFocus()==a1);EnableWindow(a2,TRUE);
    ACCEPT_STEP("nested focus redirection");
    redirectFocus=1;setA2=0;CHECK(SetFocus(a2)==a1 && GetFocus()==a3 && !setA2);
    ACCEPT_STEP("destroy focus target in callback");
    SetFocus(a1);destroyFocus=1;CHECK(SetFocus(a2)==a1 && !IsWindow(a2) && !GetFocus() && !setA2);
    ACCEPT_STEP("nested capture redirection");
    SetFocus(a1);redirectCapture=1;CHECK(SetCapture(a)==a1 && GetCapture()==a3);
    ACCEPT_STEP("cross-thread capture transfer");
    SetCapture(a1);CHECK(SendMessageA(b,TAKE_CAPTURE,0,0)==102 && !GetCapture() && captureChanges>=2);
    CHECK(ReleaseCapture() && !GetCapture());CHECK(GetGUIThreadInfo(workerId,&info) && info.hwndCapture==b1);
    if(InputTestProbe){
        CHECK(SendMessageA(b,PROBE,1,0)==103);
        while(PeekMessageA(&msg,NULL,WM_KEYFIRST,WM_KEYLAST,PM_REMOVE)){
            CHECK(msg.hwnd==a1);if(msg.message==WM_KEYDOWN){CHECK(msg.wParam=='A');++keys;}
            if(msg.message==WM_CHAR){CHECK(msg.wParam=='a');++chars;}
        }
        CHECK(keys==1 && chars==1);
        CHECK(SetForegroundWindow(b));CHECK(SendMessageA(b,PROBE,2,0)==103);
        CHECK(!PeekMessageA(&msg,NULL,WM_KEYFIRST,WM_KEYLAST,PM_REMOVE));
        CHECK(SetForegroundWindow(a));SetFocus(a1);SetCapture(a1);
        CHECK(SendMessageA(b,PROBE,3,0)==103);
        CHECK(SetForegroundWindow(b) && SendMessageA(b,TAKE_CAPTURE,0,0)==102);
        CHECK(SendMessageA(b,PROBE,4,0)==103);
        doomed=CreateWindowA(cls.lpszClassName,"destroy on mouse activation",WS_OVERLAPPEDWINDOW,100,300,180,140,NULL,NULL,cls.hInstance,NULL);
        CHECK(doomed);ShowWindow(doomed,SW_SHOWNOACTIVATE);
        CHECK(SendMessageA(b,PROBE,5,0)==103 && !IsWindow(doomed) && GetForegroundWindow()==b);
    }
    CHECK(SetForegroundWindow(a));SetFocus(a1);SetCapture(a1);
    ACCEPT_STEP("ExitThread from WM_SETFOCUS");
    SendMessageA(b,EXIT_FOCUS,0,0);
    CHECK(WaitForSingleObject(thread,5000)==WAIT_OBJECT_0 && GetExitCodeThread(thread,&code) && code==62 && CloseHandle(thread));
    CHECK(!afterExit && !failure && !IsWindow(b) && !IsWindow(b1) && !GetGUIThreadInfo(workerId,&info));
    CHECK(GetFocus()==a1 && GetCapture()==a1 && GetActiveWindow()==a);
    ACCEPT_STEP("create second worker");
    thread=start_worker();CHECK(SendMessageA(b,TAKE_CAPTURE,0,0)==102);
    ACCEPT_STEP("ExitThread from WM_CAPTURECHANGED");
    SendMessageA(b,EXIT_CAPTURE,0,0);
    CHECK(WaitForSingleObject(thread,5000)==WAIT_OBJECT_0 && GetExitCodeThread(thread,&code) && code==63 && CloseHandle(thread));
    CHECK(!afterExit && !failure && !GetCapture() && GetFocus()==a1 && !IsWindow(b1));
    ACCEPT_STEP("disable and destroy main window");
    SetCapture(a1);EnableWindow(a,FALSE);CHECK(!GetFocus() && !GetCapture());EnableWindow(a,TRUE);SetFocus(a1);SetCapture(a1);
    CHECK(DestroyWindow(a) && !GetFocus() && !GetCapture() && !GetActiveWindow());
    CHECK(CloseHandle(ready) && TlsFree(tls));
    ACCEPT_STEP("input passed");
    puts("PASSED: per-thread focus/activation/capture, foreign-queue rejection, nested focus/destruction/capture and callback thread-exit cleanup");return 0;
}
