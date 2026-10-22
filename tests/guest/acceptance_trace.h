#ifndef ACCEPTANCE_TRACE_H
#define ACCEPTANCE_TRACE_H
#include <windows.h>
#include <stdio.h>
#include <errno.h>
/* A small checkpoint journal; no per-instruction or per-frame SD writes.
   Preserve last-error so instrumentation cannot change CHECK expectations. */
static inline void AcceptanceTraceAt(const char* source,int line,const char* text,int failed){
    DWORD error=GetLastError(),thread=GetCurrentThreadId(),tick=GetTickCount();
    int savedErrno=errno;
    FILE* log=fopen("stage-c-details.txt","a");
    if(log){fprintf(log,"%s %s:%d thread=%lu tick=%lu error=%lu: %s\n",
        failed?"FAIL":"STEP",source,line,thread,tick,error,text);fclose(log);}
    errno=savedErrno;SetLastError(error);
}
static inline void AcceptanceTraceBegin(const char* label){
    DWORD error=GetLastError();int savedErrno=errno;FILE* log=fopen("stage-c-details.txt","w");
    if(log){fprintf(log,"Stage C details v2: %s\n",label);fclose(log);}
    errno=savedErrno;SetLastError(error);
}
#define ACCEPT_STEP(text) AcceptanceTraceAt(__FILE__,__LINE__,text,0)
#define ACCEPT_FAIL(text) AcceptanceTraceAt(__FILE__,__LINE__,text,1)
#endif
