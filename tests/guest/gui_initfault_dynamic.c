#include "initfault_shared.h"
#include <stdio.h>
#define CHECK(x) do{if(!(x)){printf("FAIL initfault line %d mode %lu phase %lu err %lu flags %lu count %lu\n",__LINE__,s->mode,s->phase,GetLastError(),s->failure,s->count);ExitProcess(1);}}while(0)
static int __cdecl resume_handler(EXCEPTION_RECORD* r,void* f,CONTEXT* c,void* d){(void)r;(void)f;(void)c;(void)d;return 0;}
struct Reg{struct Reg* next;void* handler;};
static void* baselineSeh;
static void check_seh(InitFaultState* s){
    struct Reg reg;void* old;
    __asm__ volatile("movl %%fs:0,%0":"=r"(old));
    CHECK(old==baselineSeh);
    reg.next=(struct Reg*)old;reg.handler=(void*)resume_handler;
    __asm__ volatile("movl %0,%%fs:0"::"r"(&reg):"memory");
    RaiseException(0xe042f099,0,0,NULL);
    __asm__ volatile("movl %0,%%fs:0"::"r"(old):"memory");
    CHECK(!s->failure);
}
static DWORD WINAPI other_worker(LPVOID unused){
    InitFaultState* s=InitFaultControl();struct Reg reg;void* old;(void)unused;
    SetEvent(s->ready);WaitForSingleObject(s->go,5000);
    __asm__ volatile("movl %%fs:0,%0":"=r"(old));reg.next=(struct Reg*)old;reg.handler=(void*)resume_handler;
    __asm__ volatile("movl %0,%%fs:0"::"r"(&reg):"memory");
    RaiseException(INITFAULT_OTHER,0,0,NULL);++s->otherHandled;
    __asm__ volatile("movl %0,%%fs:0"::"r"(old):"memory");return 18;
}
static void finish_workers(InitFaultState* s){
    DWORD code;
    if(s->worker){CHECK(WaitForSingleObject(s->worker,5000)==WAIT_OBJECT_0);CHECK(GetExitCodeThread(s->worker,&code)&&code==19&&CloseHandle(s->worker));s->worker=NULL;}
    if(s->suspended){CHECK(ResumeThread(s->suspended)==1&&WaitForSingleObject(s->suspended,5000)==WAIT_OBJECT_0);CHECK(GetExitCodeThread(s->suspended,&code)&&code==19&&CloseHandle(s->suspended));s->suspended=NULL;}
}
int main(void){
    InitFaultState* s=InitFaultControl();DWORD mode,i;HMODULE h;
    __asm__ volatile("movl %%fs:0,%0":"=r"(baselineSeh));
    for(mode=0;mode<=13;++mode){
        if(mode==5)continue;
        s->mode=mode;s->phase=0;s->afterFault=s->handlerCalls=s->handlerAfter=s->count=s->afterNested=0;
        h=LoadLibraryA("initfault_module.dll");
        if(mode<=2 || mode==11){CHECK(h);CHECK(FreeLibrary(h));}
        else{CHECK(!h && GetLastError()==ERROR_DLL_INIT_FAILED);CHECK(!s->afterFault && !s->handlerAfter);}
        CHECK(!GetModuleHandleA("initfault_module.dll")&&!s->failure);
        if(mode>=3 && mode!=10 && mode!=11){
            for(i=0;i<s->count;++i)CHECK(s->events[i][1]!=DLL_PROCESS_DETACH);
        }
        if(mode==11)CHECK(s->afterNested==1&&!GetModuleHandleA("initfault_child.dll"));
        finish_workers(s);check_seh(s);
    }
    s->mode=3;s->phase=1;s->afterFault=s->count=0;
    CHECK(!LoadLibraryA("initfault_module.dll")&&GetLastError()==ERROR_DLL_INIT_FAILED);
    CHECK(s->count==1 && s->events[0][0]==11 && !s->afterFault);check_seh(s);
    s->mode=14;s->phase=0;s->afterFault=s->count=0;
    s->go=CreateEventA(NULL,TRUE,FALSE,NULL);s->ready=CreateEventA(NULL,TRUE,FALSE,NULL);
    {HANDLE other=CreateThread(NULL,0,other_worker,NULL,0,NULL);DWORD code;
        CHECK(other&&WaitForSingleObject(s->ready,5000)==WAIT_OBJECT_0);
        CHECK(!LoadLibraryA("initfault_module.dll")&&GetLastError()==ERROR_DLL_INIT_FAILED);
        CHECK(WaitForSingleObject(other,5000)==WAIT_OBJECT_0&&GetExitCodeThread(other,&code)&&code==18&&s->otherHandled==1&&CloseHandle(other));
    }
    CHECK(CloseHandle(s->go)&&CloseHandle(s->ready));check_seh(s);
    s->mode=0;s->phase=0;h=LoadLibraryA("initfault_module.dll");CHECK(h&&FreeLibrary(h));
    puts("PASSED: loader SEH/CPU faults, handler faults, no failed attach detach, TLS recovery, nested boundary and per-thread exception state");
    return 0;
}
