/* Public CRT-free VFS-backed profile API contract. No private payload. */
#include <windows.h>
__declspec(dllexport) volatile DWORD ProfileProbe[2];
#define CHECK(x) do{if(!(x))ExitProcess(__LINE__);}while(0)
static int equal(const char* a,const char* b){while(*a && *a==*b){++a;++b;}return *a==*b;}
void WINAPI start(void){
    const char* path="C:\\settings.ini";char b[128];WCHAR w[32];DWORD mode=ProfileProbe[0];
    CHECK(GetPrivateProfileIntA("Game","Missing",123,path)==123);
    CHECK(GetPrivateProfileStringA("none","none"," default  ",b,128,"C:\\missing.ini")==8 && equal(b," default"));
    CHECK(GetPrivateProfileStringW(L"GAME",L"Text",L"bad",w,32,L"C:\\SETTINGS.INI")==5 && w[0]=='h' && w[4]=='o' && !w[5]);
    if(!(mode&1)){
        CHECK(WritePrivateProfileStringA("Game","Score",mode?"73":"0x2a",path));
        CHECK(WritePrivateProfileStringW(L"Game",L"Negative",L"-1",L"C:\\settings.ini"));
        CHECK(WritePrivateProfileStringA("Game","Trailing","42tail",path));
        CHECK(WritePrivateProfileStringA("Delete","Key","old",path));
        CHECK(WritePrivateProfileStringW(L"Delete",L"Key",NULL,L"C:\\settings.ini"));
        CHECK(GetPrivateProfileIntW(L"Delete",L"Key",321,L"C:\\settings.ini")==321);
        CHECK(WritePrivateProfileStringA("Delete",NULL,NULL,path));
        CHECK(WriteProfileStringA("Public","Number",mode?"73":"42"));
        CHECK(WriteProfileStringW(L"Public",L"Text",L"saved"));
        CHECK(WritePrivateProfileStringA("Bare","Value","real","bare.ini"));
        CHECK(WritePrivateProfileStringA(NULL,NULL,NULL,path));
    }
    CHECK(GetPrivateProfileIntA("game","score",999,path)==(mode>=2?73:42));
    CHECK(GetPrivateProfileIntW(L"GAME",L"NEGATIVE",1,L"C:\\settings.ini")==0xffffffffu);
    CHECK(GetPrivateProfileIntA("Game","Trailing",99,path)==42);
    CHECK(GetProfileIntA("Public","Number",999)==(mode>=2?73:42));
    CHECK(GetProfileIntW(L"Public",L"Number",999)==(mode>=2?73:42));
    CHECK(GetProfileStringA("Public","Text","bad",b,128)==5 && equal(b,"saved"));
    CHECK(GetProfileStringW(L"Public",L"Text",L"bad",w,32)==5 && w[0]=='s' && w[4]=='d' && !w[5]);
    CHECK(GetPrivateProfileIntA("Bare","Value",99,"bare.ini")==0);
    CHECK(GetPrivateProfileSectionNamesA(b,128,path)==11 && equal(b,"Game") && equal(b+5,"Other") && !b[11]);
    CHECK(GetPrivateProfileStringA(NULL,NULL,NULL,b,128,path)==11 && equal(b,"Game") && equal(b+5,"Other"));
    CHECK(GetPrivateProfileSectionNamesW(w,32,L"C:\\settings.ini")==11 && w[5]=='O' && !w[11]);
    CHECK(GetPrivateProfileSectionA("Other",b,128,path)==9 && equal(b,"Keep=yes") && !b[9]);
    CHECK(GetPrivateProfileSectionW(L"Other",w,32,L"C:\\settings.ini")==9 && w[0]=='K' && !w[9]);
    CHECK(GetPrivateProfileStringA("Other",NULL,NULL,b,128,path)==5 && equal(b,"Keep") && !b[5]);
    b[4]='p';CHECK(GetPrivateProfileSectionNamesA(b,4,path)==2 && b[0]=='G' && b[1]=='a' && !b[2] && !b[3] && b[4]=='p');
    b[3]='p';CHECK(GetPrivateProfileStringA("Other","Keep","bad",b,3,path)==2 && b[0]=='y' && b[1]=='e' && !b[2] && b[3]=='p');
    b[0]='p';CHECK(!GetPrivateProfileStringA("Other","Keep",NULL,b,0,path) && b[0]=='p');
    CHECK(!GetPrivateProfileStringA("Other","Keep",NULL,(LPSTR)(ULONG_PTR)0xffffffff,2,path) && GetLastError()==ERROR_NOACCESS);
    CHECK(!GetPrivateProfileStringW(L"Other",L"Keep",NULL,(LPWSTR)(ULONG_PTR)0xfffffff0,32,L"C:\\settings.ini") && GetLastError()==ERROR_NOACCESS);
    CHECK(!GetPrivateProfileStringA((LPCSTR)(ULONG_PTR)0xffffffff,"Keep",NULL,b,128,path) && GetLastError()==ERROR_NOACCESS);
    CHECK(!WritePrivateProfileStringA("Bad\nSection","Key","bad",path) && GetLastError()==ERROR_INVALID_PARAMETER);
    CHECK(!WritePrivateProfileStringA("Other","Keep","bad","R:\\settings.ini") && GetLastError()==ERROR_WRITE_PROTECT);
    CHECK(GetPrivateProfileIntA("Other","Keep",99,path)==0);
    CHECK(GetPrivateProfileStringW(L"Unicode",L"Text",L"bad",w,32,L"C:\\unicode.ini")==1 && w[0]==0x416 && !w[1]);
    CHECK(!GetPrivateProfileStringA("Unicode","Text","bad",b,128,"C:\\unicode.ini") && GetLastError()==ERROR_NO_UNICODE_TRANSLATION);
    CHECK(WritePrivateProfileStringW(L"Unicode",L"Text",L"\x416",L"C:\\unicode.ini"));
    CHECK(!WritePrivateProfileStringA("Other","Keep","\xe9",path) && GetLastError()==ERROR_NO_UNICODE_TRANSLATION);
    CHECK(!WritePrivateProfileStringW(L"Other",L"Keep",L"\x416",L"C:\\settings.ini") && GetLastError()==ERROR_NO_UNICODE_TRANSLATION);
    CHECK(!WritePrivateProfileStringA("Other","Keep","bad",(LPCSTR)(ULONG_PTR)0xfffffff0) && GetLastError()==ERROR_NOACCESS);
    CHECK(GetPrivateProfileStringA("Other","Keep","bad",b,128,path)==3 && equal(b,"yes"));
    ProfileProbe[1]=1;ExitProcess(0);
}
