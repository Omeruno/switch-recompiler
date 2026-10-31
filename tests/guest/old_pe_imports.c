/* Public CRT-free old-linker PE fixture, not game code. */
#include <windows.h>
__declspec(dllimport) DWORD WINAPI OrdinalAnswer(void);
__declspec(dllexport) volatile DWORD OldPeProbe[8];
#define CHECK(x) do{if(!(x))ExitProcess(__LINE__);}while(0)
void WINAPI start(void){
    DWORD n;HANDLE file;const char payload[]="old PE named and ordinal imports\r\n";
    for(n=1;n<8;++n)CHECK(OldPeProbe[n]==0);
    CHECK(GetModuleHandleA(NULL) && GetModuleHandleA("old_pe_api.dll"));
    CHECK(GetProcAddress(GetModuleHandleA("kernel32.dll"),"GetTickCount"));
    CHECK(OrdinalAnswer()==0x5a17);
    CHECK(RegisterWindowMessageA("PublicOldPeMessage")>=0xc000);
    file=CreateFileA("old-pe-public.txt",GENERIC_WRITE,0,NULL,CREATE_ALWAYS,0,NULL);CHECK(file!=INVALID_HANDLE_VALUE);
    CHECK(WriteFile(file,payload,sizeof(payload)-1,&n,NULL) && n==sizeof(payload)-1);
    CHECK(FlushFileBuffers(file) && CloseHandle(file));OldPeProbe[1]=1;
    if(OldPeProbe[0])for(;;)Sleep(0);
    ExitProcess(0);
}
