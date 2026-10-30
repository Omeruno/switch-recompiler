#include <windows.h>
#include <mmsystem.h>
/* Public authored game-like loop; no game code/assets or simulated physics. */
__declspec(dllexport) volatile DWORD LoopProbe[24];
enum { MODE,WINDOW,STAGE,XPOS,ZHELD,TIMERS,PAINTS,CHARS,FINAL_ACK,DONE,KEYDOWNS,KEYUPS,START_TICK,ELAPSED,DRAW_STAGE,DRAW_X,DRAW_Z,DRAW_TIMERS };
#define CHECK(x) do{if(!(x))ExitProcess(__LINE__);}while(0)
static BOOL started,right;
static DWORD lastFrame;
static void Paint(HWND hwnd) {
    PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd,&ps);CHECK(dc);
    RECT canvas={0,0,96,64};HBRUSH background=CreateSolidBrush(RGB(24,40,56));CHECK(background);
    CHECK(FillRect(dc,&canvas,background));CHECK(DeleteObject(background));
    COLORREF color=LoopProbe[ZHELD]?RGB(0,255,0):RGB(255,128,0);
    for(int y=16;y<19;++y)for(int x=(int)LoopProbe[XPOS];x<(int)LoopProbe[XPOS]+3;++x)CHECK(SetPixel(dc,x,y,color)!=CLR_INVALID);
    CHECK(SetPixel(dc,2,2,RGB(0,0,(LoopProbe[TIMERS]&255)))!=CLR_INVALID);
    CHECK(EndPaint(hwnd,&ps));
    LoopProbe[DRAW_X]=LoopProbe[XPOS];LoopProbe[DRAW_Z]=LoopProbe[ZHELD];LoopProbe[DRAW_TIMERS]=LoopProbe[TIMERS];
    LoopProbe[DRAW_STAGE]=LoopProbe[STAGE];++LoopProbe[PAINTS];
}
static LRESULT CALLBACK Proc(HWND hwnd,UINT msg,WPARAM w,LPARAM l) {
    if(msg==WM_PAINT){Paint(hwnd);return 0;}
    if(msg==WM_TIMER){CHECK(w==7);++LoopProbe[TIMERS];return 0;}
    if(msg==WM_CHAR){CHECK(w=='z' || w==' ');++LoopProbe[CHARS];return 0;}
    if(msg==WM_KEYDOWN || msg==WM_KEYUP) {
        const BOOL down=msg==WM_KEYDOWN;DWORD scan=((DWORD)l>>16)&0x1ff;
        CHECK(scan==(w==VK_F2?0x3c:w==VK_RIGHT?0x14d:w=='Z'?0x2c:0x39));
        CHECK(((DWORD)l>>31)==(down?0u:1u));
        CHECK(!!(GetKeyState((int)w)&0x8000)==!!down);
        if(down)++LoopProbe[KEYDOWNS];else ++LoopProbe[KEYUPS];
        if(w==VK_F2 && down){CHECK(LoopProbe[STAGE]==1);started=TRUE;LoopProbe[XPOS]=8;LoopProbe[STAGE]=2;}
        else if(w==VK_RIGHT){right=down;if(!down){CHECK(LoopProbe[STAGE]==3);LoopProbe[STAGE]=4;}}
        else if(w=='Z'){LoopProbe[ZHELD]=down;CHECK(LoopProbe[STAGE]==(down?4u:5u));LoopProbe[STAGE]=down?5:6;}
        else if(w==VK_SPACE && down){CHECK(LoopProbe[STAGE]==6);LoopProbe[XPOS]=8;LoopProbe[STAGE]=7;}
        return 0;
    }
    if(msg==WM_DESTROY){PostQuitMessage(0);return 0;}
    return DefWindowProcA(hwnd,msg,w,l);
}
void WINAPI start(void) {
    WNDCLASSA cls={0};MSG msg;cls.lpfnWndProc=Proc;cls.lpszClassName="PublicGameLoop";CHECK(RegisterClassA(&cls));
    HWND hwnd=CreateWindowA(cls.lpszClassName,"Public input/timer/frame loop",WS_OVERLAPPEDWINDOW,20,20,320,180,0,0,0,0);CHECK(hwnd);
    LoopProbe[WINDOW]=(DWORD)hwnd;LoopProbe[XPOS]=4;ShowWindow(hwnd,SW_SHOW);SetForegroundWindow(hwnd);SetFocus(hwnd);
    CHECK(!(GetAsyncKeyState(VK_RIGHT)&0x8000) && !(GetKeyState('Z')&0x8000));
    CHECK(SetTimer(hwnd,7,20,0)==7);LoopProbe[START_TICK]=timeGetTime();lastFrame=LoopProbe[START_TICK];LoopProbe[STAGE]=1;
    for(;;) {
        /* Busy PeekMessage game loop: no Sleep, GetMessage or fake timer tick. */
        if(PeekMessageA(&msg,0,0,0,PM_REMOVE)) {
            CHECK(msg.message!=WM_QUIT);TranslateMessage(&msg);DispatchMessageA(&msg);
        }
        const DWORD now=timeGetTime();LoopProbe[ELAPSED]=now-LoopProbe[START_TICK];
        CHECK(LoopProbe[ELAPSED]<10000);
        if(now-lastFrame>=16) {
            lastFrame=now;
            if(started && right && LoopProbe[XPOS]<64)++LoopProbe[XPOS];
            if(LoopProbe[STAGE]==2 && LoopProbe[XPOS]>=12)LoopProbe[STAGE]=3;
            CHECK(InvalidateRect(hwnd,0,FALSE) && UpdateWindow(hwnd));
        }
        if(LoopProbe[STAGE]==7 && LoopProbe[CHARS]==2 && LoopProbe[KEYUPS]==4 && LoopProbe[TIMERS]>=8) {
            LoopProbe[STAGE]=8;CHECK(InvalidateRect(hwnd,0,FALSE) && UpdateWindow(hwnd));
            if(LoopProbe[MODE])while(!LoopProbe[FINAL_ACK]){
                if(PeekMessageA(&msg,0,0,0,PM_REMOVE)){CHECK(msg.message!=WM_QUIT);TranslateMessage(&msg);DispatchMessageA(&msg);}
            }
            else {Sleep(32);PeekMessageA(&msg,0,0,0,PM_NOREMOVE);}
            CHECK(LoopProbe[KEYDOWNS]==4 && !right && !LoopProbe[ZHELD]);
            CHECK(!(GetAsyncKeyState(VK_RIGHT)&0x8000) && !(GetKeyState('Z')&0x8000));
            CHECK(KillTimer(hwnd,7) && DestroyWindow(hwnd));LoopProbe[DONE]=1;ExitProcess(0);
        }
    }
}
