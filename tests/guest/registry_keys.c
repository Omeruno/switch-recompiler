/* Public CRT-free registry semantics, no application payload or API-name hacks. */
#include <windows.h>
__declspec(dllexport) volatile DWORD RegistryProbe[4];
#define CHECK(x) do {if(!(x))ExitProcess(__LINE__);}while(0)
void WINAPI start(void){
    HKEY key=0,read=0,parent=0,bad=(HKEY)(ULONG_PTR)0x12345678;DWORD disposition=0,type=0,size=0,data=0;
    const DWORD mode=RegistryProbe[0],value=0x12345678u+(mode&2);
    const char* path="Software\\SwitchRecompiler\\PublicRegistry";
    if(RegistryProbe[1])CHECK(RegCloseKey((HKEY)(ULONG_PTR)RegistryProbe[1])==ERROR_INVALID_HANDLE);
    SetLastError(0x76543210);
    CHECK(RegCreateKeyExA(HKEY_CURRENT_USER,path,0,NULL,0,KEY_ALL_ACCESS,NULL,&key,&disposition)==0);
    CHECK(disposition==(mode?REG_OPENED_EXISTING_KEY:REG_CREATED_NEW_KEY));
    CHECK(GetLastError()==0x76543210);RegistryProbe[2]=(DWORD)(ULONG_PTR)key;
    CHECK(RegOpenKeyExW(HKEY_CURRENT_USER,L"software\\SWITCHRECOMPILER",0,KEY_READ,&parent)==0);CHECK(RegCloseKey(parent)==0);
    if(!(mode&1)){
        CHECK(RegSetValueExA(key,"Number",0,REG_DWORD,(const BYTE*)&value,4)==0);
        CHECK(RegSetValueExA(key,"Text",0,REG_SZ,(const BYTE*)"hello",6)==0);
        CHECK(RegSetValueExW(key,NULL,0,REG_DWORD,(const BYTE*)&value,4)==0);
    }
    size=4;CHECK(RegQueryValueExW(key,L"nUmBeR",NULL,&type,(BYTE*)&data,&size)==0 && type==REG_DWORD && data==value && size==4);
    size=4;data=0;CHECK(RegQueryValueExA(key,"",NULL,&type,(BYTE*)&data,&size)==0 && data==value);
    {WCHAR wide[8]={0};char ansi[8]={0};size=sizeof(wide);CHECK(RegQueryValueExW(key,L"TEXT",NULL,&type,(BYTE*)wide,&size)==0 && size==12 && type==REG_SZ && wide[0]=='h' && wide[4]=='o' && wide[5]==0);
     size=sizeof(ansi);CHECK(RegQueryValueExA(key,"text",NULL,&type,(BYTE*)ansi,&size)==0 && size==6 && ansi[0]=='h' && ansi[4]=='o' && ansi[5]==0);}
    size=1;data=0xdeadbeef;CHECK(RegQueryValueExA(key,"Number",NULL,&type,(BYTE*)&data,&size)==ERROR_MORE_DATA && size==4 && data==0xdeadbeef);
    size=0;CHECK(RegQueryValueExA(key,"Number",NULL,&type,NULL,&size)==0 && size==4);
    CHECK(RegOpenKeyExA(key,NULL,0,KEY_READ,&read)==0);
    CHECK(RegSetValueExA(read,"Number",0,REG_DWORD,(const BYTE*)&data,4)==ERROR_ACCESS_DENIED);CHECK(RegCloseKey(read)==0);
    CHECK(RegCloseKey(read)==ERROR_INVALID_HANDLE);
    CHECK(RegOpenKeyExA(key,NULL,0,KEY_SET_VALUE,&read)==0);
    size=4;CHECK(RegQueryValueExA(read,"Number",NULL,&type,(BYTE*)&data,&size)==ERROR_ACCESS_DENIED);CHECK(RegCloseKey(read)==0);
    CHECK(RegCreateKeyExA(bad,"Child",0,NULL,0,KEY_ALL_ACCESS,NULL,&read,NULL)==ERROR_INVALID_HANDLE);
    CHECK(RegCreateKeyExA(key,"NoOutput",0,NULL,0,KEY_ALL_ACCESS,NULL,NULL,NULL)==ERROR_INVALID_PARAMETER);
    CHECK(RegCreateKeyExA(key,"NoPointer",0,NULL,0,KEY_ALL_ACCESS,NULL,(PHKEY)(ULONG_PTR)0xfffffff0,NULL)==ERROR_NOACCESS);
    CHECK(RegOpenKeyExA(key,"NoPointer",0,KEY_READ,&read)==ERROR_FILE_NOT_FOUND);
    CHECK(RegCreateKeyExA(key,"Reserved",1,NULL,0,KEY_ALL_ACCESS,NULL,&read,NULL)==ERROR_INVALID_PARAMETER);
    CHECK(RegCreateKeyExA(key,"Volatile",0,NULL,REG_OPTION_VOLATILE,KEY_ALL_ACCESS,NULL,&read,NULL)==ERROR_NOT_SUPPORTED);
    CHECK(RegCreateKeyExA(key,"Class",0,"unsupported",0,KEY_ALL_ACCESS,NULL,&read,NULL)==ERROR_NOT_SUPPORTED);
    CHECK(RegOpenKeyExA(key,NULL,0,KEY_WOW64_64KEY|KEY_READ,&read)==ERROR_NOT_SUPPORTED);
    {char longName[257];unsigned n;for(n=0;n<256;++n)longName[n]='x';longName[256]=0;
     CHECK(RegCreateKeyExA(key,longName,0,NULL,0,KEY_ALL_ACCESS,NULL,&read,NULL)==ERROR_FILENAME_EXCED_RANGE);}
    CHECK(RegSetValueExA(key,"BadPointer",0,REG_BINARY,(const BYTE*)(ULONG_PTR)0xfffffff0,4)==ERROR_NOACCESS);
    CHECK(RegSetValueExA(key,"Huge",0,REG_BINARY,(const BYTE*)&data,0xffffffff)==ERROR_NOT_ENOUGH_MEMORY);
    CHECK(RegSetValueExA(key,"BadDword",0,REG_DWORD,(const BYTE*)&data,3)==ERROR_INVALID_DATA);
    CHECK(RegSetValueExA(key,"UnknownType",0,0x1234,(const BYTE*)&data,4)==ERROR_NOT_SUPPORTED);
    {const WCHAR nonAscii[]={0xe9,0};CHECK(RegCreateKeyExW(key,nonAscii,0,NULL,0,KEY_READ,NULL,&read,NULL)==ERROR_NO_UNICODE_TRANSLATION);
     CHECK(RegSetValueExW(key,L"UnicodeData",0,REG_SZ,(const BYTE*)nonAscii,sizeof(nonAscii))==0);
     size=4;data=0xdeadbeef;CHECK(RegQueryValueExA(key,"UnicodeData",NULL,&type,(BYTE*)&data,&size)==ERROR_NO_UNICODE_TRANSLATION && data==0xdeadbeef);}
    CHECK(RegEnumKeyW(key,0,NULL,0)==ERROR_CALL_NOT_IMPLEMENTED);
    CHECK(RegDeleteKeyW(key,L"Anything")==ERROR_CALL_NOT_IMPLEMENTED);
    CHECK(RegOpenKeyExA(HKEY_CURRENT_USER,NULL,0,KEY_READ,&parent)==0 && parent==HKEY_CURRENT_USER);
    CHECK(RegOpenKeyW(key,L"",&parent)==0 && parent==key);
    CHECK(RegCloseKey(HKEY_CURRENT_USER)==0);
    CHECK(RegCloseKey(key)==0);
    CHECK(RegOpenKeyExA(key,NULL,0,KEY_READ,&read)==ERROR_INVALID_HANDLE);
    CHECK(RegOpenKeyA(HKEY_CURRENT_USER,path,&read)==0);CHECK(RegCloseKey(read)==0);
    RegistryProbe[3]=1;ExitProcess(0);
}
