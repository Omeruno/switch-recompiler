/* Authored resources and TrueColor capabilities; no private game payload. */
#include <windows.h>
#include <mmsystem.h>
__declspec(dllexport) volatile DWORD ResourceProbe[12];
#define P ResourceProbe
#define CHECK(x) do{if(!(x))ExitProcess(__LINE__+1000);}while(0)
static int equal(const char* a,const char* b){while(*a && *a==*b){++a;++b;}return *a==*b;}
static void Stage(DWORD stage){P[1]=stage;while(P[2]!=stage)Sleep(0);}
void WINAPI start(void){
    HINSTANCE main=GetModuleHandleA(NULL),dll;HDC dc=GetDC(NULL),memory;char text[64],saved[64];WCHAR wide[64];const WCHAR* pointer=(const WCHAR*)0x12345678;unsigned i;PALETTEENTRY entries[4];
    {DWORD first=RegisterWindowMessageA("Public.Resource.Message"),again=RegisterWindowMessageW(L"public.resource.message");
        DWORD t0=timeGetTime(),t1;
        CHECK(first>=0xc000 && first<=0xffff && first==again && RegisterWindowMessageA("Other.Resource.Message")!=first);
        CHECK(RegisterWindowMessageA("Euro\x80") == RegisterWindowMessageW(L"Euro\x20ac"));
        CHECK(!RegisterWindowMessageA("") && GetLastError()==ERROR_INVALID_PARAMETER);
        CHECK(!RegisterWindowMessageW((LPCWSTR)(ULONG_PTR)0xffffffff) && GetLastError()==ERROR_NOACCESS);
        Sleep(37);t1=timeGetTime();CHECK((DWORD)(t1-t0)>=37 && (DWORD)(GetTickCount()-t1)<1000);P[7]=first;
    }
    CHECK(dc && !(GetDeviceCaps(dc,RASTERCAPS)&RC_PALETTE) && GetDeviceCaps(dc,SIZEPALETTE)==0 && GetDeviceCaps(dc,NUMRESERVED)==0);
    CHECK(SetPixel(dc,1,1,RGB(12,34,56))!=CLR_INVALID);
    for(i=0;i<4;++i){entries[i].peRed=0x61;entries[i].peGreen=0x62;entries[i].peBlue=0x63;entries[i].peFlags=0x64;}
    CHECK(GetSystemPaletteEntries(dc,0,4,entries)==0 && GetLastError()==ERROR_NOT_SUPPORTED && entries[0].peRed==0x61 && entries[3].peFlags==0x64);
    CHECK(GetSystemPaletteEntries(dc,0,0,NULL)==0 && GetSystemPaletteUse(dc)==SYSPAL_ERROR);
    CHECK(SetSystemPaletteUse(dc,SYSPAL_STATIC)==SYSPAL_ERROR && SetSystemPaletteUse(dc,SYSPAL_NOSTATIC)==SYSPAL_ERROR && SetSystemPaletteUse(dc,SYSPAL_NOSTATIC256)==SYSPAL_ERROR);
    CHECK(SetSystemPaletteUse(dc,99)==SYSPAL_ERROR && GetLastError()==ERROR_INVALID_PARAMETER && GetPixel(dc,1,1)==RGB(12,34,56));
    CHECK(SetSystemPaletteUse((HDC)0x12345678,SYSPAL_STATIC)==SYSPAL_ERROR && GetLastError()==ERROR_INVALID_HANDLE);
    CHECK(GetSystemPaletteUse(NULL)==SYSPAL_ERROR && GetLastError()==ERROR_INVALID_HANDLE);
    memory=CreateCompatibleDC(dc);CHECK(memory && GetSystemPaletteUse(memory)==SYSPAL_ERROR && GetLastError()==ERROR_NOT_SUPPORTED);
    if(P[0]==0){
        struct {WORD version,count;PALETTEENTRY colors[2];} palette={0x300,2,{{255,0,0,0},{0,255,0,0}}};
        struct {BITMAPINFOHEADER header;WORD indices[2];} info={0};void* bits=NULL;HPALETTE pal=CreatePalette((LOGPALETTE*)&palette),old;HBITMAP bitmap,previous;
        CHECK(pal);old=SelectPalette(dc,pal,FALSE);CHECK(old);
        info.header.biSize=sizeof(BITMAPINFOHEADER);info.header.biWidth=2;info.header.biHeight=-1;info.header.biPlanes=1;info.header.biBitCount=8;info.header.biClrUsed=2;info.indices[0]=1;info.indices[1]=0;
        bitmap=CreateDIBSection(dc,(BITMAPINFO*)&info,DIB_PAL_COLORS,&bits,NULL,0);CHECK(bitmap && bits);((BYTE*)bits)[0]=0;((BYTE*)bits)[1]=1;
        previous=SelectObject(memory,bitmap);CHECK(previous);
        CHECK(GetPixel(memory,0,0)==RGB(0,255,0) && GetPixel(memory,1,0)==RGB(255,0,0));
        CHECK(SetSystemPaletteUse(memory,SYSPAL_NOSTATIC)==SYSPAL_ERROR && GetPixel(memory,0,0)==RGB(0,255,0));
        CHECK(GetPaletteEntries(pal,0,2,entries)==2 && entries[0].peRed==255 && entries[1].peGreen==255);
        CHECK(SelectObject(memory,previous)==bitmap && DeleteObject(bitmap));CHECK(SelectPalette(dc,old,FALSE)==pal && DeleteObject(pal));
    }
    CHECK(DeleteDC(memory));CHECK(GetSystemPaletteUse(memory)==SYSPAL_ERROR && GetLastError()==ERROR_INVALID_HANDLE);
    CHECK(ReleaseDC(NULL,dc));
    CHECK(LoadStringA(main,1,text,64)==13 && equal(text,"MAIN resource"));CHECK(LoadStringA(NULL,0x10001,text,64)==13 && equal(text,"MAIN resource"));
    CHECK(LoadStringW(main,17,wide,64)==8 && wide[3]==0xe9 && wide[5]==0x20ac && wide[7]==0x416 && !wide[8]);
    CHECK(LoadStringA(main,17,text,64)==8 && (BYTE)text[3]==0xe9 && (BYTE)text[5]==0x80 && text[7]=='?' && !text[8]);
    text[4]='p';CHECK(LoadStringA(main,1,text,4)==3 && text[0]=='M' && !text[3] && text[4]=='p');
    wide[4]=0x1234;CHECK(LoadStringW(main,1,wide,4)==3 && wide[0]=='M' && !wide[3] && wide[4]==0x1234);
    text[1]='p';CHECK(!LoadStringA(main,1,text,1) && !text[0] && text[1]=='p');
    text[0]='p';CHECK(!LoadStringA(main,1,text,0) && text[0]=='p');CHECK(!LoadStringA(main,1,text,-1) && text[0]=='p');
    CHECK(LoadStringA(main,1,text,0x7fffffff)==13 && equal(text,"MAIN resource"));
    CHECK(LoadStringW(main,1,(LPWSTR)&pointer,0)==13 && pointer[0]=='M' && pointer[12]=='e');
    CHECK(!LoadStringW(main,3,(LPWSTR)&pointer,0) && pointer!=(const WCHAR*)0x12345678);
    CHECK(!LoadStringA(main,3,text,64) && !text[0]);
    pointer=(const WCHAR*)0x12345678;CHECK(!LoadStringW(main,5000,(LPWSTR)&pointer,0) && pointer==(const WCHAR*)0x12345678);
    CHECK(!LoadStringA(main,5000,text,64) && !text[0]);
    CHECK(LoadStringW(main,9,wide,64)==3 && wide[0]=='A' && !wide[1] && wide[2]=='B' && !wide[3]);
    CHECK(LoadStringA(main,9,text,64)==3 && text[0]=='A' && !text[1] && text[2]=='B' && !text[3]);
    CHECK(!LoadStringW(main,1,(LPWSTR)(ULONG_PTR)0xfffffff0,64) && GetLastError()==ERROR_NOACCESS);
    CHECK(!LoadStringA(main,1,(LPSTR)(ULONG_PTR)0xffffffff,64) && GetLastError()==ERROR_NOACCESS);
    dll=LoadLibraryA("resource_strings.dll");CHECK(dll && LoadStringA(dll,1,saved,64)==15 && equal(saved,"MODULE resource"));
    CHECK(LoadStringW(dll,1,(LPWSTR)&pointer,0)==15 && pointer[0]=='M');P[4]=(DWORD)dll;P[5]=(DWORD)pointer;P[6]=(DWORD)saved;Stage(P[0]==2?9:1);
    if(P[0]==2){Sleep(INFINITE);ExitProcess(200);}
    CHECK(FreeLibrary(dll));Stage(2);CHECK(equal(saved,"MODULE resource"));
    CHECK(!LoadStringA(dll,1,text,64) && !text[0]);CHECK(LoadStringA(main,1,text,64)==13 && equal(text,"MAIN resource"));
    P[3]=1;ExitProcess(0);
}
