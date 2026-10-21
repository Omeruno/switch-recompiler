#include "lifecycle_shared.h"
#include <stdio.h>
typedef DWORD* (*TlsAddress)(void);
__declspec(dllimport) DWORD LifeTopAnchor(void);
static HANDLE go, initial;
static volatile LONG ready, loaded;
static DWORD workerTid[3], workerValue[2];
static DWORD* workerTls[2];
static HMODULE late[2];
static LRESULT CALLBACK WindowProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    if (msg==WM_APP+1) ExitThread(32);
    return DefWindowProcA(hwnd,msg,wp,lp);
}
static void NTAPI ExeTls(PVOID module,DWORD reason,PVOID reserved) {
    (void)module;
    life_record(5,1,reason,0,(DWORD)reserved);
}
PIMAGE_TLS_CALLBACK exe_callback __attribute__((used,section(".CRT$XLB")))=ExeTls;

static DWORD WINAPI Worker(LPVOID arg) {
    DWORD index=(DWORD)arg;
    DWORD* depTls;
    TlsAddress address=(TlsAddress)GetProcAddress(GetModuleHandleA("lifecycle_dep.dll"),"LifeTlsAddress");
    workerTid[index]=GetCurrentThreadId();
    depTls=address();
    if (depTls[0]!=(workerTid[index]^1) || depTls[1]!=0xfeed) return 10;
    if (index==2) {
        address=(TlsAddress)GetProcAddress(GetModuleHandleA("lifecycle_late.dll"),"LifeTlsAddress");
        if (!address || address()[0]!=(workerTid[index]^4)) return 11;
        return 33;
    }
    InterlockedIncrement(&ready);
    if (WaitForSingleObject(go,INFINITE)!=WAIT_OBJECT_0) return 12;
    late[index]=LoadLibraryA("lifecycle_late.dll");
    if (!late[index]) return 13;
    address=(TlsAddress)GetProcAddress(late[index],"LifeTlsAddress");
    if (!address) return 14;
    workerTls[index]=address();
    workerValue[index]=workerTls[index][0];
    workerTls[index][0]=100+index;
    InterlockedIncrement(&loaded);
    while (loaded<2) Sleep(0);
    if (workerTls[index][0]!=100+index) return 15;
    if (index==1) {
        WNDCLASSA cls={0};
        HWND hwnd;
        cls.lpfnWndProc=WindowProc;cls.hInstance=GetModuleHandleA(NULL);cls.lpszClassName="DllExit";
        if (!RegisterClassA(&cls)) return 16;
        hwnd=CreateWindowA(cls.lpszClassName,"",0,0,0,40,40,NULL,NULL,cls.hInstance,NULL);
        if (!hwnd) return 17;
        SendMessageA(hwnd,WM_APP+1,0,0);
        return 18; // ExitThread inside the callback must never return
    }
    return 31;
}
static int check_notifications(DWORD tid,BOOL lateAttach) {
    LifeState* state=life_state();
    DWORD attach[]={11,12,22,51};
    DWORD attachLate[]={11,12,22,51,41,42};
    DWORD detach[]={41,42,51,22,11,12};
    // Initialization order: dep, top, disabled, EXE, dynamically loaded late.
    DWORD ai=0,di=0,i;
    for (i=0;i<state->count;i++) {
        LifeEvent* e=&state->events[i];
        DWORD tag=e->module*10+e->kind;
        if (e->tid!=tid) continue;
        if (e->reason==DLL_THREAD_ATTACH) {
            if (ai>=(lateAttach?6:4) || tag!=(lateAttach?attachLate[ai]:attach[ai]) || e->reserved) return 1;
            ai++;
        }
        if (e->reason==DLL_THREAD_DETACH) {
            if (di>=6 || tag!=detach[di] || e->reserved) return 2;
            di++;
        }
    }
    return ai!=(lateAttach?6:4) || di!=6;
}
static DWORD WINAPI Survivor(LPVOID unused) {
    DWORD code;
    LifeState* state=life_state();
    DWORD i,seen=0;
    (void)unused;
    if (WaitForSingleObject(initial,INFINITE)!=WAIT_OBJECT_0 ||
        !GetExitCodeThread(initial,&code) || code!=73) return 50;
    for (i=0;i<state->count;i++)
        if (state->events[i].tid==workerTid[0] && state->events[i].reason==DLL_THREAD_DETACH) seen++;
    if (seen!=6 || state->failure) return 51;
    CloseHandle(initial);
    puts("PASSED: DLL/TLS thread lifecycle, loader serialization, dynamic TLS, suspended start and nested/main ExitThread");
    return 0;
}
static int Run(void) {
    HANDLE workers[2],third,survivor;
    DWORD before,code,i,failedCalls=0;
    LifeState* state=life_state();
    HMODULE disabled=LoadLibraryA("lifecycle_disabled.dll");
    HMODULE top=LoadLibraryA("lifecycle_top.dll");
    TlsAddress lateAddress;
    if (!disabled || !top || state->failure || LifeTopAnchor()!=2) return 1;
    if (LoadLibraryA("lifecycle_parent.dll") || GetModuleHandleA("lifecycle_parent.dll") ||
        LoadLibraryA("lifecycle_failed.dll") || LoadLibraryA("lifecycle_failed.dll") ||
        GetModuleHandleA("lifecycle_failed.dll")) return 26;
    for (i=0;i<state->count;i++) {
        LifeEvent* e=&state->events[i];
        if (e->module==6 && e->reason==DLL_PROCESS_ATTACH) failedCalls++;
        if ((e->module==1 || e->module==2) && e->kind==2 && e->reason==1 && !e->reserved) return 27;
        if (e->module==3 && e->reason==1 && e->reserved) return 28;
    }
    if (failedCalls!=1) return 29;
    if (DisableThreadLibraryCalls(NULL) || DisableThreadLibraryCalls(GetModuleHandleA("lifecycle_dep.dll"))) return 2;
    go=CreateEventA(NULL,TRUE,FALSE,NULL);
    before=state->count;
    for (i=0;i<2;i++) workers[i]=CreateThread(NULL,0,Worker,(LPVOID)i,CREATE_SUSPENDED,NULL);
    if (!workers[0] || !workers[1] || state->count!=before) return 3;
    if (ResumeThread(workers[0])!=1 || ResumeThread(workers[1])!=1) return 4;
    while (ready<2) Sleep(0);
    SetEvent(go);
    if (WaitForMultipleObjects(2,workers,TRUE,INFINITE)!=WAIT_OBJECT_0) return 5;
    for (i=0;i<2;i++) {
        if (!GetExitCodeThread(workers[i],&code) || code!=31+i || check_notifications(workerTid[i],FALSE)) return 6+i;
        CloseHandle(workers[i]);
    }
    if (late[0]!=late[1] || state->lateAttaches!=1 || workerTls[0]==workerTls[1]) return 8;
    if (!((workerValue[0]==(workerTid[0]^4) && workerValue[1]==LIFE_SEED) ||
          (workerValue[1]==(workerTid[1]^4) && workerValue[0]==LIFE_SEED))) return 9;
    lateAddress=(TlsAddress)GetProcAddress(late[0],"LifeTlsAddress");
    if (!lateAddress || lateAddress()[0]!=LIFE_SEED) return 20;
    third=CreateThread(NULL,0,Worker,(LPVOID)2,0,NULL);
    if (!third || WaitForSingleObject(third,INFINITE)!=WAIT_OBJECT_0 ||
        !GetExitCodeThread(third,&code) || code!=33 || check_notifications(workerTid[2],TRUE)) return 21;
    CloseHandle(third);CloseHandle(go);
    if (state->failure) return 22;
    // Initial-thread detach must complete before its HANDLE becomes signaled.
    workerTid[0]=GetCurrentThreadId();
    if (!DuplicateHandle(GetCurrentProcess(),GetCurrentThread(),GetCurrentProcess(),&initial,0,FALSE,DUPLICATE_SAME_ACCESS)) return 23;
    survivor=CreateThread(NULL,0,Survivor,NULL,0,NULL);
    if (!survivor) return 24;
    CloseHandle(survivor);
    ExitThread(73);
    return 25;
}
int main(void) {
    int result=Run();
    LifeState* state=life_state();
    DWORD i;
    printf("FAILED: DLL lifecycle stage=%d flags=%lu events=%lu\n",result,state->failure,state->count);
    for (i=0;i<state->count && i<256;i++) {
        LifeEvent* e=&state->events[i];
        printf("module=%lu kind=%lu reason=%lu tid=%lx value=%lx reserved=%lx\n",
            e->module,e->kind,e->reason,e->tid,e->value,e->reserved);
    }
    return result;
}
