#include "rollback_shared.h"
#ifndef ROLLBACK_ID
#define ROLLBACK_ID 1
#endif
static DWORD tlsIndex,seed=0x76543210;
static DWORD* tls(void){ DWORD* p; __asm__ volatile("movl %%fs:0x2c,%0":"=r"(p));return (DWORD*)p[tlsIndex]; }
static void NTAPI callback(PVOID m,DWORD reason,PVOID reserved) {
    (void)m;
    RollbackRecord(ROLLBACK_ID,1,reason,tls()[0],reserved);
    if(reason==DLL_PROCESS_ATTACH || reason==DLL_THREAD_ATTACH) {
        if(tls()[0]!=seed || tls()[1]) RollbackControl()->failure|=4;
        tls()[0]=GetCurrentThreadId()^ROLLBACK_ID;
    }
}
static PIMAGE_TLS_CALLBACK callbacks[]={callback,0};
const IMAGE_TLS_DIRECTORY32 _tls_used __attribute__((used,section(".rdata$T")))={
    (DWORD)&seed,(DWORD)(&seed+1),(DWORD)&tlsIndex,(DWORD)callbacks,
#ifdef ROLLBACK_TLS_OVERFLOW
    0x1000001,
#else
    4,
#endif
    0};
__declspec(dllexport) DWORD RollbackValue(void){return tls()[0];}
#if ROLLBACK_ID==1
__declspec(dllexport) DWORD RollbackDepValue(void){return tls()[0];}
#endif
#if ROLLBACK_ID==2
__declspec(dllexport) DWORD RollbackCycleAnchor(void){return tls()[0];}
#endif
#if ROLLBACK_ID==8
__declspec(dllimport) DWORD RollbackCycleAnchor(void);
#endif
#if ROLLBACK_ID==3
__declspec(dllexport) DWORD RollbackBadValue(void){return tls()[0];}
#endif
#if ROLLBACK_ID==2 || ROLLBACK_ID==6
__declspec(dllimport) DWORD RollbackDepValue(void);
#endif
#if ROLLBACK_ID==4
__declspec(dllimport) DWORD RollbackBadValue(void);
#endif
BOOL WINAPI DllMainCRTStartup(HINSTANCE m,DWORD reason,LPVOID reserved) {
    RollbackState* s=RollbackControl();(void)m;
    RollbackRecord(ROLLBACK_ID,2,reason,tls()[0],reserved);
    if(reason==DLL_PROCESS_DETACH){Sleep(1);return TRUE;}
    if(reason!=DLL_PROCESS_ATTACH)return TRUE;
#if ROLLBACK_ID==2 || ROLLBACK_ID==6
    if(RollbackDepValue()!=(GetCurrentThreadId()^1))s->failure|=8;
#endif
#if ROLLBACK_ID==4
    if(RollbackBadValue()!=(GetCurrentThreadId()^3))s->failure|=16;
#endif
#if ROLLBACK_ID==8
    if(RollbackCycleAnchor()!=(GetCurrentThreadId()^2))s->failure|=64;
#endif
#if ROLLBACK_ID==2
    if(s->keepNested){
        s->keeper=LoadLibraryA(s->keepNested==2?"rollback_cycle.dll":"rollback_keeper.dll");
        if(!s->keeper)s->failure|=32;
    }
    Sleep(2);
    if(s->failTop)return FALSE;
#endif
#if ROLLBACK_ID==3
    if(s->failDep)return FALSE;
#endif
    return TRUE;
}
