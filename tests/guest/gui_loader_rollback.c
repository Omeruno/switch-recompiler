#include "rollback_shared.h"
#include <stdio.h>
#define CHECK(x) do{if(!(x)){printf("FAIL rollback line %d error %lu flags %lu events %lu\n",__LINE__,GetLastError(),s->failure,s->count);ExitProcess(1);}}while(0)
typedef DWORD (*Value)(void);
static HANDLE gate;
static DWORD WINAPI failing_worker(LPVOID unused) {
    (void)unused;
    if(WaitForSingleObject(gate,5000)!=WAIT_OBJECT_0)return 1;
    if(LoadLibraryA("rollback_top.dll") || GetLastError()!=ERROR_DLL_INIT_FAILED)return 2;
    return 0;
}
static DWORD WINAPI suspended_worker(LPVOID unused) {
    (void)unused;
    return GetModuleHandleA("rollback_top.dll") || GetModuleHandleA("rollback_dep.dll")?1:17;
}
static void check_failure_order(RollbackState* s) {
    DWORD ids[]={1,1,2,2,2,2,1,1},kinds[]={1,2,1,2,1,2,1,2};unsigned i;
    CHECK(s->count==8 && !s->failure);
    for(i=0;i<8;++i) CHECK(s->events[i][0]==ids[i] && s->events[i][1]==kinds[i] &&
        s->events[i][2]==(i<4?DLL_PROCESS_ATTACH:DLL_PROCESS_DETACH) && !s->events[i][4]);
}
int main(void) {
    RollbackState* s=RollbackControl(); unsigned i; HMODULE h,dep,fwd; Value value;
    s->testing=1;s->failTop=1;
    CHECK(!LoadLibraryA("a_really_missing_rollback_test.dll") && GetLastError()==ERROR_MOD_NOT_FOUND);
    for(i=0;i<80;++i) {
        s->count=0;
        CHECK(!LoadLibraryA("rollback_top.dll") && GetLastError()==ERROR_DLL_INIT_FAILED);
        CHECK(!GetModuleHandleA("rollback_top.dll") && !GetModuleHandleA("rollback_dep.dll"));
        check_failure_order(s);
    }
    s->count=0;
    {
        HANDLE workers[2],suspended;DWORD code;
        gate=CreateEventA(NULL,TRUE,FALSE,NULL);CHECK(gate);
        suspended=CreateThread(NULL,0,suspended_worker,NULL,CREATE_SUSPENDED,NULL);CHECK(suspended);
        workers[0]=CreateThread(NULL,0,failing_worker,NULL,0,NULL);
        workers[1]=CreateThread(NULL,0,failing_worker,NULL,0,NULL);CHECK(workers[0] && workers[1]);
        CHECK(SetEvent(gate) && WaitForMultipleObjects(2,workers,TRUE,5000)==WAIT_OBJECT_0);
        for(i=0;i<2;++i)CHECK(GetExitCodeThread(workers[i],&code) && !code && CloseHandle(workers[i]));
        CHECK(s->count==16 && !s->failure);
        CHECK(ResumeThread(suspended)==1 && WaitForSingleObject(suspended,5000)==WAIT_OBJECT_0);
        CHECK(GetExitCodeThread(suspended,&code) && code==17 && CloseHandle(suspended) && CloseHandle(gate));
    }
    s->failTop=0; h=LoadLibraryW(L"rollback_top.dll"); CHECK(h);
    value=(Value)GetProcAddress(h,"RollbackValue");CHECK(value && value()==(GetCurrentThreadId()^2));
    CHECK(FreeLibrary(h));
    s->failTop=1;dep=LoadLibraryA("rollback_dep.dll");CHECK(dep);s->count=0;
    CHECK(!LoadLibraryA("rollback_top.dll") && GetLastError()==ERROR_DLL_INIT_FAILED);
    CHECK(GetModuleHandleA("rollback_dep.dll")==dep && s->count==4 && !s->failure);
    CHECK(FreeLibrary(dep));
    s->count=0;s->keepNested=1;
    CHECK(!LoadLibraryA("rollback_top.dll") && GetLastError()==ERROR_DLL_INIT_FAILED);
    CHECK(s->keeper && GetModuleHandleA("rollback_keeper.dll")==s->keeper && GetModuleHandleA("rollback_dep.dll"));
    CHECK(!GetModuleHandleA("rollback_top.dll") && !s->failure);
    CHECK(FreeLibrary(s->keeper));s->keepNested=0;
    CHECK(!GetModuleHandleA("rollback_dep.dll"));
    s->count=0;s->keepNested=2;s->keeper=NULL;
    CHECK(!LoadLibraryA("rollback_top.dll") && GetLastError()==ERROR_DLL_INIT_FAILED);
    CHECK(s->keeper && !GetModuleHandleA("rollback_cycle.dll") && !GetModuleHandleA("rollback_top.dll") && !GetModuleHandleA("rollback_dep.dll"));
    CHECK(!s->failure && s->count==12);s->keepNested=0;
    s->failDep=1;s->count=0;
    CHECK(!LoadLibraryA("rollback_parent.dll") && GetLastError()==ERROR_DLL_INIT_FAILED);
    CHECK(!GetModuleHandleA("rollback_parent.dll") && !GetModuleHandleA("rollback_baddep.dll"));
    CHECK(s->count==4 && s->events[0][0]==3 && !s->failure);
    s->failDep=0;h=LoadLibraryA("rollback_parent.dll");CHECK(h && FreeLibrary(h));
    for(i=0;i<2;++i){
        CHECK(!LoadLibraryA("rollback_invalid_parent.dll") && GetLastError()==ERROR_BAD_EXE_FORMAT);
        CHECK(!GetModuleHandleA("rollback_invalid_parent.dll") && !GetModuleHandleA("rollback_invalid_child.dll"));
    }
    CHECK(!LoadLibraryA("rollback_invalid_tls.dll") && GetLastError()==ERROR_NOT_ENOUGH_MEMORY);
    CHECK(!GetModuleHandleA("rollback_invalid_tls.dll"));
    s->count=0; fwd=LoadLibraryA("rollback_forward.dll");CHECK(fwd);
    CHECK(!GetModuleHandleA("rollback_top.dll"));
    CHECK(!GetProcAddress(fwd,"ForwardValue"));
    CHECK(GetModuleHandleA("rollback_forward.dll")==fwd && !GetModuleHandleA("rollback_top.dll") && !GetModuleHandleA("rollback_dep.dll"));
    s->failTop=0;
    CHECK(!GetProcAddress(fwd,"MissingForward") && !GetModuleHandleA("rollback_top.dll"));
    value=(Value)GetProcAddress(fwd,"ForwardValue");CHECK(value && value()==(GetCurrentThreadId()^2));
    i=s->count;CHECK(GetProcAddress(fwd,"ForwardValue")== (FARPROC)value && s->count==i);
    CHECK(FreeLibrary(fwd));CHECK(!GetModuleHandleA("rollback_top.dll") && !GetModuleHandleA("rollback_dep.dll"));
    CHECK(!s->failure);s->testing=0;
    puts("PASSED: failed DLL/TLS/dependency rollback, 80 retries, independent roots, nested loads, invalid image and forwarded initialization");
    return 0;
}
