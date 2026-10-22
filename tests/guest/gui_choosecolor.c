#include <windows.h>
#include <commdlg.h>
#include <stdio.h>
#include <string.h>
static int failed,mode,depth,initCount,okCount,interactive;
static UINT setRgb,colorOk;
static HWND owner;
static COLORREF custom[16];
__declspec(dllexport) volatile DWORD ColorProbeStage;
#define CHECK(x) do {if(!(x)){printf("FAIL ChooseColor line %d: %s\n",__LINE__,#x);++failed;}}while(0)
static UINT_PTR CALLBACK Hook(HWND h,UINT msg,WPARAM wp,LPARAM lp) {
    (void)wp;
    if(msg==WM_INITDIALOG) {
        CHOOSECOLORW* cc=(CHOOSECOLORW*)lp;
        ++initCount;CHECK(cc->lStructSize==sizeof(*cc));CHECK(cc->lCustData==0x12345678);
        CHECK(IsWindow(h));CHECK(!IsWindowEnabled(cc->hwndOwner));
        if(mode==6)ExitProcess(42);
        if(mode==8){ColorProbeStage=1;return 0;}
        if(mode==5){DestroyWindow(h);return 0;}
        if(mode==7){PostQuitMessage(77);return 0;}
        if(mode==3 && !depth) {
            CHOOSECOLORW inner=*cc;inner.hwndOwner=h;depth=1;
            CHECK(!ChooseColorW(&inner));CHECK(IsWindowEnabled(h));depth=0;
        }
        if(mode==9) {
            CHECK(!IsWindowVisible(GetDlgItem(h,706)));
            SendMessageW(h,WM_COMMAND,719,0);CHECK(IsWindowVisible(GetDlgItem(h,706)));
        }
        if(mode==10) {
            CHECK(!IsWindowEnabled(GetDlgItem(h,719)));CHECK(!IsWindowVisible(GetDlgItem(h,706)));
            SendMessageW(h,WM_COMMAND,719,0);CHECK(!IsWindowVisible(GetDlgItem(h,706)));
        }
        if(depth || mode==1 || mode==10) {PostMessageW(h,WM_COMMAND,IDCANCEL,0);return 0;}
        if(interactive)return 0;
        SendMessageW(h,setRgb,0,RGB(17,34,51));
        if(mode==4) {
            SetDlgItemTextW(h,706,L"201");SetDlgItemTextW(h,707,L"73");SetDlgItemTextW(h,708,L"19");
            SendMessageW(h,WM_COMMAND,712,0); // add custom color
            PostMessageW(h,WM_COMMAND,IDCANCEL,0);
        } else PostMessageW(h,WM_COMMAND,IDOK,0);
        return 0;
    }
    if(msg==colorOk) {
        CHOOSECOLORW* cc=(CHOOSECOLORW*)lp;++okCount;
        CHECK(cc->rgbResult==(interactive?RGB(255,0,0):RGB(17,34,51)));
        if(mode==2 && okCount==1){PostMessageW(h,WM_COMMAND,IDOK,0);return 1;}
    }
    return 0;
}
static CHOOSECOLORW Request(void) {
    CHOOSECOLORW cc;memset(&cc,0,sizeof(cc));cc.lStructSize=sizeof(cc);
    cc.hwndOwner=owner;cc.rgbResult=RGB(7,8,9);cc.lpCustColors=custom;
    cc.Flags=CC_RGBINIT|CC_FULLOPEN|CC_ENABLEHOOK;cc.lpfnHook=Hook;cc.lCustData=0x12345678;
    return cc;
}
int main(int argc,char** argv) {
    HMODULE dll=LoadLibraryW(L"comdlg32.dll");
    typedef BOOL(WINAPI *ChooseW)(CHOOSECOLORW*);
    typedef BOOL(WINAPI *ChooseA)(CHOOSECOLORA*);
    ChooseW chooseW=(ChooseW)GetProcAddress(dll,"ChooseColorW");
    ChooseA chooseA=(ChooseA)GetProcAddress(dll,"ChooseColorA");
    CHECK(chooseW && chooseA);if(!chooseW || !chooseA)return 1;
    owner=CreateWindowExW(0,L"Static",L"Color owner",WS_OVERLAPPEDWINDOW|WS_VISIBLE,
        10,10,220,130,0,0,GetModuleHandleW(NULL),0);CHECK(owner!=NULL);
    setRgb=RegisterWindowMessageW(SETRGBSTRINGW);colorOk=RegisterWindowMessageA(COLOROKSTRINGA);
    CHECK(colorOk==RegisterWindowMessageW(COLOROKSTRINGW));
    CHOOSECOLORW cc=Request();cc.lStructSize=0;
    CHECK(!chooseW(&cc) && CommDlgExtendedError()==CDERR_STRUCTSIZE);
    cc=Request();cc.lpfnHook=0;CHECK(!chooseW(&cc) && CommDlgExtendedError()==CDERR_NOHOOK);
    interactive=argc>1 && strcmp(argv[1],"interactive")==0;
    if(argc>1 && strcmp(argv[1],"exit")==0){mode=6;cc=Request();chooseW(&cc);puts("FAIL: returned after hook ExitProcess");return 1;}
    if(argc>1 && strcmp(argv[1],"hostabort")==0){mode=8;cc=Request();chooseW(&cc);puts("FAIL: returned after host abort");return 1;}
    if(interactive) {
        cc=Request();cc.Flags&=~CC_ENABLEHOOK;CHECK(chooseW(&cc));CHECK(cc.rgbResult==RGB(255,0,0));
    } else {
        for(mode=0;mode<=10;++mode) {
            if(mode>=6 && mode<=8)continue;
            int before=initCount;okCount=0;cc=Request();
            if(mode==9 || mode==10)cc.Flags&=~CC_FULLOPEN;
            if(mode==10)cc.Flags|=CC_PREVENTFULLOPEN;
            DWORD flags=cc.Flags;
            BOOL result=mode==0?chooseA((CHOOSECOLORA*)&cc):chooseW(&cc);
            CHECK(CommDlgExtendedError()==0);CHECK(IsWindowEnabled(owner));CHECK(cc.Flags==flags);
            CHECK(initCount==before+(mode==3?2:1));
            if(mode==1 || mode==4 || mode==5 || mode==10) {CHECK(!result);CHECK(cc.rgbResult==RGB(7,8,9));}
            else {CHECK(result);CHECK(cc.rgbResult==RGB(17,34,51));CHECK(okCount==(mode==2?2:1));}
            if(mode==4)CHECK(custom[0]==RGB(201,73,19));
        }
        mode=7;cc=Request();CHECK(!chooseW(&cc));MSG msg;
        CHECK(PeekMessageW(&msg,0,0,0,PM_REMOVE) && msg.message==WM_QUIT && msg.wParam==77);
        CHECK(IsWindowEnabled(owner));
    }
    DestroyWindow(owner);
    if(!failed)puts("PASSED: ChooseColor A/W dynamic lookup, RGB/custom colors, hooks/veto/nesting, cancel/destroy/quit and owner restore");
    return failed?1:0;
}
