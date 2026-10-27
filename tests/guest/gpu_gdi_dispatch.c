/* Public CRT-free ordinary Win32 GDI workload. No native GPU injection. */
#include <windows.h>
__declspec(dllexport) volatile DWORD DispatchProbe[4];
#define CHECK(x) do {if(!(x))ExitProcess(__LINE__);}while(0)
static COLORREF color(DWORD c){return RGB((c>>16)&255,(c>>8)&255,c&255);}
static DWORD tile(unsigned i){return (0x203040u+i*0x12345u)&0xffffff;}
static void stage(DWORD n,HWND w,HDC dc){DispatchProbe[1]=(DWORD)(ULONG_PTR)w;DispatchProbe[2]=(DWORD)(ULONG_PTR)dc;DispatchProbe[0]=n;Sleep(32);}
static LRESULT CALLBACK proc(HWND w,UINT m,WPARAM a,LPARAM b){return DefWindowProcA(w,m,a,b);}
static void fill(HDC dc,RECT r,DWORD rgb){HBRUSH b=CreateSolidBrush(color(rgb));CHECK(b);CHECK(FillRect(dc,&r,b));CHECK(DeleteObject(b));}
void WINAPI start(void) {
    WNDCLASSA cls={0};HWND w,child,large,budget[4],pixels;HDC dc,mem,other;HBITMAP dib,old;BITMAPINFO info={0};DWORD* bits;unsigned i;
    cls.lpfnWndProc=proc;cls.hInstance=GetModuleHandleA(NULL);cls.lpszClassName="OrdinaryGpuGdi";CHECK(RegisterClassA(&cls));
    w=CreateWindowExA(WS_EX_NOACTIVATE,cls.lpszClassName,"public dispatch",WS_POPUP|WS_VISIBLE,40,50,128,128,NULL,NULL,cls.hInstance,NULL);CHECK(w);dc=GetDC(w);CHECK(dc);
    fill(dc,(RECT){0,0,128,128},0x112233);
    mem=CreateCompatibleDC(dc);CHECK(mem);info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=16;info.bmiHeader.biHeight=-16;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    dib=CreateDIBSection(mem,&info,DIB_RGB_COLORS,(void**)&bits,NULL,0);CHECK(dib&&bits);old=SelectObject(mem,dib);CHECK(old);
    stage(1,w,dc);
    for(i=0;i<64;++i){RECT r={(i%8)*16,(i/8)*16,(i%8+1)*16,(i/8+1)*16};
        if(i&1){HBRUSH b=CreateSolidBrush(color(tile(i)));HGDIOBJ prev=SelectObject(dc,b);CHECK(b&&prev);CHECK(PatBlt(dc,r.left,r.top,16,16,PATCOPY));CHECK(SelectObject(dc,prev));CHECK(DeleteObject(b));}
        else fill(dc,r,tile(i));}
    stage(2,w,dc);
    for(i=0;i<64;++i)CHECK(GetPixel(dc,(i%8)*16+8,(i/8)*16+8)==color(tile(i)));
    CHECK(PatBlt(dc,5,5,4,4,DSTINVERT));CHECK(GetPixel(dc,6,6)==color(tile(0)^0xffffff));
    CHECK(BitBlt(mem,0,0,16,16,dc,0,0,SRCCOPY));CHECK((bits[6*16+6]&0xffffff)==(tile(0)^0xffffff));
    for(i=0;i<256;++i)bits[i]=0x123abc;
    CHECK(BitBlt(dc,30,30,4,4,mem,0,0,SRCCOPY));CHECK(GetPixel(dc,31,31)==color(0x123abc));
    {DWORD output[256];CHECK(SelectObject(mem,old));CHECK(GetDIBits(dc,dib,0,16,output,&info,DIB_RGB_COLORS)==16);CHECK((output[0]&0xffffff)==0x123abc);CHECK(SelectObject(mem,dib));}
    stage(3,w,dc);
    {int saved=SaveDC(dc);HRGN clip=CreateRectRgn(10,10,25,25);CHECK(saved&&clip);CHECK(SetWindowOrgEx(dc,2,3,NULL));CHECK(SetViewportOrgEx(dc,5,7,NULL));CHECK(SelectClipRgn(dc,clip));
     fill(dc,(RECT){5,5,30,30},0x785634);CHECK(GetPixel(dc,12,12)==color(0x785634));CHECK(RestoreDC(dc,saved));CHECK(DeleteObject(clip));}
    {HBRUSH b=CreateHatchBrush(0,color(0xa1b2c3));RECT r={50,50,65,65};CHECK(b&&FillRect(dc,&r,b));CHECK(DeleteObject(b));}
    /* Child and oversized window deliberately keep CPU raster authority. */
    child=CreateWindowExA(0,cls.lpszClassName,"child",WS_CHILD|WS_VISIBLE,70,70,20,20,w,NULL,cls.hInstance,NULL);CHECK(child);other=GetDC(child);CHECK(other);fill(other,(RECT){0,0,20,20},0x314159);CHECK(GetPixel(other,1,1)==color(0x314159));CHECK(ReleaseDC(child,other));
    large=CreateWindowExA(WS_EX_NOACTIVATE,cls.lpszClassName,"large",WS_POPUP|WS_VISIBLE,300,50,600,20,NULL,NULL,cls.hInstance,NULL);CHECK(large);other=GetDC(large);CHECK(other);fill(other,(RECT){0,0,600,20},0xabcdef);CHECK(GetPixel(other,599,19)==color(0xabcdef));CHECK(ReleaseDC(large,other));
    fill(mem,(RECT){0,0,16,16},0x271828);CHECK((bits[0]&0xffffff)==0x271828);
    {HDC screen=GetDC(NULL);CHECK(screen);fill(screen,(RECT){1000,100,1008,108},0x102938);CHECK(GetPixel(screen,1001,101)==color(0x102938));CHECK(ReleaseDC(NULL,screen));}
    /* Four-image and combined pixel budgets are separate refusal gates. */
    pixels=CreateWindowExA(WS_EX_NOACTIVATE,cls.lpszClassName,"pixel budget",WS_POPUP|WS_VISIBLE,700,190,512,512,NULL,NULL,cls.hInstance,NULL);CHECK(pixels);
    other=GetDC(pixels);CHECK(other);fill(other,(RECT){0,0,512,512},0x246810);CHECK(GetPixel(other,511,511)==color(0x246810));CHECK(ReleaseDC(pixels,other));CHECK(DestroyWindow(pixels));
    for(i=0;i<4;++i){budget[i]=CreateWindowExA(WS_EX_NOACTIVATE,cls.lpszClassName,"slot budget",WS_POPUP|WS_VISIBLE,300+(int)i*20,120,8,8,NULL,NULL,cls.hInstance,NULL);CHECK(budget[i]);other=GetDC(budget[i]);CHECK(other);fill(other,(RECT){0,0,8,8},0x556677);CHECK(ReleaseDC(budget[i],other));}
    stage(4,w,dc);
    for(i=0;i<4;++i)CHECK(DestroyWindow(budget[i]));
    CHECK(DestroyWindow(child));CHECK(DestroyWindow(large));CHECK(MoveWindow(w,80,90,144,128,TRUE));
    fill(dc,(RECT){0,0,144,128},0x4162a3);CHECK(GetPixel(dc,140,120)==color(0x4162a3));stage(5,w,dc);
    CHECK(ReleaseDC(w,dc));CHECK(DestroyWindow(w));CHECK(SelectObject(mem,old));CHECK(DeleteObject(dib));CHECK(DeleteDC(mem));
    DispatchProbe[0]=6;ExitProcess(0);
}
