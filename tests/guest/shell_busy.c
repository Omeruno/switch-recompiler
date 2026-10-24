/* Public fixture: deliberately stop pumping USER, then resume normal dispatch. */
#include <windows.h>
__declspec(dllexport) volatile DWORD BusyStage,BusyResume,BusyFinish,BusyCommands,BusyMoves,BusyClicks,BusyKeys;
__declspec(dllexport) volatile DWORD BusyOwner,BusyWrongThread,BusyMode,BusyHeartbeat,BusyAsync,BusyCursorX,BusyCursorY,BusyWorker;
static LRESULT CALLBACK proc(HWND w,UINT m,WPARAM wp,LPARAM lp) {
    if(m==WM_SYSCOMMAND) {
        if(GetCurrentThreadId()!=BusyOwner)++BusyWrongThread;
        ++BusyCommands;
    }
    if(m==WM_MOUSEMOVE)++BusyMoves;
    if(m==WM_LBUTTONDOWN || m==WM_LBUTTONUP)++BusyClicks;
    if(m==WM_KEYDOWN || m==WM_KEYUP)++BusyKeys;
    if(m==WM_PAINT) {
        PAINTSTRUCT ps;HDC dc=BeginPaint(w,&ps);RECT r;GetClientRect(w,&r);
        HBRUSH b=CreateSolidBrush(RGB(30,140,70));FillRect(dc,&r,b);DeleteObject(b);EndPaint(w,&ps);return 0;
    }
    return DefWindowProcA(w,m,wp,lp);
}
static DWORD WINAPI runWindow(void* unused) {
    (void)unused;MSG msg;BusyOwner=GetCurrentThreadId();
    HINSTANCE instance=GetModuleHandleA(NULL);
    HWND w=CreateWindowA("BusyShellProbe","Busy public application",WS_OVERLAPPEDWINDOW|WS_VISIBLE,250,150,650,430,NULL,NULL,instance,NULL);
    if(!w)return 2;UpdateWindow(w);BusyStage=1;
    while(!BusyResume) {
        ++BusyHeartbeat;
        if(BusyMode) {
            POINT pt;BusyAsync|=(GetAsyncKeyState('K')&0x8000)?1:2;
            GetCursorPos(&pt);BusyCursorX=pt.x;BusyCursorY=pt.y;
        }
    } /* NO host calls: actual x86 spin. */
    BusyStage=2;
    while(!BusyFinish) {
        while(PeekMessageA(&msg,NULL,0,0,PM_REMOVE)) {TranslateMessage(&msg);DispatchMessageA(&msg);}
        Sleep(1);
    }
    DestroyWindow(w);BusyStage=3;return BusyWrongThread?3:0;
}

int main(void) {
    WNDCLASSA cls={0};cls.lpfnWndProc=proc;cls.lpszClassName="BusyShellProbe";cls.hInstance=GetModuleHandleA(NULL);
    if(!RegisterClassA(&cls))return 1;
    if(!BusyWorker)return (int)runWindow(NULL);
    HANDLE worker=CreateThread(NULL,0,runWindow,NULL,0,NULL);if(!worker)return 4;
    if(WaitForSingleObject(worker,INFINITE)!=WAIT_OBJECT_0)return 5;
    DWORD result=0;if(!GetExitCodeThread(worker,&result))return 6;CloseHandle(worker);return (int)result;
}
