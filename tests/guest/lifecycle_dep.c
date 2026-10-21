#define LIFE_STATE_IMPL
#define LIFE_ID 1
#include "lifecycle_shared.h"
static LifeState state;
LifeState* life_state(void) { return &state; }
void life_record(DWORD module,DWORD kind,DWORD reason,DWORD value,DWORD reserved) {
    LifeEvent* event;
    DWORD tid=GetCurrentThreadId();
    if (reason==DLL_THREAD_DETACH) {
        DWORD code=0;
        if (!GetExitCodeThread(GetCurrentThread(),&code) || code!=STILL_ACTIVE ||
            WaitForSingleObject(GetCurrentThread(),0)!=WAIT_TIMEOUT) state.failure|=32;
    }
    if (state.busyTid && state.busyTid!=tid) state.failure|=2;
    if (state.count>=256) { state.failure|=4; return; }
    event=&state.events[state.count++];
    event->module=module;event->kind=kind;event->reason=reason;
    event->tid=tid;event->value=value;event->reserved=reserved;
}
#include "lifecycle_tls.h"
BOOL WINAPI DllMainCRTStartup(HINSTANCE module,DWORD reason,LPVOID reserved) {
    (void)module;
    life_record(1,2,reason,life_tls()[0],(DWORD)reserved);
    return TRUE;
}
