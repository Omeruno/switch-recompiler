#include <windows.h>
static DWORD initialized;
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID reserved){
    (void)module;(void)reserved;
    if(reason==DLL_PROCESS_ATTACH){if(initialized)return FALSE;initialized=1;}
    return TRUE;
}
DWORD WINAPI OrdinalAnswer(void){return initialized?0x5a17:0;}
