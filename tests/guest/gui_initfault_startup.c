#include "initfault_shared.h"
#include "initfault_trigger.h"
__declspec(dllimport) DWORD InitFaultAnchor(void);
static void NTAPI callback(PVOID m,DWORD reason,PVOID reserved){
    InitFaultState* s=InitFaultControl();(void)m;
    InitFaultRecord(31,reason,reserved,0);
    if(reason==DLL_PROCESS_ATTACH && s->phase==2 && s->mode)init_fault();
}
static PIMAGE_TLS_CALLBACK callbacks[]={callback,0};
const IMAGE_TLS_DIRECTORY32 _tls_used __attribute__((used,section(".rdata$T")))={0,0,0,(DWORD)callbacks,0,0};
DWORD WINAPI start(void){InitFaultState* s=InitFaultControl();++s->entry;return InitFaultAnchor()!=(GetCurrentThreadId()^1);}
