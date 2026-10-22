#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <stdio.h>
#define CHECK(x) do { if(!(x)) {printf("FAIL unload line %d error %lu\n",__LINE__,GetLastError()); ExitProcess(1);} } while(0)
typedef void (*Configure)(DWORD*);
static DWORD logbuf[32];
static HMODULE workerModule;
static DWORD WINAPI reload_worker(LPVOID unused) {
    (void)unused;
    while(!((volatile DWORD*)logbuf)[24]) Sleep(0);
    logbuf[25]=1;
    workerModule=LoadLibraryA("unload_top.dll");
    logbuf[26]=1;
    return workerModule?0:1;
}
static void config(HMODULE m) { Configure fn=(Configure)GetProcAddress(m,"ConfigureUnload"); CHECK(fn); fn(logbuf); }
int main(void) {
    unsigned i;
    CHECK(!FreeLibrary((HMODULE)0x12345));
    for(i=0;i<80;++i) {
        HMODULE a=LoadLibraryA("unload_top.dll"),b,dep,ref;
        CHECK(a); b=LoadLibraryW(L"unload_top.dll"); CHECK(b==a);
        dep=GetModuleHandleA("unload_dep.dll"); CHECK(dep);
        config(a); config(dep); logbuf[0]=logbuf[1]=0;
        CHECK(GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCSTR)GetProcAddress(a,"UnloadTls"),&ref) && ref==a);
        CHECK(GetModuleHandleExW(0,L"unload_top.dll",&ref) && ref==a);
        CHECK(FreeLibrary(a) && FreeLibrary(a)); CHECK(logbuf[0]==0);
        CHECK(FreeLibrary(ref));
        CHECK(logbuf[0]==4 && !logbuf[1]);
        CHECK(logbuf[2]==21 && logbuf[3]==22 && logbuf[4]==11 && logbuf[5]==12);
        CHECK(!GetModuleHandleA("unload_top.dll") && !GetModuleHandleA("unload_dep.dll"));
        CHECK(!GetProcAddress(a,"UnloadTls") && !FreeLibrary(a));
    }
    {
        HMODULE a=LoadLibraryA("unload_top.dll"),b=LoadLibraryA("unload_side.dll"),dep=LoadLibraryA("unload_dep.dll"),ref;
        CHECK(a && b && dep); config(a); config(b); config(dep); logbuf[0]=logbuf[1]=0;
        CHECK(FreeLibrary(dep) && FreeLibrary(a));
        CHECK(logbuf[0]==2 && GetModuleHandleA("unload_dep.dll"));
        CHECK(FreeLibrary(b)); CHECK(logbuf[0]==6 && !logbuf[1]);
        CHECK(logbuf[4]==31 && logbuf[5]==32 && logbuf[6]==11 && logbuf[7]==12);
        a=LoadLibraryA("unload_top.dll"); CHECK(a);
        config(a); config(GetModuleHandleA("unload_dep.dll"));
        logbuf[0]=logbuf[1]=logbuf[24]=logbuf[25]=logbuf[26]=0;
        {
            HANDLE worker=CreateThread(NULL,0,reload_worker,NULL,0,NULL); DWORD code;
            CHECK(worker && FreeLibrary(a));
            CHECK(WaitForSingleObject(worker,5000)==WAIT_OBJECT_0);
            CHECK(GetExitCodeThread(worker,&code) && code==0 && workerModule);
            CHECK(logbuf[0]==4 && !logbuf[1] && logbuf[25] && logbuf[26]);
            CHECK(CloseHandle(worker) && FreeLibrary(workerModule));
        }
        a=LoadLibraryA("unload_top.dll"); CHECK(a);
        {
            LPTHREAD_START_ROUTINE fn=(LPTHREAD_START_ROUTINE)GetProcAddress(a,"UnloadSelf@4");
            HANDLE worker; DWORD code;
            CHECK(fn); worker=CreateThread(NULL,0,fn,a,0,NULL); CHECK(worker);
            CHECK(WaitForSingleObject(worker,5000)==WAIT_OBJECT_0);
            CHECK(GetExitCodeThread(worker,&code) && code==77);
            CHECK(!GetModuleHandleA("unload_top.dll") && !GetModuleHandleA("unload_dep.dll"));
            CHECK(CloseHandle(worker));
        }
        a=LoadLibraryA("unload_top.dll"); CHECK(a);
        CHECK(!GetModuleHandleExA(3,"unload_top.dll",&ref));
        CHECK(GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_PIN,"unload_top.dll",&ref));
        CHECK(FreeLibrary(a) && FreeLibrary(ref) && GetModuleHandleA("unload_top.dll"));
        /* Do not retain the stack log for orderly process detach (reserved!=0). */
        ((Configure)GetProcAddress(a,"ConfigureUnload"))(NULL);
    }
    puts("PASSED: DLL repeated loads/unloads, TLS slot reuse, dependencies, GetModuleHandleEx and pinning");
    return 0;
}
