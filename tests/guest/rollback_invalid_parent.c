#include <windows.h>
__declspec(dllimport) DWORD InvalidChildValue(void);
__declspec(dllexport) DWORD InvalidParentValue(void){return InvalidChildValue();}
BOOL WINAPI DllMainCRTStartup(HINSTANCE m,DWORD r,LPVOID p){(void)m;(void)r;(void)p;return TRUE;}
