/* Public CRT-free PE32: physical input is supplied by the native inspector. */
#include <windows.h>
__declspec(dllexport) volatile DWORD GestureProbe[16];
static HWND peer;
#define CHECK(x) do{if(!(x))ExitProcess(__LINE__);}while(0)
static LRESULT CALLBACK proc(HWND w,UINT m,WPARAM a,LPARAM b) {
    if(m==WM_SYSCOMMAND && (a&0xfff0)==SC_SIZE && GestureProbe[0]==21)return 0;
    if(m==WM_GETMINMAXINFO){MINMAXINFO* p=(MINMAXINFO*)b;p->ptMinTrackSize.x=200;p->ptMinTrackSize.y=120;p->ptMaxTrackSize.x=500;p->ptMaxTrackSize.y=400;return 0;}
    if(m==WM_ENTERSIZEMOVE)++GestureProbe[2];
    if(m==WM_EXITSIZEMOVE)++GestureProbe[3];
    if(m==WM_MOVING)++GestureProbe[4];
    if(m==WM_SIZING){
        ++GestureProbe[5];
        if(GestureProbe[0]==6)((RECT*)b)->right=((RECT*)b)->left+360;
        if(GestureProbe[0]==7)SendMessageA(w,WM_CANCELMODE,0,0);
        if(GestureProbe[0]==8)SetCapture(peer);
        if(GestureProbe[0]==22)DestroyWindow(w);
        return TRUE;
    }
    if(m==WM_CLOSE){++GestureProbe[6];return 0;} /* real application veto */
    if(m==WM_SIZE)++GestureProbe[9];
    return DefWindowProcA(w,m,a,b);
}
static void phase(DWORD n){MSG msg;GestureProbe[0]=n;do{while(PeekMessageA(&msg,NULL,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageA(&msg);}Sleep(1);}while(GestureProbe[7]<n);while(PeekMessageA(&msg,NULL,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageA(&msg);}}
static void rect(HWND w,int x,int y,int width,int height){RECT r;CHECK(GetWindowRect(w,&r));CHECK(r.left==x&&r.top==y&&r.right-r.left==width&&r.bottom-r.top==height);}
void WINAPI start(void){
    WNDCLASSA cls={0};HWND w;RECT normal;cls.lpfnWndProc=proc;cls.hInstance=GetModuleHandleA(NULL);cls.lpszClassName="TrueWindowGestures";CHECK(RegisterClassA(&cls));
    w=CreateWindowA(cls.lpszClassName,"Public window",WS_OVERLAPPEDWINDOW|WS_VISIBLE,60,70,320,240,NULL,NULL,cls.hInstance,NULL);CHECK(w);
    peer=CreateWindowA(cls.lpszClassName,"peer",WS_POPUP,900,100,40,40,NULL,NULL,cls.hInstance,NULL);CHECK(peer);GestureProbe[1]=(DWORD)(ULONG_PTR)w;
    phase(1);rect(w,60,70,390,290);CHECK(GestureProbe[2]==1&&GestureProbe[3]==1&&GestureProbe[5]>=1);CHECK(!GetCapture());
    phase(2);rect(w,100,100,390,290);CHECK(GestureProbe[4]>=1);
    phase(3);rect(w,100,100,200,120);
    phase(4);rect(w,100,100,500,400);
    phase(5);rect(w,100,100,500,400);CHECK(!GetCapture());
    phase(6);rect(w,100,100,360,400);
    phase(7);rect(w,100,100,360,400);CHECK(!GetCapture());
    phase(8);rect(w,100,100,360,400);CHECK(GetCapture()==peer);ReleaseCapture();
    phase(9);CHECK(IsWindow(w)&&GestureProbe[6]==0);
    phase(10);CHECK(IsWindow(w)&&GestureProbe[6]==1);
    CHECK(GetWindowRect(w,&normal));phase(11);CHECK(IsZoomed(w));{RECT r;CHECK(GetWindowRect(w,&r)&&r.left<0&&r.bottom<=684);}
    SetFocus(w);phase(12);CHECK(IsIconic(w)&&!GetFocus()&&!GetCapture());
    ShowWindow(w,SW_RESTORE);CHECK(IsZoomed(w));SetCapture(w);ShowWindow(w,SW_MINIMIZE);CHECK(!GetCapture());ShowWindow(w,SW_RESTORE);CHECK(IsZoomed(w));ShowWindow(w,SW_RESTORE);rect(w,normal.left,normal.top,normal.right-normal.left,normal.bottom-normal.top);
    phase(13);CHECK(IsZoomed(w));ShowWindow(w,SW_RESTORE);rect(w,normal.left,normal.top,normal.right-normal.left,normal.bottom-normal.top);
    {int edge;for(edge=0;edge<7;++edge){
        CHECK(SetWindowPos(w,NULL,100,100,400,300,SWP_NOZORDER|SWP_NOACTIVATE));phase(14+(DWORD)edge);
        switch(edge){case 0:rect(w,130,100,370,300);break;case 1:rect(w,100,100,430,300);break;
        case 2:rect(w,100,120,400,280);break;case 3:rect(w,130,120,370,280);break;
        case 4:rect(w,100,120,430,280);break;case 5:rect(w,100,100,400,320);break;case 6:rect(w,130,100,370,320);break;}
    }}
    {DWORD before=GestureProbe[2];phase(21);rect(w,130,100,370,320);CHECK(GestureProbe[2]==before);}
    CHECK(GestureProbe[2]==GestureProbe[3]);phase(22);CHECK(!IsWindow(w)&&!GetCapture());
    CHECK(GestureProbe[2]==GestureProbe[3]+1);CHECK(GestureProbe[9]>0);DestroyWindow(peer);GestureProbe[0]=99;ExitProcess(0);
}
