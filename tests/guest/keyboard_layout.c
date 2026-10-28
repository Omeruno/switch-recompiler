/* Public CRT-free fixed-US keyboard API and message contract. */
#include <windows.h>
__declspec(dllexport) volatile DWORD KeyboardProbe[2];
#define CHECK(x) do {if(!(x))ExitProcess(__LINE__);}while(0)
static int equal(const char* a,const char* b){while(*a && *a==*b){++a;++b;}return *a==*b;}
static void names(LONG lp,const char* expected){
    char a[32];WCHAR w[32];unsigned n=0;
    SetLastError(0x76543210);
    CHECK(GetKeyNameTextA(lp,a,32)>0 && equal(a,expected));
    CHECK(GetLastError()==0x76543210);
    CHECK(GetKeyNameTextW(lp,w,32)>0);
    while(expected[n]){CHECK(w[n]==(unsigned char)expected[n]);++n;}CHECK(w[n]==0);
}
void WINAPI start(void){
    char a[16]={'x','y','z'};WCHAR w[3]={1,2,3};MSG msg;unsigned n=0;
    const DWORD keys[19]={'A','A','A',VK_CONTROL,VK_CONTROL,VK_LEFT,VK_LEFT,VK_NUMPAD4,VK_NUMPAD4,VK_SHIFT,VK_SHIFT,VK_MENU,VK_MENU,VK_RETURN,VK_RETURN,VK_SHIFT,VK_SHIFT,VK_SHIFT,VK_SHIFT};
    const DWORD scans[19]={0x1e,0x1e,0x1e,0x11d,0x11d,0x14b,0x14b,0x4b,0x4b,0x36,0x36,0x138,0x138,0x11c,0x11c,0x2a,0x36,0x2a,0x36};
    const char* labels[19]={"A","A","A","Right Ctrl","Right Ctrl","Left","Left","Num 4","Num 4","Right Shift","Right Shift","Right Alt","Right Alt","Num Enter","Num Enter","Shift","Right Shift","Shift","Right Shift"};
    WNDCLASSA cls={0};HWND hwnd;
    SetLastError(0x76543210);
    CHECK(MapVirtualKeyA('A',0)==0x1e && MapVirtualKeyW(0x1e,1)=='A');
    CHECK(MapVirtualKeyA(VK_RCONTROL,4)==0xe01d && MapVirtualKeyW(0xe01d,3)==VK_RCONTROL);
    CHECK(MapVirtualKeyA(0xe01d,1)==VK_CONTROL && MapVirtualKeyW(VK_NUMPAD4,0)==0x4b);
    CHECK(MapVirtualKeyA(0x4b,1)==VK_LEFT && MapVirtualKeyA(VK_OEM_PLUS,2)=='=');
    CHECK(MapVirtualKeyA(VK_F12,0)==0x58 && MapVirtualKeyW(VK_PAUSE,4)==0xe11d);
    CHECK(MapVirtualKeyA(0xffffffff,0)==0 && MapVirtualKeyW(0xffe01d,3)==0 && MapVirtualKeyA('A',99)==0);
    CHECK(GetLastError()==0x76543210);
    names(0x1e0000,"A");names(0xe11d0000,"Right Ctrl");names(0x031d0000,"Ctrl");
    names(0x00360000,"Right Shift");names(0x02360000,"Shift");names(0x01450000,"Num Lock");names(0x00450000,"Pause");
    CHECK(GetKeyNameTextA(0x011d0000,a,3)==2 && a[0]=='R' && a[1]=='i' && a[2]==0);
    CHECK(GetKeyNameTextW(0x011d0000,w,2)==1 && w[0]=='R' && !w[1] && w[2]==3);
    a[0]='x';a[1]='y';CHECK(!GetKeyNameTextA(0x1e0000,a,1) && !a[0] && a[1]=='y');
    a[0]='x';CHECK(!GetKeyNameTextA(0x1e0000,a,0) && a[0]=='x');
    CHECK(!GetKeyNameTextA(0x1e0000,a,-1) && a[0]=='x');
    CHECK(!GetKeyNameTextA(0x7f0000,a,16) && !a[0] && GetLastError()==0x76543210);
    CHECK(!GetKeyNameTextA(0x1e0000,NULL,2) && GetLastError()==ERROR_INVALID_PARAMETER);
    CHECK(!GetKeyNameTextW(0x1e0000,(LPWSTR)(ULONG_PTR)0xffffffff,2) && GetLastError()==ERROR_NOACCESS);
    cls.lpfnWndProc=DefWindowProcA;cls.lpszClassName="PublicKeyboard";CHECK(RegisterClassA(&cls));
    hwnd=CreateWindowA(cls.lpszClassName,"Public keyboard",WS_OVERLAPPEDWINDOW,20,20,300,200,NULL,NULL,NULL,NULL);CHECK(hwnd);
    ShowWindow(hwnd,SW_SHOW);SetFocus(hwnd);KeyboardProbe[0]=1;
    while(n<19){
        CHECK(PeekMessageA(&msg,hwnd,WM_KEYFIRST,WM_KEYLAST,PM_NOREMOVE));
        CHECK(!(GetKeyState(VK_RCONTROL)&0x8000) || n==4);
        CHECK(PeekMessageA(&msg,hwnd,WM_KEYFIRST,WM_KEYLAST,PM_REMOVE));
        CHECK(msg.wParam==keys[n] && (((DWORD)msg.lParam>>16)&0x1ff)==scans[n]);
        names(msg.lParam,labels[n]);
        if(n==11 || n==12)CHECK(msg.message==(n==11?WM_SYSKEYDOWN:WM_SYSKEYUP));
        CHECK(!(GetAsyncKeyState(VK_RCONTROL)&0x8000));
        CHECK(((DWORD)msg.lParam&0x80000000u)==((n==2 || n==4 || n==6 || n==8 || n==10 || n==12 || n==14 || n==17 || n==18)?0x80000000u:0));
        if(n==1)CHECK((DWORD)msg.lParam&0x40000000u);
        if(n==3)CHECK(GetKeyState(VK_RCONTROL)&0x8000);
        if(n==4)CHECK(!(GetKeyState(VK_RCONTROL)&0x8000));
        if(n==17){BYTE state[256];CHECK(GetKeyboardState(state));CHECK(state[VK_SHIFT]&0x80);CHECK(!(state[VK_LSHIFT]&0x80));CHECK(state[VK_RSHIFT]&0x80);}
        if(n==18)CHECK(!(GetKeyState(VK_SHIFT)&0x8000));
        ++n;
    }
    KeyboardProbe[1]=1;ExitProcess(0);
}
