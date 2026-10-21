#include <windows.h>
#include <stdio.h>
static HANDLE initial;
static LRESULT CALLBACK WindowProc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    if (message==WM_APP+1) ExitThread(73);
    return DefWindowProcA(window,message,wp,lp);
}
static DWORD WINAPI Survivor(LPVOID unused) {
    (void)unused;
    DWORD code=0;
    if (WaitForSingleObject(initial,INFINITE)!=WAIT_OBJECT_0) return 10;
    if (!GetExitCodeThread(initial,&code) || code!=73) return 11;
    if (!CloseHandle(initial)) return 12;
    puts("PASSED: ExitThread inside WndProc unwinds native frames and preserves peer");
    return 0;
}
int main(void) {
    WNDCLASSA cls={0};
    cls.lpfnWndProc=WindowProc;cls.hInstance=GetModuleHandleA(NULL);cls.lpszClassName="CallbackExit";
    if (!RegisterClassA(&cls)) return 1;
    HWND window=CreateWindowA(cls.lpszClassName,"exit",0,0,0,100,100,NULL,NULL,cls.hInstance,NULL);
    if (!window || !DuplicateHandle(GetCurrentProcess(),GetCurrentThread(),GetCurrentProcess(),
                                    &initial,0,FALSE,DUPLICATE_SAME_ACCESS)) return 2;
    HANDLE worker=CreateThread(NULL,0,Survivor,NULL,0,NULL);
    if (!worker || !CloseHandle(worker)) return 3;
    SendMessageA(window,WM_APP+1,0,0);
    return 4; // must never execute
}
