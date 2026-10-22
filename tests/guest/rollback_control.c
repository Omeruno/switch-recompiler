#define ROLLBACK_CONTROL
#include "rollback_shared.h"
static RollbackState state;
RollbackState* RollbackControl(void) { return &state; }
void RollbackRecord(DWORD id,DWORD kind,DWORD reason,DWORD value,LPVOID reserved) {
    DWORD* event;
    if(!state.testing)return;
    if(reserved || (kind==2 && value!=(GetCurrentThreadId()^id)) ||
       (kind==1 && reason!=DLL_PROCESS_ATTACH && reason!=DLL_THREAD_ATTACH && value!=(GetCurrentThreadId()^id))) state.failure|=1;
    if(state.count>=256){state.failure|=2;return;}
    event=state.events[state.count++];
    event[0]=id;event[1]=kind;event[2]=reason;event[3]=GetCurrentThreadId();event[4]=(DWORD)reserved;
}
BOOL WINAPI DllMainCRTStartup(HINSTANCE m,DWORD reason,LPVOID reserved) { (void)m;(void)reason;(void)reserved;return TRUE; }
