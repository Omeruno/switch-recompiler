#include <windows.h>
#ifndef UNLOAD_ID
#define UNLOAD_ID 1
#endif
static DWORD* logbuf;
static DWORD tls_index, seed=0x12345678;
static DWORD* tls(void) { DWORD* p; __asm__ volatile("movl %%fs:0x2c,%0":"=r"(p)); return (DWORD*)p[tls_index]; }
static void record(DWORD kind,LPVOID reserved) {
    if (!logbuf) return;
    if (reserved || tls()[0]!=0x12345678) logbuf[1]|=1;
    logbuf[2+logbuf[0]++]=UNLOAD_ID*10+kind;
}
#if UNLOAD_ID != 1
__declspec(dllimport) void UnloadDependencyProbe(void);
#endif
#if UNLOAD_ID == 1
__declspec(dllexport) void UnloadDependencyProbe(void) { if (logbuf && tls()[0]!=0x12345678) logbuf[1]|=2; }
#endif
__declspec(dllexport) void ConfigureUnload(DWORD* buffer) { logbuf=buffer; }
__declspec(dllexport) DWORD* UnloadTls(void) { return tls(); }
__declspec(dllexport) DWORD WINAPI UnloadSelf(LPVOID module) {
    FreeLibraryAndExitThread((HMODULE)module,77);
    return 999; // must never execute from this unmapped image
}
static void NTAPI callback(PVOID m,DWORD reason,PVOID reserved) { (void)m; if(reason==DLL_PROCESS_DETACH) record(1,reserved); }
static PIMAGE_TLS_CALLBACK callbacks[]={callback,0};
const IMAGE_TLS_DIRECTORY32 _tls_used __attribute__((used,section(".rdata$T")))={
 (DWORD)&seed,(DWORD)(&seed+1),(DWORD)&tls_index,(DWORD)callbacks,4,0};
BOOL WINAPI DllMainCRTStartup(HINSTANCE m,DWORD reason,LPVOID reserved) {
    (void)m;
    if(reason==DLL_PROCESS_DETACH) {
        record(2,reserved);
#if UNLOAD_ID != 1
        UnloadDependencyProbe();
#endif
#if UNLOAD_ID == 2
        if(logbuf) logbuf[24]=1;
#endif
        Sleep(1);
#if UNLOAD_ID == 2
        if(logbuf && logbuf[25] && logbuf[26]) logbuf[1]|=4;
#endif
    }
    return TRUE;
}
