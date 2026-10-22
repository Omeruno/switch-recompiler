#define LIFE_STATE_IMPL
#define LIFE_ID 1
#include "lifecycle_shared.h"
__declspec(dllexport) LifeState state;
LifeState* life_state(void) { return &state; }
void life_record(DWORD module,DWORD kind,DWORD reason,DWORD value,DWORD reserved) {
    LifeEvent* event;
    DWORD tid=GetCurrentThreadId();
    if (reason==DLL_PROCESS_DETACH && state.exitTest) {
        DWORD code;
        HANDLE peers[]={state.exitBlocked,state.exitSuspended,state.exitFinished};
        unsigned i;
        if (tid!=state.exitOwner || !reserved) state.failure|=128;
        if (!GetExitCodeThread(GetCurrentThread(),&code) || code!=STILL_ACTIVE ||
            WaitForSingleObject(GetCurrentThread(),0)!=WAIT_TIMEOUT) state.failure|=256;
        for(i=0;i<3;++i)
            if(WaitForSingleObject(peers[i],0)!=WAIT_OBJECT_0 ||
               !GetExitCodeThread(peers[i],&code) || code!=(i==2?73:0)) state.failure|=512;
        if (!GetModuleHandleA("lifecycle_dep.dll") ||
            !GetProcAddress(GetModuleHandleA("lifecycle_dep.dll"),"life_state")) state.failure|=1024;
        if (!LoadLibraryA("lifecycle_top.dll")) state.failure|=65536;
        if (module==3 && kind==2) {
            // Runtime policy: existing loader queries are allowed, while
            // new module/thread creation cannot extend the shutdown set.
            if (LoadLibraryA("lifecycle_late.dll") ||
                CreateThread(NULL,0,(LPTHREAD_START_ROUTINE)life_state,NULL,CREATE_SUSPENDED,NULL))
                state.failure|=131072;
        }
    }
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
    if(reason==DLL_PROCESS_DETACH && state.exitTest) Sleep(2);
    return TRUE;
}
