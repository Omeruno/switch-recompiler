#include <windows.h>
#include "native_dispatch_kernel.h"
static DWORD checksum;
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID reserved){
    (void)module;(void)reserved;if(reason==DLL_PROCESS_ATTACH){DWORD b;checksum=Kernel(512,&b)^b;}return TRUE;
}
__declspec(dllexport) DWORD WINAPI NativeDllChecksum(void){return checksum;}
