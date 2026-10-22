#include <windows.h>
#include <stdio.h>
#include <string.h>
static int failures,scenario,initSubclass,commands,ticks,creatingNested,nestedInit;
static INT_PTR CALLBACK Dialog(HWND,UINT,WPARAM,LPARAM);
__declspec(dllexport) volatile DWORD DialogProbeStage;
static HHOOK hook;
static WNDPROC previous;
#define CHECK(x) do {if(!(x)){printf("FAIL dialog subclass line %d: %s\n",__LINE__,#x);++failures;}}while(0)
static INT_PTR CALLBACK Replaced(HWND h,UINT msg,WPARAM wp,LPARAM lp) {
    (void)lp;
    if(msg==WM_COMMAND && LOWORD(wp)==IDOK){++commands;EndDialog(h,46);return TRUE;}
    if(msg==WM_TIMER){if(++ticks==1)SendDlgItemMessageW(h,IDOK,BM_CLICK,0,0);
        if(ticks>3)EndDialog(h,-1);return TRUE;}
    return FALSE;
}
static LRESULT CALLBACK Subclass(HWND h,UINT msg,WPARAM wp,LPARAM lp) {
    if(msg==WM_INITDIALOG)++initSubclass;
    if(msg==WM_COMMAND && (LOWORD(wp)==IDOK || LOWORD(wp)==IDCANCEL)) {
        ++commands;EndDialog(h,LOWORD(wp));return 0;
    }
    if(msg==WM_CLOSE){++commands;EndDialog(h,IDCANCEL);return 0;}
    return CallWindowProcW(previous,h,msg,wp,lp);
}
static INT_PTR CALLBACK CreationProc(HWND h,UINT msg,WPARAM wp,LPARAM lp) {
    (void)wp;(void)lp;
    if(msg==WM_INITDIALOG){CHECK(GetDlgItem(h,IDOK)!=NULL);++commands;EndDialog(h,47);return TRUE;}
    return FALSE;
}
static INT_PTR CALLBACK Nested(HWND h,UINT msg,WPARAM wp,LPARAM lp) {
    (void)wp;(void)lp;
    if(msg==WM_INITDIALOG){++nestedInit;EndDialog(h,17);return TRUE;}
    return FALSE;
}
static INT_PTR CALLBACK AnsiProc(HWND h,UINT msg,WPARAM wp,LPARAM lp) {
    (void)wp;
    if(msg==WM_SETTEXT){CHECK(strcmp((const char*)lp,"Converted title")==0);return FALSE;}
    if(msg==WM_APP+1){SetWindowLongA(h,DWL_MSGRESULT,0x12345678);return TRUE;}
    if(msg==WM_APP+2)return TRUE;
    if(msg==WM_COMMAND){++commands;EndDialog(h,48);return TRUE;}
    return FALSE;
}
static LRESULT CALLBACK Cbt(int code,WPARAM wp,LPARAM lp) {
    if(code==HCBT_CREATEWND) {
        WCHAR cls[32];GetClassNameW((HWND)wp,cls,32);
        if(lstrcmpW(cls,L"#32770")==0) {
            previous=(WNDPROC)SetWindowLongW((HWND)wp,GWL_WNDPROC,(LONG)Subclass);
            CHECK(previous!=NULL);
            if(scenario==4 && !creatingNested) {
                CHECK((DLGPROC)GetWindowLongW((HWND)wp,DWL_DLGPROC)==Dialog);
                CHECK((DLGPROC)SetWindowLongW((HWND)wp,DWL_DLGPROC,(LONG)CreationProc)==Dialog);
                creatingNested=1;
                CHECK(DialogBoxParamW(GetModuleHandleW(NULL),MAKEINTRESOURCEW(1),0,Nested,0)==17);
                creatingNested=0;
                CHECK((DLGPROC)GetWindowLongW((HWND)wp,DWL_DLGPROC)==CreationProc);
            }
        }
    }
    return CallNextHookEx(hook,code,wp,lp);
}
static INT_PTR CALLBACK Dialog(HWND h,UINT msg,WPARAM wp,LPARAM lp) {
    (void)wp;(void)lp;
    if(msg==WM_INITDIALOG) {
        if(scenario==7){DialogProbeStage=1;return TRUE;}
        if(scenario==8)ExitProcess(42);
        if(scenario==5) {
            CHECK((DLGPROC)SetWindowLongA(h,DWL_DLGPROC,(LONG)AnsiProc)==Dialog);
            SendMessageW(h,WM_SETTEXT,0,(LPARAM)L"Converted title");
            CHECK(SendMessageW(h,WM_APP+1,0,0)==0x12345678);
            CHECK(SendMessageW(h,WM_APP+2,0,0)==0);
            PostMessageW(h,WM_COMMAND,IDOK,0);return TRUE;
        }
        if(scenario<3 || scenario==6)CHECK((WNDPROC)GetWindowLongW(h,GWL_WNDPROC)==Subclass);
        else {
            CHECK((DLGPROC)GetWindowLongW(h,DWL_DLGPROC)==Dialog);
            CHECK((DLGPROC)SetWindowLongW(h,DWL_DLGPROC,(LONG)Replaced)==Dialog);
            CHECK((DLGPROC)GetWindowLongW(h,DWL_DLGPROC)==Replaced);
        }
        SetTimer(h,1,10,NULL);return TRUE;
    }
    if(msg==WM_TIMER) {
        if(++ticks==1) {
            if(scenario==2)PostMessageW(h,WM_CLOSE,0,0);
            else SendDlgItemMessageW(h,scenario==1?IDCANCEL:IDOK,BM_CLICK,0,0);
        }
        if(ticks>3)EndDialog(h,-1);
        return TRUE;
    }
    return FALSE;
}
int main(int argc,char** argv) {
    HINSTANCE instance=GetModuleHandleW(NULL);
    if(argc>1) {
        scenario=strcmp(argv[1],"exit")==0?8:7;
        DialogBoxParamW(instance,MAKEINTRESOURCEW(1),0,Dialog,0);
        puts("FAIL: dialog returned after host/process stop");return 1;
    }
    for(scenario=0;scenario<7;++scenario) {
        commands=ticks=initSubclass=0;
        if(scenario<3 || scenario==4 || scenario==6) {hook=SetWindowsHookExW(WH_CBT,Cbt,NULL,GetCurrentThreadId());CHECK(hook!=NULL);}
        INT_PTR result;
        if(scenario==6) {
            HWND dlg=CreateDialogParamW(instance,MAKEINTRESOURCEW(1),0,Dialog,0);CHECK(dlg!=NULL);
            ShowWindow(dlg,SW_SHOW);
            MSG msg;
            while(IsWindowVisible(dlg) && GetMessageW(&msg,0,0,0)>0) {
                if(!IsDialogMessageW(dlg,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}
            }
            result=commands?IDOK:-1;CHECK(DestroyWindow(dlg));
        } else result=DialogBoxParamW(instance,MAKEINTRESOURCEW(1),0,Dialog,0);
        if(scenario<3 || scenario==4 || scenario==6) {CHECK(UnhookWindowsHookEx(hook));hook=0;CHECK(initSubclass==(scenario==4?2:1));}
        CHECK(result==(scenario==0?IDOK:scenario<3?IDCANCEL:scenario==3?46:scenario==4?47:scenario==5?48:IDOK));CHECK(commands==1);
    }
    CHECK(nestedInit==1);
    if(!failures)puts("PASSED: CBT-installed dialog subclass survives creation, receives init/OK/Cancel/close; DWL_DLGPROC replacement dispatches commands");
    return failures?1:0;
}
