#define INITFAULT_CONTROL
#include "initfault_shared.h"
static InitFaultState state;
InitFaultState* InitFaultControl(void){return &state;}
DWORD WINAPI InitFaultWorker(LPVOID unused){(void)unused;++state.workerBody;return 19;}
void InitFaultRecord(DWORD id,DWORD reason,LPVOID reserved,DWORD value){
    DWORD* e;if(state.count>=128){state.failure|=1;return;}
    e=state.events[state.count++];e[0]=id;e[1]=reason;e[2]=GetCurrentThreadId();e[3]=(DWORD)reserved;e[4]=value;
}
BOOL WINAPI DllMainCRTStartup(HINSTANCE m,DWORD r,LPVOID p){(void)m;(void)r;(void)p;return TRUE;}
