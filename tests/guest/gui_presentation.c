/* Full-desktop visual/Win32 probe; automatic, no button presses required. */
#include <windows.h>
#include <stdio.h>
static int width,height,painted;
static void fill(HDC dc,int x,int y,int w,int h,COLORREF color) {
    RECT r={x,y,x+w,y+h};HBRUSH b=CreateSolidBrush(color);FillRect(dc,&r,b);DeleteObject(b);
}
static LRESULT CALLBACK proc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    if(message==WM_PAINT) {
        PAINTSTRUCT paint;HDC dc=BeginPaint(window,&paint);char text[160];
        fill(dc,0,0,width,height,RGB(24,32,48));
        fill(dc,0,0,width,8,RGB(255,200,0));fill(dc,0,height-8,width,8,RGB(255,200,0));
        fill(dc,0,0,8,height,RGB(255,200,0));fill(dc,width-8,0,8,height,RGB(255,200,0));
        fill(dc,16,16,32,32,RGB(255,0,0));fill(dc,width-48,16,32,32,RGB(0,255,0));
        fill(dc,16,height-48,32,32,RGB(0,0,255));fill(dc,width-48,height-48,32,32,RGB(255,255,255));
        SetTextColor(dc,RGB(255,255,255));SetBkMode(dc,TRANSPARENT);
        wsprintfA(text,"Software presentation - desktop %d x %d",width,height);
        TextOutA(dc,64,80,text,lstrlenA(text));
        TextOutA(dc,64,112,"All four coloured corners and the yellow border must be visible.",sizeof("All four coloured corners and the yellow border must be visible.")-1);
        TextOutA(dc,64,144,"Switch Lite target: 1280 x 720. Automatic exit; no clicks required.",sizeof("Switch Lite target: 1280 x 720. Automatic exit; no clicks required.")-1);
        TextOutA(dc,64,176,"CPU GDI reference; this is not GPU acceleration.",sizeof("CPU GDI reference; this is not GPU acceleration.")-1);
        ++painted;EndPaint(window,&paint);return 0;
    }
    return DefWindowProcA(window,message,wp,lp);
}
#define CHECK(x) do {if(!(x)){fprintf(log,"FAIL line %d: %s error %lu\n",__LINE__,#x,GetLastError());fclose(log);return 1;}}while(0)
int main(void) {
    FILE* log=fopen("stage-e-presentation.txt","w");WNDCLASSA cls={0};HWND window;MSG msg;RECT r;HDC dc;DWORD until;
    if(!log)return 2;
    width=GetSystemMetrics(SM_CXSCREEN);height=GetSystemMetrics(SM_CYSCREEN);
    fprintf(log,"Stage E software presentation v1\nVirtual screen: %d x %d\nRUNNING\n",width,height);fflush(log);
    CHECK(width>=800 && height>=600);
    CHECK(GetClientRect(GetDesktopWindow(),&r) && r.right==width && r.bottom==height);
    CHECK(SystemParametersInfoA(SPI_GETWORKAREA,0,&r,0) && r.right==width && r.bottom==height);
    cls.lpfnWndProc=proc;cls.hInstance=GetModuleHandleA(NULL);cls.lpszClassName="PresentationProbe";
    CHECK(RegisterClassA(&cls));
    window=CreateWindowA(cls.lpszClassName,"Software presentation",WS_POPUP|WS_VISIBLE,0,0,width,height,NULL,NULL,cls.hInstance,NULL);
    CHECK(window && UpdateWindow(window));
    CHECK(GetClientRect(window,&r) && r.right==width && r.bottom==height && painted>0);
    dc=GetDC(window);CHECK(dc);
    CHECK(GetPixel(dc,20,20)==RGB(255,0,0));CHECK(GetPixel(dc,width-20,20)==RGB(0,255,0));
    CHECK(GetPixel(dc,20,height-20)==RGB(0,0,255));CHECK(GetPixel(dc,width-20,height-20)==RGB(255,255,255));
    CHECK(ReleaseDC(window,dc));
    until=GetTickCount()+4500;
    while((LONG)(until-GetTickCount())>0) {
        while(PeekMessageA(&msg,NULL,0,0,PM_REMOVE))DispatchMessageA(&msg);
        Sleep(16);
    }
    CHECK(DestroyWindow(window));
    fprintf(log,"PASS: desktop geometry and four CPU GDI corner pixels\nVirtual screen: %d x %d\n",width,height);fclose(log);
    puts("PASSED: software presentation desktop geometry and four GDI corners");return 0;
}
