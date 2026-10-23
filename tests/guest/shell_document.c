/* Public x86 fixture: verifies actual command/argv delivery and opens the
 * Unicode document passed by the native shell. No proprietary application. */
#include <windows.h>
#include <shellapi.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
__declspec(dllexport) volatile unsigned DocumentProbe;
__declspec(dllimport) int __cdecl __wgetmainargs(int*,wchar_t***,wchar_t***,int,void*);
#define CHECK(x) do { if(!(x)){printf("shell_document FAIL line %d\n",__LINE__);return 1;} } while(0)
int main(int argc,char** argv) {
    int count=0,wideCount=0;
    LPWSTR* words=CommandLineToArgvW(GetCommandLineW(),&count);
    wchar_t **wide=0,**env=0;int startup=0;
    CHECK(words && count==argc && (argc==2 || argc==7));
    CHECK(__wgetmainargs(&wideCount,&wide,&env,0,&startup)==0 && wideCount==argc);
    for(int i=0;i<argc;++i)CHECK(wcscmp(words[i],wide[i])==0);
    CHECK(words[argc]==0 && wide[argc]==0 && argv[argc]==0);
    wchar_t module[512];CHECK(GetModuleFileNameW(0,module,512)>0 && wcscmp(words[0],module)==0);
    CHECK(strlen(GetCommandLineA())>63 && wcslen(GetCommandLineW())>63);
    if(argc==7) {
        CHECK(strcmp(argv[1],"verify")==0 && strcmp(argv[2],"space value")==0);
        CHECK(strcmp(argv[3],"")==0 && strcmp(argv[4],"quote\"value")==0 && strcmp(argv[5],"slash\\")==0);
        CHECK(wcscmp(words[2],L"space value")==0 && wcscmp(words[4],L"quote\"value")==0);
    }
    CHECK(strstr(argv[argc-1],"文档")!=0);
    HANDLE file=CreateFileW(words[argc-1],GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,0,0);
    CHECK(file!=INVALID_HANDLE_VALUE);
    char data[64]={0};DWORD read=0;
    CHECK(ReadFile(file,data,63,&read,0) && read==23 && memcmp(data,"public document payload",23)==0);
    CHECK(CloseHandle(file));CHECK(LocalFree(words)==0);
    DocumentProbe=1;puts("shell_document: actual Unicode document and A/W/CRT argv PASS");return 0;
}
