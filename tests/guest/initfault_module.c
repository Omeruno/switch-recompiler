#include "initfault_shared.h"
#include "initfault_trigger.h"
#ifndef INITFAULT_ID
#define INITFAULT_ID 1
#endif
static DWORD tlsIndex,seed=0x12345678;
static DWORD* tls(void){DWORD* p;__asm__ volatile("movl %%fs:0x2c,%0":"=r"(p));return(DWORD*)p[tlsIndex];}
static void NTAPI callback(PVOID m,DWORD reason,PVOID reserved){
    InitFaultState* s=InitFaultControl();(void)m;
    InitFaultRecord(INITFAULT_ID*10+1,reason,reserved,tls()[0]);
    if(reason==DLL_PROCESS_ATTACH){
        if(tls()[0]!=seed || tls()[1])s->failure|=2;
        tls()[0]=GetCurrentThreadId()^INITFAULT_ID;
        if(s->phase==1 && s->mode)init_fault();
    }
}
static PIMAGE_TLS_CALLBACK callbacks[]={callback,0};
const IMAGE_TLS_DIRECTORY32 _tls_used __attribute__((used,section(".rdata$T")))={
    (DWORD)&seed,(DWORD)(&seed+1),(DWORD)&tlsIndex,(DWORD)callbacks,4,0};
__declspec(dllexport) DWORD InitFaultAnchor(void){return tls()[0];}
BOOL WINAPI DllMainCRTStartup(HINSTANCE m,DWORD reason,LPVOID reserved){
    InitFaultState* s=InitFaultControl();(void)m;
    InitFaultRecord(INITFAULT_ID*10+2,reason,reserved,tls()[0]);
    if(reason==DLL_PROCESS_ATTACH && s->phase==0){
        if(s->mode==11){
#if INITFAULT_ID==1
            if(LoadLibraryA("initfault_child.dll") || GetLastError()!=ERROR_DLL_INIT_FAILED)s->failure|=4;
            ++s->afterNested;
#else
            RaiseException(INITFAULT_CODE,0,0,NULL);++s->afterFault;
#endif
        } else if(s->mode==10)return FALSE;
        else if(s->mode){
            if(s->mode>=3 && s->mode!=14){
                s->worker=CreateThread(NULL,0,InitFaultWorker,NULL,0,NULL);
                s->suspended=CreateThread(NULL,0,InitFaultWorker,NULL,CREATE_SUSPENDED,NULL);
                if(!s->worker || !s->suspended)s->failure|=8;
                Sleep(1); // children must still be loader-serialized on startup
            }
            init_fault();
        }
    }
    if(reason==DLL_PROCESS_DETACH && s->mode==10 && s->phase==0){RaiseException(INITFAULT_CODE,0,0,NULL);++s->afterFault;}
    return TRUE;
}
