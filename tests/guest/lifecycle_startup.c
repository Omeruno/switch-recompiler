#define _WIN32_WINNT 0x0501
#define LIFE_ID 8
#include "lifecycle_shared.h"
#include "lifecycle_tls.h"
__declspec(dllexport) DWORD StartupAnchor(void) { return 8; }
static DWORD WINAPI Worker(LPVOID data) {
    LifeState* state=life_state();DWORD i,exeAttach=0;(void)data;
    for(i=0;i<state->count;++i)
        if(state->events[i].module==9 && state->events[i].reason==DLL_PROCESS_ATTACH)++exeAttach;
    if(exeAttach!=1)state->failure|=0x100000;
    ++state->startupWorker;SetEvent(state->startupReady);return 17;
}
static DWORD WINAPI NeverStart(LPVOID data) { (void)data;life_state()->failure|=0x200000;return 18; }
BOOL WINAPI DllMainCRTStartup(HINSTANCE module,DWORD reason,LPVOID reserved) {
    LifeState* state=life_state();LARGE_INTEGER due;(void)module;
    life_record(8,2,reason,life_tls()[0],(DWORD)reserved);
    if(reason==DLL_PROCESS_DETACH) {
        if(state->startupMode==5) { ExitProcess(39);++state->startupAfterExit; }
        Sleep(1);return TRUE;
    }
    if(reason!=DLL_PROCESS_ATTACH)return TRUE;
    state->startupReady=CreateEventA(NULL,TRUE,FALSE,NULL);
    state->startupTimer=CreateWaitableTimerA(NULL,FALSE,NULL);
    state->startupMutex=CreateMutexA(NULL,TRUE,NULL);
    if(!state->startupReady || !state->startupTimer || !state->startupMutex)state->failure|=0x400000;
    due.QuadPart=-600000000;
    if(!SetWaitableTimer(state->startupTimer,&due,1000,NULL,NULL,FALSE))state->failure|=0x800000;
    state->startupSuspended=CreateThread(NULL,0,NeverStart,NULL,CREATE_SUSPENDED,NULL);
    state->startupThread=CreateThread(NULL,0,Worker,NULL,0,NULL);
    if(!state->startupSuspended || !state->startupThread)state->failure|=0x1000000;
    state->startupWait=0x7fffffff;
    state->startupWait=WaitForSingleObject(state->startupReady,state->startupMode==4?5000:2);
    if(state->startupWait!=WAIT_TIMEOUT || state->startupWorker)state->failure|=0x2000000;
    if(state->startupMode==1 || state->startupMode==5)return FALSE;
    if(state->startupMode==2) { ExitProcess(37);++state->startupAfterExit; }
    if(state->startupMode==3) { ExitThread(38);++state->startupAfterExit; }
    return TRUE;
}
