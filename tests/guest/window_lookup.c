#define UNICODE
#include <windows.h>
__declspec(dllexport) volatile DWORD LookupProbe[8];
#define CHECK(x) do { if (!(x)) ExitProcess(__LINE__); } while (0)
static const WCHAR cls[]=L"LookupClass\x20ac";
static LRESULT CALLBACK Proc(HWND h,UINT msg,WPARAM w,LPARAM l){return DefWindowProcW(h,msg,w,l);}
void WINAPI start(void) {
    const BOOL legacy=LookupProbe[0]==1;
    HINSTANCE instance=GetModuleHandleW(0);WNDCLASSW wc={0};
    CHECK(!FindWindowW(cls,0));
    wc.lpfnWndProc=Proc;wc.hInstance=instance;wc.lpszClassName=cls;
    ATOM atom=RegisterClassW(&wc);CHECK(atom);
    HWND a=CreateWindowExW(0,cls,L"First Title",0,0,0,160,100,0,0,instance,0);CHECK(a);
    HWND b=CreateWindowExW(0,MAKEINTATOM(atom),L"Second \x20ac",0,0,0,160,100,0,0,instance,0);CHECK(b && b!=a);
    HWND child=CreateWindowExW(0,cls,L"Child",WS_CHILD,0,0,30,30,a,0,instance,0);CHECK(child);
    HWND grand=CreateWindowExW(0,cls,L"Grandchild",WS_CHILD,0,0,10,10,child,0,instance,0);CHECK(grand);
    HWND owned=CreateWindowExW(0,cls,L"Owned",WS_POPUP,0,0,30,30,a,0,instance,0);CHECK(owned);
    HWND empty=CreateWindowExW(0,cls,L"",0,0,0,30,30,0,0,instance,0);CHECK(empty);
    CHECK(FindWindowW(cls,L"first TITLE")==a);
    CHECK(FindWindowA("lookupclass\x80","SECOND \x80")==b);
    CHECK(FindWindowW(MAKEINTATOM(atom),L"First Title")==a);
    CHECK(!FindWindowW(MAKEINTATOM(0xbeef),0));
    CHECK(!FindWindowW(cls,L"Child") && !FindWindowW(cls,L"Grandchild"));
    CHECK(FindWindowW(0,L"owned")==owned && FindWindowW(cls,L"")==empty);
    CHECK(FindWindowExW(a,0,cls,L"child")==child);
    CHECK(FindWindowExA((HWND)0,owned,"LookupClass\x80",0)==b);
    CHECK(FindWindowExW(0,b,MAKEINTATOM(atom),0)==a);
    CHECK(!FindWindowExW(a,0,cls,L"Grandchild"));
    CHECK(FindWindowExW(child,0,cls,L"Grandchild")==grand);
    SetLastError(0x9876);CHECK(FindWindowW(cls,L"First Title")==a && GetLastError()==0x9876);
    CHECK(!FindWindowW(cls,L"absent") && GetLastError()==0x9876);
    CHECK(!FindWindowExW(a,b,0,0) && GetLastError()==ERROR_INVALID_WINDOW_HANDLE);
    CHECK(!FindWindowExW((HWND)0xdeadbeef,0,0,0) && GetLastError()==ERROR_INVALID_WINDOW_HANDLE);
    CHECK(!FindWindowA((LPCSTR)0xdead0000,0) && GetLastError()==ERROR_NOACCESS);
    CHECK(!FindWindowW(0,(LPCWSTR)0xfffffffe) && GetLastError()==ERROR_NOACCESS);
    LookupProbe[2]=(DWORD)a;LookupProbe[3]=(DWORD)atom;LookupProbe[1]=1;
    while (!LookupProbe[4]) Sleep(1);
    if (!legacy) {
        CHECK(SetWindowTextW(a,L"Renamed"));
        CHECK(!FindWindowW(cls,L"First Title") && FindWindowW(cls,L"RENAMED")==a);
        CHECK(DestroyWindow(a));
        CHECK(!FindWindowW(cls,L"Renamed") && !FindWindowExW(0,child,0,0));
        CHECK(!FindWindowExW(child,0,0,0));
        CHECK(!FindWindowW(cls,L"Owned")); /* owner destruction also destroys its popups */
        CHECK(DestroyWindow(b) && DestroyWindow(empty));
        CHECK(!FindWindowW(cls,0));
    }
    LookupProbe[5]=1;ExitProcess(0);
}
