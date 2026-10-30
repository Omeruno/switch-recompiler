#include <windows.h>
__declspec(dllexport) volatile DWORD MenuProbe[12];
#define CHECK(x) do{if(!(x))ExitProcess(__LINE__);}while(0)
static LRESULT CALLBACK Proc(HWND h,UINT msg,WPARAM w,LPARAM l) {
    if(msg==WM_NCCREATE || msg==WM_CREATE) {
        const CREATESTRUCTA* cs=(const CREATESTRUCTA*)l;
        if(!(cs->style&WS_CHILD)){CHECK(GetMenu(h) && GetMenu(h)==cs->hMenu);}
        if(msg==WM_CREATE && cs->lpCreateParams==(void*)1)return -1;
        if(msg==WM_CREATE && cs->lpCreateParams==(void*)2){MenuProbe[2]=(DWORD)GetMenu(h);MenuProbe[1]=9;Sleep(INFINITE);}
    }
    if(msg==WM_INITMENUPOPUP && MenuProbe[0]==2){MenuProbe[1]=9;Sleep(INFINITE);}
    if(msg==WM_COMMAND){CHECK(LOWORD(w)==201 && HIWORD(w)==0 && l==0);++MenuProbe[3];return 0;}
    return DefWindowProcA(h,msg,w,l);
}
void WINAPI start(void) {
    HINSTANCE instance=GetModuleHandleW(0);WNDCLASSEXA a={0};WNDCLASSW w={0};char menuName[]="NamedMenu";
    a.cbSize=sizeof(a);a.lpfnWndProc=Proc;a.hInstance=instance;a.lpszClassName="ClassMenuA";a.lpszMenuName=menuName;CHECK(RegisterClassExA(&a));
    menuName[0]='X'; /* Registration must own the resource name, not borrow this buffer. */
    HWND one=CreateWindowA("ClassMenuA","One",WS_OVERLAPPEDWINDOW,20,20,320,200,0,0,instance,MenuProbe[0]==1?(void*)2:0);CHECK(one);
    HMENU first=GetMenu(one),sub=GetSubMenu(first,0);CHECK(first && sub && GetMenuItemID(sub,0)==201);MenuProbe[2]=(DWORD)first;
    HWND two=CreateWindowA("ClassMenuA","Two",WS_OVERLAPPEDWINDOW,400,20,320,200,0,0,instance,0);CHECK(two);
    HMENU second=GetMenu(two);CHECK(second!=first && GetSubMenu(second,0)!=sub);
    CHECK(DeleteMenu(sub,201,MF_BYCOMMAND));CHECK(GetMenuItemID(GetSubMenu(second,0),0)==201);
    CHECK(DestroyWindow(one) && !IsMenu(first) && !IsMenu(sub) && IsMenu(second));
    HMENU explicitMenu=CreateMenu();CHECK(explicitMenu && AppendMenuA(explicitMenu,MF_STRING,900,"Override"));
    HWND over=CreateWindowA("ClassMenuA","Override",WS_OVERLAPPEDWINDOW,0,0,120,100,0,explicitMenu,instance,0);CHECK(over && GetMenu(over)==explicitMenu);
    CHECK(GetMenuItemCount(GetMenu(over))==1 && GetMenuItemID(GetMenu(over),0)==900);
    HWND child=CreateWindowA("ClassMenuA","Child",WS_CHILD,0,0,30,30,two,(HMENU)77,instance,0);CHECK(child && !GetMenu(child) && GetDlgCtrlID(child)==77);
    w.lpfnWndProc=DefWindowProcW;w.hInstance=instance;w.lpszClassName=L"ClassMenuW";w.lpszMenuName=MAKEINTRESOURCEW(101);CHECK(RegisterClassW(&w));
    HWND numeric=CreateWindowW(w.lpszClassName,L"Numeric",WS_OVERLAPPEDWINDOW,0,0,160,100,0,0,instance,0);CHECK(numeric);
    CHECK(GetMenuItemID(GetSubMenu(GetMenu(numeric),0),0)==202);
    SetClassLongW(numeric,GCL_MENUNAME,(LONG)MAKEINTRESOURCEW(102));
    HWND extended=CreateWindowW(w.lpszClassName,L"Extended",WS_OVERLAPPEDWINDOW,0,0,160,100,0,0,instance,0);CHECK(extended);
    CHECK(GetMenuItemID(GetSubMenu(GetMenu(extended),0),0)==204);
    CHECK(!CreateWindowA("ClassMenuA","Rejected",0,0,0,100,100,0,0,instance,(void*)1));
    HMODULE dll=LoadLibraryA("class_menus.dll");CHECK(dll);
    w.hInstance=dll;w.lpszClassName=L"ClassMenuDLL";w.lpszMenuName=L"DllMenu";CHECK(RegisterClassW(&w));
    HWND module=CreateWindowW(w.lpszClassName,L"DLL",0,0,0,120,100,0,0,dll,0);CHECK(module);
    HMENU moduleMenu=GetMenu(module);CHECK(GetMenuItemID(GetSubMenu(moduleMenu,0),0)==303);
    CHECK(FreeLibrary(dll) && IsMenu(moduleMenu)); /* menu is an owning copy */
    CHECK(!CreateWindowW(w.lpszClassName,L"Unloaded",0,0,0,120,100,0,0,dll,0));
    CHECK(DestroyWindow(module) && !IsMenu(moduleMenu));
    HMENU detached=GetMenu(numeric);CHECK(SetMenu(numeric,0) && IsMenu(detached));
    CHECK(DestroyWindow(numeric) && IsMenu(detached) && DestroyMenu(detached));
    CHECK(DestroyWindow(extended));CHECK(DestroyWindow(over) && !IsMenu(explicitMenu));
    ShowWindow(two,SW_SHOW);SetForegroundWindow(two);SetFocus(two);MenuProbe[2]=(DWORD)second;MenuProbe[1]=1;
    SendMessageA(two,WM_SYSCOMMAND,SC_KEYMENU,0);
    MSG msg;while(!MenuProbe[3]){CHECK(GetMessageA(&msg,0,0,0)>0);TranslateMessage(&msg);DispatchMessageA(&msg);}
    CHECK(MenuProbe[3]==1 && DestroyWindow(two) && !IsMenu(second));MenuProbe[4]=1;ExitProcess(0);
}
