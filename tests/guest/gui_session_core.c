/* Redistributable sequential-session fixture. No UI/legacy isolation claim. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <io.h>
#include <fcntl.h>
#include <locale.h>
__declspec(dllexport) volatile DWORD SessionProbe;
static void on_exit(void){SessionProbe=2;}
#define CHECK(x) do {if(!(x)){printf("FAIL session core line %d: %s\n",__LINE__,#x);return __LINE__;}}while(0)
int main(int argc,char** argv) {
    const char* mode=argc>1?argv[1]:"single";
    int first=strcmp(mode,"first")==0 || strcmp(mode,"abort")==0;
    char value[64];DWORD slot;HANDLE heap=GetProcessHeap();void* p;
    CHECK(heap!=NULL);p=HeapAlloc(heap,HEAP_ZERO_MEMORY,96);CHECK(p && ((char*)p)[0]==0);
    CHECK(strstr(GetCommandLineA(),mode)!=NULL || strcmp(mode,"single")==0);
    CHECK(SetErrorMode(0)==0);
    /* MinGW installs its own exception filter before main. */
    CHECK(SetUnhandledExceptionFilter(NULL)!=(LPTOP_LEVEL_EXCEPTION_FILTER)(ULONG_PTR)0x12345678);
    CHECK(GetEnvironmentVariableA("SR_SESSION_CORE",value,sizeof(value))==0);
    CHECK(GetEnvironmentVariableA("SYSTEMROOT",value,sizeof(value))>0 && strcmp(value,"C:\\WINDOWS")==0);
    slot=TlsAlloc();CHECK(slot==0 && TlsGetValue(slot)==NULL);
    CHECK(rand()==41);CHECK(signal(SIGTERM,SIG_DFL)==SIG_DFL);
    CHECK(strtok(NULL,",")==NULL);CHECK(strcmp(setlocale(LC_ALL,NULL),"C")==0);CHECK(tmpnam(NULL)!=NULL);
    CHECK(_write(2,"session stderr ready\n",21)==21);
    if(first) {
        FILE* stream;HANDLE held;DWORD i;
        CHECK(SetEnvironmentVariableA("SR_SESSION_CORE","old-process"));
        SetErrorMode(0x123);SetUnhandledExceptionFilter((LPTOP_LEVEL_EXCEPTION_FILTER)(ULONG_PTR)0x12345678);
        CHECK(TlsSetValue(slot,(void*)0x1234));for(i=1;i<64;++i)CHECK(TlsAlloc()==i);
        {static char tokens[]="old,remaining";CHECK(strcmp(strtok(tokens,","),"old")==0);}
        srand(1234);signal(SIGTERM,SIG_IGN);CHECK(_close(2)==0);
        stream=fopen("session-leaked-stream.txt","wb");CHECK(stream);CHECK(fwrite("flushed",1,7,stream)==7);
        held=CreateFileA("session-delete.tmp",GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL|FILE_FLAG_DELETE_ON_CLOSE,NULL);CHECK(held!=INVALID_HANDLE_VALUE);
        CHECK(atexit(on_exit)==0);SessionProbe=1;
        if(strcmp(mode,"abort")==0)for(;;)Sleep(1000);
        /* Deliberately leave heap, TLS, stream and HANDLE open for Process teardown. */
    } else {
        CHECK(HeapFree(heap,0,p));CHECK(TlsFree(slot));
    }
    puts("PASSED: session core defaults and owned resources");return 0;
}
