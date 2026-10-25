/* Public x86 fixture: actual window/DC lifetime, geometry and clipping. */
#include <windows.h>
__declspec(dllexport) volatile DWORD GpuWindowProbe[8];
static void stage(DWORD n,HWND w,HDC dc,HDC other,HDC mem) {
    GpuWindowProbe[1]=(DWORD)(ULONG_PTR)w;GpuWindowProbe[2]=(DWORD)(ULONG_PTR)dc;
    GpuWindowProbe[3]=(DWORD)(ULONG_PTR)other;GpuWindowProbe[4]=(DWORD)(ULONG_PTR)mem;
    GpuWindowProbe[0]=n;Sleep(32);
}
static LRESULT CALLBACK proc(HWND w,UINT m,WPARAM a,LPARAM b) {
    return DefWindowProcA(w,m,a,b);
}
#define CHECK(x) do {if(!(x))ExitProcess(__LINE__);}while(0)
void WINAPI start(void) {
    WNDCLASSA cls={0};HWND w,cover;HDC dc,other,mem;HRGN clip;
    cls.lpfnWndProc=proc;cls.hInstance=GetModuleHandleA(NULL);cls.lpszClassName="GpuWindowBindings";
    CHECK(RegisterClassA(&cls));
    w=CreateWindowExA(WS_EX_NOACTIVATE,cls.lpszClassName,"binding",WS_POPUP|WS_VISIBLE|WS_CLIPSIBLINGS,
        40,50,100,80,NULL,NULL,cls.hInstance,NULL);CHECK(w);
    dc=GetDC(w);other=GetWindowDC(w);mem=CreateCompatibleDC(NULL);CHECK(dc&&other&&mem);
    stage(1,w,dc,other,mem);
    cover=CreateWindowExA(WS_EX_NOACTIVATE,cls.lpszClassName,"cover",WS_POPUP|WS_VISIBLE,
        60,60,20,20,NULL,NULL,cls.hInstance,NULL);CHECK(cover);
    CHECK(SetViewportOrgEx(dc,3,4,NULL));clip=CreateRectRgn(2,1,40,35);CHECK(clip);
    CHECK(SelectClipRgn(dc,clip)!=ERROR);CHECK(DeleteObject(clip));stage(2,w,dc,other,mem);
    CHECK(ReleaseDC(w,dc));stage(3,w,dc,other,mem);
    CHECK(MoveWindow(w,70,80,100,80,TRUE));stage(4,w,dc,other,mem);
    CHECK(MoveWindow(w,70,80,120,90,TRUE));stage(5,w,dc,other,mem);
    ShowWindow(w,SW_HIDE);stage(6,w,dc,other,mem);
    ShowWindow(w,SW_SHOWNOACTIVATE);stage(7,w,dc,other,mem);
    CHECK(DestroyWindow(w));stage(8,w,dc,other,mem);
    /* The host deliberately reuses the old HWND allocation at this boundary. */
    w=CreateWindowExA(WS_EX_NOACTIVATE,cls.lpszClassName,"reused",WS_POPUP|WS_VISIBLE,
        40,50,100,80,NULL,NULL,cls.hInstance,NULL);CHECK(w);
    dc=GetDC(w);CHECK(dc);stage(9,w,dc,other,mem);
    CHECK(ReleaseDC(w,dc));ReleaseDC(w,other);CHECK(DeleteDC(mem));
    CHECK(DestroyWindow(w));CHECK(DestroyWindow(cover));stage(10,w,dc,other,mem);
    ExitProcess(0);
}
