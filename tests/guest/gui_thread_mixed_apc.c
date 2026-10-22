#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <stdio.h>
#define CHECK(x) do { if (!(x)) { printf("FAIL mixed APC line %d error %lu phase %ld failure %ld\n",__LINE__,GetLastError(),phase,failure); return __LINE__; } } while (0)
#define REQUIRE(x) do { if (!(x)) { failure=__LINE__;return; } } while (0)
static HANDLE entered,gate,token,timer;
static HWND window;
static DWORD owner,tls,roundNo;
static volatile LONG phase,failure,timerCalls,userCalls,sentCalls;
static const char timerName[]="StageC.Mixed.Timer";
static LRESULT CALLBACK Proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    if(msg==WM_APP+1) {
        if(GetCurrentThreadId()!=owner || TlsGetValue(tls)!=(LPVOID)456 || wp!=roundNo)failure=__LINE__;
        ++sentCalls;return 321;
    }
    return DefWindowProcA(hwnd,msg,wp,lp);
}
static VOID CALLBACK UserApc(ULONG_PTR data) {
    REQUIRE(GetCurrentThreadId()==owner && TlsGetValue(tls)==(LPVOID)456 && data==roundNo);
    REQUIRE(phase==2);++userCalls;
}
static VOID CALLBACK TimerApc(LPVOID data,DWORD low,DWORD high) {
    (void)low;(void)high;
    REQUIRE(GetCurrentThreadId()==owner && TlsGetValue(tls)==(LPVOID)456 && (ULONG_PTR)data==roundNo);
    ++timerCalls;phase=1;REQUIRE(SetEvent(entered));
    /* Even with an alertable MsgWait outside, this wait must not dispatch
       ordinary APCs or sent window messages until the callback finishes. */
    REQUIRE(WaitForSingleObject(gate,2000)==WAIT_OBJECT_0);
    REQUIRE(!userCalls && !sentCalls);phase=2;
}
static DWORD WINAPI Worker(LPVOID unused) {
    LARGE_INTEGER due;MSG msg;
    (void)unused;owner=GetCurrentThreadId();
    if(!TlsSetValue(tls,(LPVOID)456))return 1;
    window=CreateWindowExA(0,"MixedApcWnd","",WS_POPUP,0,0,10,10,NULL,NULL,GetModuleHandleA(NULL),NULL);
    if(!window)return 2;
    PeekMessageA(&msg,NULL,0,0,PM_NOREMOVE);
    timer=CreateWaitableTimerA(NULL,FALSE,timerName);if(!timer)return 3;
    due.QuadPart=-10000;
    if(!SetWaitableTimer(timer,&due,2,TimerApc,(LPVOID)(ULONG_PTR)roundNo,FALSE))return 4;
    if(MsgWaitForMultipleObjectsEx(1,&token,2000,QS_POSTMESSAGE|QS_SENDMESSAGE,MWMO_ALERTABLE|MWMO_INPUTAVAILABLE)!=WAIT_IO_COMPLETION)return 5;
    if(timerCalls!=1 || userCalls!=1 || sentCalls || failure || phase!=2)return 6;
    if(WaitForSingleObject(token,0)!=WAIT_OBJECT_0 || WaitForSingleObject(token,0)!=WAIT_TIMEOUT)return 7;
    if(MsgWaitForMultipleObjectsEx(1,&token,0,QS_POSTMESSAGE|QS_SENDMESSAGE,MWMO_INPUTAVAILABLE)!=WAIT_OBJECT_0+1 || sentCalls)return 8;
    if(!PeekMessageA(&msg,NULL,WM_APP+2,WM_APP+2,PM_REMOVE) || msg.hwnd || msg.message!=WM_APP+2 || msg.wParam!=roundNo)return 9;
    if(sentCalls!=1 || failure || PeekMessageA(&msg,NULL,WM_APP+2,WM_APP+2,PM_REMOVE))return 10;
    if(!DestroyWindow(window))return 11;window=NULL;
    return 0;
}
static int Join(HANDLE t) {
    DWORD code=999;
    int ok=WaitForSingleObject(t,2000)==WAIT_OBJECT_0 && GetExitCodeThread(t,&code) && !code;
    if(!ok)printf("mixed worker exit %lu\n",code);
    CloseHandle(t);return ok;
}
int main(void) {
    WNDCLASSA cls={0};HANDLE thread,probe;
    cls.lpfnWndProc=Proc;cls.lpszClassName="MixedApcWnd";cls.hInstance=GetModuleHandleA(NULL);
    CHECK(RegisterClassA(&cls));tls=TlsAlloc();CHECK(tls!=TLS_OUT_OF_INDEXES);
    entered=CreateEventA(NULL,FALSE,FALSE,NULL);gate=CreateEventA(NULL,FALSE,FALSE,NULL);
    token=CreateEventA(NULL,FALSE,FALSE,NULL);CHECK(entered && gate && token);
    for(roundNo=1;roundNo<=12;++roundNo) {
        phase=failure=timerCalls=userCalls=sentCalls=0;timer=NULL;window=NULL;
        thread=CreateThread(NULL,0,Worker,NULL,0,NULL);CHECK(thread);
        CHECK(WaitForSingleObject(entered,2000)==WAIT_OBJECT_0 && phase==1 && timerCalls==1);
        CHECK(SendNotifyMessageA(window,WM_APP+1,roundNo,0));
        CHECK(PostThreadMessageA(owner,WM_APP+2,roundNo,0));
        CHECK(QueueUserAPC(UserApc,thread,roundNo) && SetEvent(token));
        Sleep(5);CHECK(phase==1 && timerCalls==1 && !userCalls && !sentCalls);
        CHECK(CancelWaitableTimer(timer) && CloseHandle(timer));timer=NULL;
        /* Last close while APC's native stack is parked must expire its name. */
        probe=CreateEventA(NULL,FALSE,FALSE,timerName);CHECK(probe && GetLastError()!=ERROR_ALREADY_EXISTS);
        CHECK(CloseHandle(probe) && SetEvent(gate) && Join(thread));
        CHECK(!failure && phase==2 && timerCalls==1 && userCalls==1 && sentCalls==1);
    }
    CloseHandle(entered);CloseHandle(gate);CloseHandle(token);TlsFree(tls);UnregisterClassA("MixedApcWnd",cls.hInstance);
    puts("PASSED: 12 mixed MsgWait/timer/APC rounds, nonalertable callback blocking, sent/posted preservation, token isolation and parked-source lifetime");
    return 0;
}
