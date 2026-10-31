/* Authored CRT-free launcher contract: no application payload or assets. */
#include <windows.h>
#include <dplay.h>
__declspec(dllexport) volatile DWORD LauncherProbe[2];
#define CHECK(x) do{if(!(x))ExitProcess(__LINE__);}while(0)
static BOOL CALLBACK provider(LPGUID guid,LPSTR name,DWORD major,DWORD minor,LPVOID context){
    (void)guid;(void)name;(void)major;(void)minor;(void)context;ExitProcess(220);return FALSE;
}
static BOOL CALLBACK providerW(LPGUID guid,LPWSTR name,DWORD major,DWORD minor,LPVOID context){
    (void)guid;(void)name;(void)major;(void)minor;(void)context;ExitProcess(221);return FALSE;
}
void WINAPI start(void){
    char name[32],data[8];WCHAR wide[32];DWORD count=32,got=0;HANDLE file;WIN32_FILE_ATTRIBUTE_DATA info;LPDIRECTPLAY object=(LPDIRECTPLAY)1;
    CHECK(GetEnvironmentVariableA("USERNAME",name,32)==6 && name[0]=='P');
    CHECK(SetEnvironmentVariableA("USERNAME","spoof"));
    CHECK(!GetUserNameA(name,&(DWORD){0}) && GetLastError()==ERROR_INSUFFICIENT_BUFFER);
    CHECK(GetUserNameA(name,&count) && count==7 && name[0]=='P' && name[5]=='r' && !name[6]);
    count=32;CHECK(GetUserNameW(wide,&count) && count==7 && wide[0]=='P' && wide[5]=='r' && !wide[6]);
    CHECK(GetFileAttributesA("")==INVALID_FILE_ATTRIBUTES && GetLastError()==ERROR_PATH_NOT_FOUND);
    CHECK(GetFileAttributesW(L"")==INVALID_FILE_ATTRIBUTES && GetLastError()==ERROR_PATH_NOT_FOUND);
    CHECK(!GetFileAttributesExA("",GetFileExInfoStandard,&info) && GetLastError()==ERROR_PATH_NOT_FOUND);
    CHECK(!GetFileAttributesExW(L"",GetFileExInfoStandard,&info) && GetLastError()==ERROR_PATH_NOT_FOUND);
    CHECK(GetFileAttributesA(".") & FILE_ATTRIBUTE_DIRECTORY);
    CHECK(GetFileAttributesW(L".") & FILE_ATTRIBUTE_DIRECTORY);
    CHECK(GetFileAttributesA("missing.dat")==INVALID_FILE_ATTRIBUTES && GetLastError()==ERROR_FILE_NOT_FOUND);
    CHECK(GetFileAttributesA("file.dat\\child")==INVALID_FILE_ATTRIBUTES && GetLastError()==ERROR_PATH_NOT_FOUND);
    CHECK(GetFileAttributesA("caf\xe9.dat")!=INVALID_FILE_ATTRIBUTES);
    CHECK(GetFileAttributesW(L"caf\xe9.dat")!=INVALID_FILE_ATTRIBUTES);
    CHECK(GetFileAttributesExA("file.dat",GetFileExInfoStandard,&info) && info.nFileSizeLow==8 && info.nFileSizeHigh==0);
    CHECK(GetFileAttributesExW(L"file.dat",GetFileExInfoStandard,&info) && info.nFileSizeLow==8);
    /* The empty first candidate must fail, allowing the real file candidate. */
    CHECK(GetFileAttributesA("")==INVALID_FILE_ATTRIBUTES);
    file=CreateFileA("file.dat",GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);CHECK(file!=INVALID_HANDLE_VALUE);
    CHECK(ReadFile(file,data,8,&got,NULL) && got==8 && data[0]=='p' && data[7]=='!');CHECK(CloseHandle(file));
    CHECK(DirectPlayEnumerateA(provider,NULL)==DPERR_UNSUPPORTED);
    CHECK(DirectPlayEnumerateW(providerW,NULL)==DPERR_UNSUPPORTED);
    CHECK(DirectPlayCreate(NULL,&object,NULL)==DPERR_UNSUPPORTED && object==NULL);
    LauncherProbe[1]=1;
    if(LauncherProbe[0]){Sleep(INFINITE);ExitProcess(222);}
    ExitProcess(0);
}
