#include <windows.h>
__declspec(dllexport) DWORD InvalidChildValue(void){return 42;}
BOOL WINAPI DllMainCRTStartup(HINSTANCE m,DWORD r,LPVOID p){(void)m;(void)r;(void)p;return TRUE;}
