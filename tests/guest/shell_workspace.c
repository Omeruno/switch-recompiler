/* Public one-Process shell/window integration fixture; no private Paint files. */
#include <windows.h>
__declspec(dllexport) volatile DWORD WorkspaceStage,WorkspaceFinish,WorkspaceClicks,WorkspaceKeys;
static LRESULT CALLBACK proc(HWND w,UINT m,WPARAM wp,LPARAM lp) {
    if(m==WM_PAINT) {
        PAINTSTRUCT ps;RECT r;HBRUSH b=CreateSolidBrush(RGB(210,40,30));
        HDC dc=BeginPaint(w,&ps);GetClientRect(w,&r);FillRect(dc,&r,b);DeleteObject(b);EndPaint(w,&ps);return 0;
    }
    if(m==WM_LBUTTONDOWN){++WorkspaceClicks;return 0;}
    if(m==WM_KEYDOWN){++WorkspaceKeys;return 0;}
    return DefWindowProcA(w,m,wp,lp);
}
int main(void) {
    RECT work;MONITORINFO info={sizeof(info)};WNDCLASSA cls={0};MSG msg;
    if(!SystemParametersInfoA(SPI_GETWORKAREA,0,&work,0) || work.bottom!=680)return 1;
    if(!GetMonitorInfoA(MonitorFromWindow(NULL,MONITOR_DEFAULTTOPRIMARY),&info) || info.rcWork.bottom!=680 || info.rcMonitor.bottom!=720)return 2;
    cls.lpfnWndProc=proc;cls.lpszClassName="WorkspaceProbe";cls.hInstance=GetModuleHandleA(NULL);
    if(!RegisterClassA(&cls))return 3;
    HWND main=CreateWindowA(cls.lpszClassName,"Public workspace application",WS_OVERLAPPEDWINDOW|WS_VISIBLE,260,160,600,400,NULL,NULL,cls.hInstance,NULL);
    HWND popup=CreateWindowA(cls.lpszClassName,"Owned window",WS_OVERLAPPEDWINDOW|WS_VISIBLE,900,200,280,220,main,NULL,cls.hInstance,NULL);
    if(!main || !popup)return 4;
    UpdateWindow(main);UpdateWindow(popup);WorkspaceStage=1;
    while(!WorkspaceFinish) {
        while(PeekMessageA(&msg,NULL,0,0,PM_REMOVE)) {TranslateMessage(&msg);DispatchMessageA(&msg);}
        Sleep(1);
    }
    DestroyWindow(popup);DestroyWindow(main);WorkspaceStage=2;return 0;
}
