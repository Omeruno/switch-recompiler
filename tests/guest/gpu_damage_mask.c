/* Public odd-size window, child/occluder, selected clip and mapped sparse writes. */
#include <windows.h>
__declspec(dllexport) volatile DWORD DamageProbe[8];
#define CHECK(x) do {if(!(x))ExitProcess(__LINE__);}while(0)
static void stage(DWORD n,HWND w,HDC dc){DamageProbe[1]=(DWORD)(ULONG_PTR)w;DamageProbe[2]=(DWORD)(ULONG_PTR)dc;DamageProbe[0]=n;Sleep(32);}
static LRESULT CALLBACK proc(HWND w,UINT m,WPARAM a,LPARAM b){return DefWindowProcA(w,m,a,b);}
static void fill(HDC dc,RECT r,COLORREF color){HBRUSH b=CreateSolidBrush(color);CHECK(b&&FillRect(dc,&r,b));CHECK(DeleteObject(b));}
void WINAPI start(void){
    WNDCLASSA cls={0};HWND w,child,cover;HDC dc,c,o;HRGN region,hole;int x,y,i=0;
    cls.lpfnWndProc=proc;cls.hInstance=GetModuleHandleA(NULL);cls.lpszClassName="ExactDamage";CHECK(RegisterClassA(&cls));
    w=CreateWindowExA(WS_EX_NOACTIVATE,cls.lpszClassName,"odd",WS_POPUP|WS_VISIBLE|WS_CLIPCHILDREN|WS_CLIPSIBLINGS,40,50,37,29,NULL,NULL,cls.hInstance,NULL);CHECK(w);dc=GetDC(w);CHECK(dc);fill(dc,(RECT){0,0,37,29},RGB(0x11,0x22,0x33));
    child=CreateWindowExA(0,cls.lpszClassName,"child",WS_CHILD|WS_VISIBLE,8,9,7,7,w,NULL,cls.hInstance,NULL);CHECK(child);c=GetDC(child);CHECK(c);fill(c,(RECT){0,0,7,7},RGB(0x55,0x66,0x77));
    cover=CreateWindowExA(WS_EX_NOACTIVATE,cls.lpszClassName,"cover",WS_POPUP|WS_VISIBLE,70,68,6,6,NULL,NULL,cls.hInstance,NULL);CHECK(cover);o=GetDC(cover);CHECK(o);fill(o,(RECT){0,0,6,6},RGB(0x77,0x88,0x99));
    region=CreateRectRgn(1,1,36,28);hole=CreateRectRgn(18,12,22,17);CHECK(region&&hole);CHECK(CombineRgn(region,region,hole,RGN_DIFF));CHECK(SelectClipRgn(dc,region));CHECK(DeleteObject(region)&&DeleteObject(hole));
    CHECK(SetWindowOrgEx(dc,1,-1,NULL)&&SetViewportOrgEx(dc,3,2,NULL));stage(1,w,dc);
    for(y=-3;y<29;y+=2)for(x=-2;x<37;x+=2){DWORD rgb=(0x203040u+(DWORD)i++*0x1379u)&0xffffff;fill(dc,(RECT){x,y,x+1,y+1},RGB((rgb>>16)&255,(rgb>>8)&255,rgb&255));}
    stage(2,w,dc);
    CHECK(GetPixel(c,2,2)==RGB(0x55,0x66,0x77));CHECK(GetPixel(o,2,2)==RGB(0x77,0x88,0x99));stage(3,w,dc);
    CHECK(ReleaseDC(child,c)&&ReleaseDC(cover,o)&&ReleaseDC(w,dc));CHECK(DestroyWindow(w)&&DestroyWindow(cover));DamageProbe[0]=4;ExitProcess(0);
}
