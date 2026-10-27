/* Public ordinary GDI: one persistent window/DC, nine full tiled frames. */
#include <windows.h>
__declspec(dllexport) volatile DWORD WarmProbe[4];
#define CHECK(x) do {if(!(x))ExitProcess(__LINE__);}while(0)
static DWORD tile(unsigned frame,unsigned i){return (0x203040u+i*0x12345u+frame*0x102019u)&0xffffff;}
static COLORREF color(DWORD c){return RGB((c>>16)&255,(c>>8)&255,c&255);}
static void stage(DWORD n,HWND w,HDC dc){WarmProbe[1]=(DWORD)(ULONG_PTR)w;WarmProbe[2]=(DWORD)(ULONG_PTR)dc;WarmProbe[0]=n;Sleep(32);}
static LRESULT CALLBACK proc(HWND w,UINT m,WPARAM a,LPARAM b){return DefWindowProcA(w,m,a,b);}
void WINAPI start(void){
    WNDCLASSA cls={0};HWND w;HDC dc;unsigned frame,i;
    cls.lpfnWndProc=proc;cls.hInstance=GetModuleHandleA(NULL);cls.lpszClassName="WarmGdi";CHECK(RegisterClassA(&cls));
    w=CreateWindowExA(WS_EX_NOACTIVATE,cls.lpszClassName,"warm",WS_POPUP|WS_VISIBLE,40,50,128,128,NULL,NULL,cls.hInstance,NULL);CHECK(w);dc=GetDC(w);CHECK(dc);
    {RECT r={0,0,128,128};HBRUSH b=CreateSolidBrush(color(0x112233));CHECK(b&&FillRect(dc,&r,b));CHECK(DeleteObject(b));}
    stage(1,w,dc);
    for(frame=0;frame<9;++frame){
        for(i=0;i<64;++i){RECT r={(i%8)*16,(i/8)*16,(i%8+1)*16,(i/8+1)*16};HBRUSH b=CreateSolidBrush(color(tile(frame,i)));CHECK(b);
            if(i&1){HGDIOBJ old=SelectObject(dc,b);CHECK(old&&PatBlt(dc,r.left,r.top,16,16,PATCOPY));CHECK(SelectObject(dc,old));}
            else {CHECK(FillRect(dc,&r,b));}
            CHECK(DeleteObject(b));}
        stage(frame+2,w,dc);
    }
    for(i=0;i<64;++i)CHECK(GetPixel(dc,(i%8)*16+8,(i/8)*16+8)==color(tile(8,i)));
    stage(11,w,dc);CHECK(ReleaseDC(w,dc));CHECK(DestroyWindow(w));WarmProbe[0]=12;ExitProcess(0);
}
