/* Public fixture: real x86 legacy USER/GDI and synthetic MFC ABI, no Paint binary. */
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <string.h>
__declspec(dllexport) volatile DWORD LegacySessionProbe[8];
static DWORD application[160],document[32],view[32];
static int dialogExit;
#define CHECK(x) do{if(!(x)){printf("FAIL legacy session line %d: %s\n",__LINE__,#x);return __LINE__;}}while(0)
typedef DWORD (__attribute__((thiscall)) *This0)(void*);
typedef DWORD (__attribute__((thiscall)) *This1)(void*,void*);
typedef DWORD (WINAPI *FromHandle)(HDC);
static LRESULT CALLBACK WindowProc(HWND h,UINT msg,WPARAM wp,LPARAM lp) {
    if(msg==WM_APP+7){LegacySessionProbe[2]=(DWORD)wp;return 123;}
    return DefWindowProcW(h,msg,wp,lp);
}
static INT_PTR CALLBACK DialogProc(HWND h,UINT msg,WPARAM wp,LPARAM lp) {
    (void)wp;(void)lp;
    if(msg==WM_INITDIALOG){
        SetDlgItemTextW(h,IDOK,L"Fresh dialog");LegacySessionProbe[0]=2;
        if(dialogExit)ExitProcess(42);
        return TRUE;
    }
    return FALSE;
}
int main(int argc,char** argv) {
    const char* mode=argc>1?argv[1]:"normal";
    HMODULE mfc=LoadLibraryW(L"mfc42u.dll");WNDCLASSW wc;HWND window;HDC dc;RECT area={0,0,8,8};
    This1 app=(This1)GetProcAddress(mfc,(LPCSTR)561),add=(This1)GetProcAddress(mfc,(LPCSTR)1090);
    This0 first=(This0)GetProcAddress(mfc,(LPCSTR)3193),focus=(This0)GetProcAddress(mfc,(LPCSTR)5977);
    FromHandle cdc=(FromHandle)GetProcAddress(mfc,(LPCSTR)2854);
    CHECK(mfc && app && add && first && cdc && focus);CHECK(first(document)==0);
    app(application,0);CHECK(application[0x158/4] && *(WCHAR*)application[0x158/4]==0);
    add(document,view);CHECK(first(document)!=0 && view[0x44/4]==(DWORD)document);
    memset(&wc,0,sizeof(wc));wc.hInstance=GetModuleHandleW(0);wc.lpfnWndProc=WindowProc;wc.lpszClassName=L"LegacySessionWindow";
    CHECK(RegisterClassW(&wc));
    window=CreateWindowExW(0,wc.lpszClassName,L"Legacy",WS_OVERLAPPEDWINDOW,0,0,120,100,0,0,wc.hInstance,0);CHECK(window);
    CHECK(LoadMenuW(wc.hInstance,MAKEINTRESOURCEW(102))!=NULL);
    dc=GetDC(window);CHECK(dc);CHECK(FillRect(dc,&area,(HBRUSH)(COLOR_BTNFACE+1)));
    CHECK(GetPixel(dc,1,1)==GetSysColor(COLOR_BTNFACE));
    {
        DWORD* wrapper=(DWORD*)cdc(dc);CHECK(wrapper && wrapper[1]==(DWORD)dc);
        DWORD* table=(DWORD*)wrapper[0];CHECK(table && table[0]>=0xF0000000u);
        CHECK(((This0)table[0])(wrapper)==0);
        CHECK(*(WCHAR*)application[0x158/4]==0);
        LegacySessionProbe[4]=(DWORD)wrapper;LegacySessionProbe[5]=wrapper[0];
    }
    CHECK(PostMessageW(window,WM_APP+7,0x1357,0));
    {
        MSG msg;CHECK(PeekMessageW(&msg,window,WM_APP+7,WM_APP+7,PM_REMOVE));
        CHECK(DispatchMessageW(&msg)==123 && LegacySessionProbe[2]==0x1357);
    }
    SetCapture(window);view[1]=(DWORD)window;CHECK(focus(view)==0);CHECK(SetTimer(window,77,20,0));
    LegacySessionProbe[1]=(DWORD)window;LegacySessionProbe[6]=(DWORD)document;LegacySessionProbe[0]=1;
    if(strcmp(mode,"dialogexit")==0 || strcmp(mode,"dialogabort")==0) {
        dialogExit=strcmp(mode,"dialogexit")==0;
        DialogBoxParamW(wc.hInstance,MAKEINTRESOURCEW(101),window,DialogProc,0);
        return 99; /* ExitProcess/cancellation must never resume here. */
    }
    if(strcmp(mode,"aboutabort")==0){ShellAboutW(window,L"Legacy fixture",L"Public test\nSecond line",0);return 98;}
    if(strcmp(mode,"abort")==0)for(;;)Sleep(1000);
    /* Leave a real legacy WM_QUIT and timer/paint queue for final session discard. */
    PostQuitMessage(0);puts("PASSED: legacy session fixture");return 0;
}
