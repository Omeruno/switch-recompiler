/* Unattended window/screen-DC acceptance. Also runs as an ordinary Switch EXE. */
#include <windows.h>
#include <stdio.h>

__declspec(dllexport) volatile DWORD CompositionStage;
static HWND background,lower,upper,child,popup,sibling;
static HDC dibDc;
static HBITMAP dib,oldBitmap;
static DWORD* dibBits;
static FILE* report;
static int failed;
static const COLORREF dark=RGB(24,32,48),red=RGB(210,30,40),blue=RGB(30,60,210),green=RGB(30,190,60);
#define CHECK(x) do {if(!(x)){fprintf(report,"FAIL line %d: %s (error %lu)\n",__LINE__,#x,GetLastError());fflush(report);failed=1;goto cleanup;}}while(0)
static LRESULT CALLBACK paint(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    if(msg==WM_CREATE) {SetWindowLongA(hwnd,GWL_USERDATA,(LONG)((CREATESTRUCTA*)lp)->lpCreateParams);return 0;}
    if(msg==WM_ERASEBKGND)return 1;
    if(msg==WM_PAINT) {
        PAINTSTRUCT ps;RECT r;HDC dc=BeginPaint(hwnd,&ps);
        HBRUSH brush=CreateSolidBrush((COLORREF)GetWindowLongA(hwnd,GWL_USERDATA));
        GetClientRect(hwnd,&r);FillRect(dc,&r,brush);DeleteObject(brush);
        if(hwnd==lower && dibDc)BitBlt(dc,20,20,32,24,dibDc,0,0,SRCCOPY);
        EndPaint(hwnd,&ps);return 0;
    }
    return DefWindowProcA(hwnd,msg,wp,lp);
}
static HWND make(HWND parent,DWORD style,int x,int y,int w,int h,COLORREF color) {
    return CreateWindowExA(WS_EX_NOACTIVATE,"CompositionProbe","Composition acceptance",style|WS_VISIBLE|WS_CLIPSIBLINGS|WS_CLIPCHILDREN,
        x,y,w,h,parent,NULL,GetModuleHandleA(NULL),(void*)(ULONG_PTR)color);
}
static int sample(int x,int y,COLORREF expected) {
    HDC dc=GetDC(NULL);COLORREF actual=GetPixel(dc,x,y);ReleaseDC(NULL,dc);
    if(actual==expected)return 1;
    fprintf(report,"FAIL stage %lu pixel (%d,%d): %06lx != %06lx\n",CompositionStage,x,y,actual,expected);fflush(report);return 0;
}
static void settle(void) {
    HWND windows[]={background,lower,upper,child,popup};unsigned i;MSG msg;
    /* Update only already-invalid regions. Never invalidate everything to hide exposure bugs. */
    for(i=0;i<sizeof(windows)/sizeof(windows[0]);++i)if(IsWindow(windows[i]))UpdateWindow(windows[i]);
    for(i=0;i<100 && PeekMessageA(&msg,NULL,0,0,PM_REMOVE);++i)DispatchMessageA(&msg);
}
static void checkpoint(DWORD stage,const char* name) {
    settle();CompositionStage=stage;
    fprintf(report,"BEGIN %lu: %s\n",stage,name);fflush(report);
    /* A scheduler boundary lets host inspectors validate every stable scene. */
    Sleep(32);
}
int main(void) {
    WNDCLASSA cls={0};int width=GetSystemMetrics(SM_CXSCREEN),height=GetSystemMetrics(SM_CYSCREEN);
    BITMAPINFO info={0};HDC screen=NULL,captureDc=NULL;HBITMAP capture=NULL,captureOld=NULL;int i;
    report=fopen("stage-e-composition.txt","w");if(!report)return 2;
    fprintf(report,"Stage E composition acceptance v1\nRUNNING: incomplete report is not a pass\nScreen: %dx%d\n",width,height);fflush(report);
    CHECK(width>=640 && height>=480);
    cls.lpfnWndProc=paint;cls.hInstance=GetModuleHandleA(NULL);cls.lpszClassName="CompositionProbe";
    CHECK(RegisterClassA(&cls));
    background=make(NULL,WS_POPUP,0,0,width,height,dark);CHECK(background);
    checkpoint(1,"desktop-sized background");CHECK(sample(width-1,height-1,dark));
    lower=make(NULL,WS_POPUP,40,50,300,220,red);CHECK(lower);
    checkpoint(2,"lower window");CHECK(sample(50,60,red));
    upper=make(NULL,WS_POPUP,160,100,280,220,green);CHECK(upper);
    checkpoint(3,"overlap");CHECK(sample(170,110,green));
    SetWindowLongA(lower,GWL_USERDATA,blue);CHECK(InvalidateRect(lower,NULL,FALSE));
    checkpoint(4,"lower repaint clipped by upper");CHECK(sample(50,60,blue)&&sample(170,110,green));
    CHECK(SetWindowPos(upper,NULL,120,140,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE));
    checkpoint(5,"move exposes old footprint");CHECK(sample(170,110,blue)&&sample(130,150,green));
    CHECK(SetWindowPos(upper,NULL,0,0,100,70,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE));
    checkpoint(6,"shrink exposes old footprint");CHECK(sample(250,200,blue)&&sample(390,350,dark));
    ShowWindow(upper,SW_HIDE);checkpoint(7,"hide exposes lower");CHECK(sample(130,150,blue));
    ShowWindow(upper,SW_SHOWNOACTIVATE);checkpoint(8,"show restores upper");CHECK(sample(130,150,green));
    CHECK(SetWindowPos(lower,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE));
    checkpoint(9,"raise lower");CHECK(sample(130,150,blue));
    CHECK(SetWindowPos(upper,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE));
    checkpoint(10,"raise upper");CHECK(sample(130,150,green));
    child=make(lower,WS_CHILD,250,180,150,100,RGB(250,220,20));CHECK(child);
    checkpoint(11,"child clipped by parent right and bottom");CHECK(sample(300,240,RGB(250,220,20))&&sample(350,240,dark));
    CHECK(SetWindowPos(child,NULL,-20,-10,150,100,SWP_NOZORDER|SWP_NOACTIVATE));
    checkpoint(12,"child clipped by parent left and top");CHECK(sample(45,55,RGB(250,220,20))&&sample(25,45,dark));
    CHECK(DestroyWindow(child));child=NULL;
    checkpoint(13,"child destruction exposes parent");CHECK(sample(45,55,blue));
    popup=make(lower,WS_POPUP,60,70,80,60,RGB(245,245,245));CHECK(popup);
    checkpoint(14,"owned popup above owner");CHECK(sample(70,80,RGB(245,245,245)));
    CHECK(DestroyWindow(popup));popup=NULL;
    checkpoint(15,"popup destruction exposes owner");CHECK(sample(70,80,blue));
    screen=GetDC(NULL);CHECK(screen);CHECK(PatBlt(screen,30,40,200,150,DSTINVERT));
    ReleaseDC(NULL,screen);screen=NULL;
    checkpoint(16,"destination ROP on shared screen");CHECK(sample(70,80,blue^0xFFFFFF));
    screen=GetDC(NULL);CHECK(screen);captureDc=CreateCompatibleDC(screen);CHECK(captureDc);
    capture=CreateCompatibleBitmap(screen,256,224);CHECK(capture);captureOld=SelectObject(captureDc,capture);CHECK(captureOld);
    CHECK(BitBlt(captureDc,0,0,256,224,screen,0,0,SRCCOPY));
    CHECK(GetPixel(captureDc,70,80)==(blue^0xFFFFFF));CHECK(GetPixel(captureDc,130,150)==(green^0xFFFFFF));
    CHECK(PatBlt(screen,30,40,200,150,DSTINVERT));ReleaseDC(NULL,screen);screen=NULL;
    checkpoint(17,"screen readback and second XOR restore");CHECK(sample(70,80,blue));
    info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=32;info.bmiHeader.biHeight=-24;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    dib=CreateDIBSection(NULL,&info,DIB_RGB_COLORS,(void**)&dibBits,NULL,0);CHECK(dib&&dibBits);
    dibDc=CreateCompatibleDC(NULL);CHECK(dibDc);oldBitmap=SelectObject(dibDc,dib);CHECK(oldBitmap);
    for(i=0;i<32*24;++i)dibBits[i]=(i&1)?0x00FF00FF:0x0000FFFF;
    CHECK(InvalidateRect(lower,NULL,FALSE));
    checkpoint(18,"guest-written DIB to window");CHECK(sample(60,70,RGB(0,255,255))&&sample(61,70,RGB(255,0,255)));
    dibBits[0]=0x00FFFF00;screen=GetDC(lower);CHECK(screen);CHECK(BitBlt(screen,20,20,32,24,dibDc,0,0,SRCCOPY));
    ReleaseDC(lower,screen);screen=NULL;
    checkpoint(19,"raw DIB mutation invalidates output");CHECK(sample(60,70,RGB(255,255,0)));
    CHECK(DestroyWindow(lower));lower=NULL;
    checkpoint(20,"destroy lower keeps upper");CHECK(sample(60,70,dark)&&sample(130,150,green));
    CHECK(DestroyWindow(upper));upper=NULL;
    checkpoint(21,"destroy upper restores background");CHECK(sample(130,150,dark));
    SelectObject(dibDc,oldBitmap);DeleteDC(dibDc);dibDc=NULL;DeleteObject(dib);dib=NULL;
    lower=make(NULL,WS_POPUP,40,50,300,220,blue);CHECK(lower);
    upper=make(NULL,WS_POPUP,120,140,100,70,green);CHECK(upper);
    checkpoint(22,"activation pair initial overlap");CHECK(sample(130,150,green));
    CHECK(SetForegroundWindow(lower));
    checkpoint(23,"activation raises and repaints lower");CHECK(sample(130,150,blue));
    CHECK(SetForegroundWindow(upper));
    checkpoint(24,"activation raises and repaints upper");CHECK(sample(130,150,green));
    CHECK(SetForegroundWindow(lower));
    child=make(lower,WS_CHILD,10,10,100,100,RGB(250,220,20));CHECK(child);
    sibling=make(lower,WS_CHILD,60,60,100,100,RGB(180,40,190));CHECK(sibling);
    CHECK(SetWindowPos(sibling,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE));
    checkpoint(25,"overlapping child siblings");CHECK(sample(110,120,RGB(180,40,190)));
    CHECK(SetWindowPos(sibling,NULL,150,150,100,100,SWP_NOZORDER|SWP_NOACTIVATE));
    checkpoint(26,"moving child exposes sibling under clipped parent");CHECK(sample(110,120,RGB(250,220,20)));
    ShowWindow(sibling,SW_HIDE);
    checkpoint(27,"hiding child exposes parent");CHECK(sample(200,220,blue));
    CHECK(DestroyWindow(child));child=NULL;
    checkpoint(28,"destroying child restores parent");CHECK(sample(60,70,blue));
cleanup:
    if(screen)ReleaseDC(NULL,screen);
    if(captureDc){if(captureOld)SelectObject(captureDc,captureOld);DeleteDC(captureDc);}
    if(capture)DeleteObject(capture);
    if(IsWindow(popup))DestroyWindow(popup);
    if(IsWindow(child))DestroyWindow(child);
    if(IsWindow(sibling))DestroyWindow(sibling);
    if(IsWindow(lower))DestroyWindow(lower);
    if(IsWindow(upper))DestroyWindow(upper);
    if(IsWindow(background))DestroyWindow(background);
    if(dibDc){if(oldBitmap)SelectObject(dibDc,oldBitmap);DeleteDC(dibDc);}if(dib)DeleteObject(dib);
    fprintf(report,failed?"FAIL: composition acceptance\n":"PASS: ALL 28 SCENES\n");fclose(report);
    if(!failed)puts("PASSED: composition acceptance, ALL 28 SCENES; report stage-e-composition.txt");
    return failed;
}
