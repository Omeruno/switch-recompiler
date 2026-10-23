/* Public x86 fixture: deliberately leave USER/GDI objects for session teardown. */
#include <windows.h>
#include <commdlg.h>
#include <stdio.h>
#include <string.h>
__declspec(dllexport) volatile DWORD SessionUiProbe[8];
#define CHECK(x) do{if(!(x)){printf("FAIL GUI session line %d: %s\n",__LINE__,#x);return __LINE__;}}while(0)
static LRESULT CALLBACK Cbt(int code,WPARAM wp,LPARAM lp) {
    ++SessionUiProbe[2];return CallNextHookEx(0,code,wp,lp);
}
static LRESULT CALLBACK Proc(HWND h,UINT msg,WPARAM wp,LPARAM lp) {
    if(msg==WM_PAINT) {
        PAINTSTRUCT ps;HDC dc=BeginPaint(h,&ps);
        HBRUSH b=CreateSolidBrush(RGB(19,83,151));FillRect(dc,&ps.rcPaint,b);DeleteObject(b);EndPaint(h,&ps);return 0;
    }
    return DefWindowProcA(h,msg,wp,lp);
}
int main(int argc,char** argv) {
    HINSTANCE instance=GetModuleHandleA(0);WNDCLASSA wc,old;HWND window;HMENU menu;
    HDC memory,screen;HBITMAP dib,mask;BITMAPINFO info;void* bits;ICONINFO icon;HICON image;
    ACCEL accel={FVIRTKEY,'Q',77};int index=COLOR_BTNFACE;COLORREF color=RGB(1,2,3);
    CHECK(!FindWindowA("SessionUiClass",0));
    CHECK(!GetClassInfoA(instance,"SessionUiClass",&old));
    CHECK(GetSysColor(COLOR_BTNFACE)==0xC8D0D4);CHECK(CommDlgExtendedError()==0);
    memset(&wc,0,sizeof(wc));wc.hInstance=instance;wc.lpfnWndProc=Proc;wc.lpszClassName="SessionUiClass";
    CHECK(RegisterClassA(&wc));
    menu=CreatePopupMenu();CHECK(menu);CHECK(AppendMenuA(menu,MF_STRING,77,"Session item"));
    CHECK(CreateAcceleratorTableA(&accel,1));
    window=CreateWindowA(wc.lpszClassName,"Session",WS_OVERLAPPEDWINDOW|WS_VISIBLE,
        30,30,240,180,0,0,instance,0);CHECK(window);
    /* An old hook at the reused guest address would have run during creation. */
    CHECK(SessionUiProbe[2]==0);CHECK(SetWindowsHookExA(WH_CBT,Cbt,0,GetCurrentThreadId()));
    CHECK(CreateWindowA("Static","child",WS_CHILD|WS_VISIBLE,5,5,40,20,window,0,instance,0));
    CHECK(SessionUiProbe[2]>0);CHECK(LoadIconA(0,IDI_APPLICATION));CHECK(LoadCursorA(0,IDC_ARROW));
    memory=CreateCompatibleDC(0);CHECK(memory);
    memset(&info,0,sizeof(info));info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=16;info.bmiHeader.biHeight=-16;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    dib=CreateDIBSection(memory,&info,DIB_RGB_COLORS,&bits,0,0);CHECK(dib && bits);
    CHECK(((DWORD*)bits)[0]==0);((DWORD*)bits)[0]=0x123456;SelectObject(memory,dib);
    CHECK(GetPixel(memory,0,0)==RGB(0x12,0x34,0x56));
    mask=CreateBitmap(16,16,1,1,0);CHECK(mask);memset(&icon,0,sizeof(icon));icon.fIcon=TRUE;icon.hbmMask=mask;icon.hbmColor=dib;
    image=CreateIconIndirect(&icon);CHECK(image);
    UpdateWindow(window);screen=GetDC(window);CHECK(screen);CHECK(BitBlt(screen,4,28,16,16,memory,0,0,SRCCOPY));
    CHECK(SetSysColors(1,&index,&color));SetCapture(window);SetFocus(window);CHECK(SetTimer(window,77,100,0));
    SessionUiProbe[1]=(DWORD)window;SessionUiProbe[3]=(DWORD)dib;SessionUiProbe[4]=(DWORD)bits;
    SessionUiProbe[0]=1;
    if(argc>1 && strcmp(argv[1],"single")!=0)while(!SessionUiProbe[7])Sleep(1);
    puts("PASSED: session USER/GDI resources");return 0;
}
