/* Same source regressions as CI, linked into one unattended hardware probe. */
#include <windows.h>
#include <stdio.h>
int StageC_sync(void);
int StageC_wait_multiple(void);
int StageC_named(void);
int StageC_signalwait(void);
int StageC_timer_contention(void);
int StageC_mixed_apc(void);
int StageC_input(void);
int StageC_timer_exit(void);
struct Suite {const char* name;int (*run)(void);};
static const struct Suite suites[]={
    {"mutex/semaphore producer-consumer",StageC_sync},
    {"multiple waits",StageC_wait_multiple},
    {"named objects and handle access",StageC_named},
    {"SignalObjectAndWait/APC",StageC_signalwait},
    {"timer contention and wait-all",StageC_timer_contention},
    {"mixed MsgWait/timer/user APC",StageC_mixed_apc},
    {"GUI input ownership and callback exit",StageC_input},
    {"ExitThread inside timer APC",StageC_timer_exit}
};
int main(void){
    FILE* log=fopen("stage-c-acceptance.txt","w");unsigned i;HWND result;MSG msg;DWORD until;
    if(!log){puts("FAIL: cannot create stage-c-acceptance.txt");return 2;}
    fprintf(log,"Stage C acceptance v1\nRUNNING: incomplete report is not a pass\n");fflush(log);
    for(i=0;i<sizeof(suites)/sizeof(suites[0]);++i){
        int code;
        fprintf(log,"BEGIN %u: %s\n",i+1,suites[i].name);fflush(log);
        code=suites[i].run();
        if(code){fprintf(log,"FAIL %u: code %d\n",i+1,code);fclose(log);return code;}
        fprintf(log,"PASS %u: %s\n",i+1,suites[i].name);fflush(log);
    }
    fprintf(log,"PASS: ALL 8 SUITES\n");fclose(log);
    /* Keep the outcome visible briefly without making CI depend on a click. */
    result=CreateWindowA("STATIC","Stage C: PASS\nAll 8 suites passed.\nReport: stage-c-acceptance.txt",
        WS_OVERLAPPEDWINDOW|WS_VISIBLE|SS_CENTER,160,160,460,180,NULL,NULL,GetModuleHandleA(NULL),NULL);
    until=GetTickCount()+3000;
    while(IsWindow(result) && (LONG)(until-GetTickCount())>0){
        while(PeekMessageA(&msg,NULL,0,0,PM_REMOVE))DispatchMessageA(&msg);
        Sleep(16);
    }
    if(IsWindow(result))DestroyWindow(result);
    puts("PASSED: Stage C acceptance v1, ALL 8 SUITES; report stage-c-acceptance.txt");return 0;
}
