/* Actual Win32 reads/ROP/raw DIB/layout consume native-injected GPU changes. */
#include <windows.h>
__declspec(dllexport) volatile DWORD CoherenceProbe[8];
#define CHECK(x) do {if(!(x))ExitProcess(__LINE__);}while(0)
static COLORREF color(DWORD c){return RGB((c>>16)&255,(c>>8)&255,c&255);}
static void stage(DWORD n,HWND w,HDC dc){CoherenceProbe[1]=(DWORD)(ULONG_PTR)w;CoherenceProbe[2]=(DWORD)(ULONG_PTR)dc;CoherenceProbe[0]=n;Sleep(32);}
static LRESULT CALLBACK proc(HWND w,UINT m,WPARAM a,LPARAM b){return DefWindowProcA(w,m,a,b);}
void WINAPI start(void) {
    WNDCLASSA cls={0};HWND w,second;HDC dc,other,mem;BITMAPINFO info={0};HBITMAP dib,old;DWORD* bits;int i;
    cls.lpfnWndProc=proc;cls.hInstance=GetModuleHandleA(NULL);cls.lpszClassName="GpuCoherence";
    CHECK(RegisterClassA(&cls));
    w=CreateWindowExA(WS_EX_NOACTIVATE,cls.lpszClassName,"coherent",WS_POPUP|WS_VISIBLE,
        40,50,128,128,NULL,NULL,cls.hInstance,NULL);CHECK(w);dc=GetDC(w);CHECK(dc);
    {RECT r={0,0,128,128};HBRUSH brush=CreateSolidBrush(color(0x112233));CHECK(brush);CHECK(FillRect(dc,&r,brush));CHECK(DeleteObject(brush));}
    mem=CreateCompatibleDC(dc);CHECK(mem);info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=16;info.bmiHeader.biHeight=-16;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    dib=CreateDIBSection(mem,&info,DIB_RGB_COLORS,(void**)&bits,NULL,0);CHECK(dib&&bits);old=SelectObject(mem,dib);CHECK(old);
    second=CreateWindowExA(WS_EX_NOACTIVATE,cls.lpszClassName,"second",WS_POPUP|WS_VISIBLE,
        300,50,16,16,NULL,NULL,cls.hInstance,NULL);CHECK(second);other=GetDC(second);CHECK(other);
    CoherenceProbe[3]=(DWORD)(ULONG_PTR)second;CoherenceProbe[4]=(DWORD)(ULONG_PTR)other;
    stage(1,w,dc);
    for(i=0;i<64;++i)CHECK(GetPixel(dc,(i%8)*16+8,(i/8)*16+8)==color((0x203040u+(DWORD)i*0x12345u)&0xffffff));
    CHECK(GetPixel(other,2,2)==color(0x4162a3));
    stage(2,w,dc);
    CHECK(PatBlt(dc,5,5,16,16,DSTINVERT));CHECK(GetPixel(dc,6,6)==color(0x778899));
    CHECK(BitBlt(mem,0,0,16,16,dc,5,5,SRCCOPY));CHECK((bits[0]&0xffffff)==0x778899);
    stage(3,w,dc);
    /* This memory remains CPU-authoritative. Real guest stores must be visible
       to the next source blit, with no stale GPU copy of the DIB. */
    for(i=0;i<16*16;++i)bits[i]=0x123abc;
    CHECK(BitBlt(dc,30,30,4,4,mem,0,0,SRCCOPY));
    CHECK(GetPixel(dc,31,31)==color(0x123abc));CHECK(GetPixel(dc,38,38)==color(0xaabbcc));
    stage(4,w,dc);
    CHECK(BitBlt(mem,0,0,1,1,dc,64,64,SRCCOPY));CHECK((bits[0]&0xffffff)==0xdead00);
    {DWORD output[16*16];CHECK(SelectObject(mem,old));CHECK(GetDIBits(dc,dib,0,16,output,&info,DIB_RGB_COLORS)==16);CHECK((output[0]&0xffffff)==0xdead00);}
    stage(5,w,dc);
    CHECK(MoveWindow(w,80,90,128,128,TRUE));stage(6,w,dc);
    CHECK(MoveWindow(w,80,90,144,128,TRUE));stage(7,w,dc);
    CHECK(ReleaseDC(w,dc));CHECK(DestroyWindow(w));
    w=CreateWindowExA(WS_EX_NOACTIVATE,cls.lpszClassName,"fallback",WS_POPUP|WS_VISIBLE,
        40,50,128,128,NULL,NULL,cls.hInstance,NULL);CHECK(w);dc=GetDC(w);CHECK(dc);
    stage(8,w,dc);CHECK(GetPixel(dc,1,1)==color(0xee6622));
    stage(9,w,dc);CHECK(GetPixel(dc,2,2)==color(0x5566aa));
    CHECK(ReleaseDC(w,dc));CHECK(DestroyWindow(w));CHECK(DeleteObject(dib));CHECK(DeleteDC(mem));
    CHECK(ReleaseDC(second,other));CHECK(DestroyWindow(second));
    CoherenceProbe[0]=10;ExitProcess(0);
}
