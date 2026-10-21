#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <stdio.h>
#include <io.h>
#include <fcntl.h>
#define CHECK(x) do { if (!(x)) { printf("FAIL handle policy line %d error %lu\n",__LINE__,GetLastError()); return __LINE__; } } while (0)
static volatile LONG callbacks;
static VOID CALLBACK Apc(ULONG_PTR data) { callbacks+=(LONG)data; }
static DWORD WINAPI Worker(LPVOID unused) { (void)unused;return 42; }
static HANDLE Copy(HANDLE source,DWORD access,BOOL inherit,DWORD options) {
    HANDLE copy=NULL;
    return DuplicateHandle(GetCurrentProcess(),source,GetCurrentProcess(),&copy,access,inherit,options) ? copy : NULL;
}
int main(void) {
    SECURITY_ATTRIBUTES sa={sizeof(sa),NULL,TRUE},bad={0,NULL,TRUE};
    HANDLE event,copy,waiter,modifier,file,readOnly,writeOnly,noData,thread,query,suspend,apc,threadWait;
    DWORD flags,code,count;
    char ch=0;
    int fd;
    event=CreateEventA(&sa,FALSE,TRUE,NULL);CHECK(event);
    CHECK(GetHandleInformation(event,&flags) && flags==HANDLE_FLAG_INHERIT);
    CHECK(SetHandleInformation(event,HANDLE_FLAG_PROTECT_FROM_CLOSE,HANDLE_FLAG_PROTECT_FROM_CLOSE));
    CHECK(GetHandleInformation(event,&flags) && flags==3);
    CHECK(!CloseHandle(event) && GetLastError()==ERROR_ACCESS_DENIED);
    copy=Copy(event,0,FALSE,DUPLICATE_SAME_ACCESS);CHECK(copy);
    CHECK(GetHandleInformation(copy,&flags) && flags==0 && CloseHandle(copy));
    CHECK(!Copy(event,0,FALSE,DUPLICATE_SAME_ACCESS|DUPLICATE_CLOSE_SOURCE) && GetLastError()==ERROR_ACCESS_DENIED);
    CHECK(WaitForSingleObject(event,0)==WAIT_OBJECT_0);
    CHECK(SetHandleInformation(event,HANDLE_FLAG_INHERIT,0));
    CHECK(GetHandleInformation(event,&flags) && flags==HANDLE_FLAG_PROTECT_FROM_CLOSE);
    CHECK(SetHandleInformation(event,HANDLE_FLAG_PROTECT_FROM_CLOSE,0));
    waiter=Copy(event,SYNCHRONIZE,TRUE,0);modifier=Copy(event,EVENT_MODIFY_STATE,FALSE,0);CHECK(waiter && modifier);
    CHECK(!SetEvent(waiter) && GetLastError()==ERROR_ACCESS_DENIED);
    CHECK(!ResetEvent(waiter) && GetLastError()==ERROR_ACCESS_DENIED);
    CHECK(WaitForSingleObjectEx(modifier,0,TRUE)==WAIT_FAILED && GetLastError()==ERROR_ACCESS_DENIED);
    copy=Copy(waiter,EVENT_ALL_ACCESS,FALSE,DUPLICATE_SAME_ACCESS);CHECK(copy);
    CHECK(GetHandleInformation(copy,&flags) && flags==0);
    CHECK(!SetEvent(copy) && GetLastError()==ERROR_ACCESS_DENIED && SetEvent(modifier));
    CHECK(WaitForSingleObject(copy,0)==WAIT_OBJECT_0);
    CHECK(CloseHandle(copy) && CloseHandle(waiter) && CloseHandle(modifier) && CloseHandle(event));
    flags=99;CHECK(!GetHandleInformation(event,&flags) && GetLastError()==ERROR_INVALID_HANDLE && flags==99);
    CHECK(!SetHandleInformation(event,1,1) && GetLastError()==ERROR_INVALID_HANDLE);
    CHECK(!CreateEventA(&bad,TRUE,TRUE,NULL) && GetLastError()==ERROR_INVALID_PARAMETER);
    file=CreateFileA("handle_policy.tmp",GENERIC_READ|GENERIC_WRITE,0,&sa,CREATE_ALWAYS,FILE_FLAG_DELETE_ON_CLOSE,NULL);CHECK(file!=INVALID_HANDLE_VALUE);
    CHECK(GetHandleInformation(file,&flags) && flags==HANDLE_FLAG_INHERIT);
    CHECK(WriteFile(file,"AB",2,&count,NULL) && count==2 && SetFilePointer(file,0,NULL,FILE_BEGIN)==0);
    readOnly=Copy(file,GENERIC_READ,FALSE,0);writeOnly=Copy(file,GENERIC_WRITE,FALSE,0);noData=Copy(file,0,FALSE,0);CHECK(readOnly && writeOnly && noData);
    count=99;CHECK(!WriteFile(readOnly,"X",1,&count,NULL) && GetLastError()==ERROR_ACCESS_DENIED && count==0);
    count=99;CHECK(!ReadFile(writeOnly,&ch,1,&count,NULL) && GetLastError()==ERROR_ACCESS_DENIED && count==0);
    CHECK(!ReadFile(noData,&ch,1,&count,NULL) && GetLastError()==ERROR_ACCESS_DENIED);
    CHECK(!SetEndOfFile(readOnly) && GetLastError()==ERROR_ACCESS_DENIED);
    CHECK(!Copy(readOnly,GENERIC_READ|GENERIC_WRITE,FALSE,0) && GetLastError()==ERROR_ACCESS_DENIED);
    CHECK(ReadFile(readOnly,&ch,1,&count,NULL) && count==1 && ch=='A');
    // Reinterpreting the same HANDLE as a CRT descriptor cannot bypass access.
    copy=Copy(readOnly,0,FALSE,DUPLICATE_SAME_ACCESS);CHECK(copy);
    fd=_open_osfhandle((intptr_t)copy,_O_BINARY|_O_WRONLY);CHECK(fd>=0 && _write(fd,"X",1)==-1 && _close(fd)==0);
    CHECK(WriteFile(writeOnly,"C",1,&count,NULL) && count==1);
    CHECK(SetFilePointer(file,1,NULL,FILE_BEGIN)==1 && ReadFile(file,&ch,1,&count,NULL) && ch=='C');
    CHECK(CloseHandle(readOnly) && CloseHandle(writeOnly) && CloseHandle(noData) && CloseHandle(file));
    CHECK(GetFileAttributesA("handle_policy.tmp")==INVALID_FILE_ATTRIBUTES);
    file=CreateFileA("handle_append.tmp",GENERIC_READ|GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_FLAG_DELETE_ON_CLOSE,NULL);CHECK(file!=INVALID_HANDLE_VALUE);
    copy=Copy(file,FILE_APPEND_DATA,FALSE,0);CHECK(copy);
    CHECK(WriteFile(file,"AB",2,&count,NULL) && SetFilePointer(file,0,NULL,FILE_BEGIN)==0);
    CHECK(WriteFile(copy,"D",1,&count,NULL) && count==1 && GetFileSize(file,NULL)==3);
    CHECK(SetFilePointer(file,2,NULL,FILE_BEGIN)==2 && ReadFile(file,&ch,1,&count,NULL) && ch=='D');
    CHECK(CloseHandle(copy) && CloseHandle(file));
    file=CreateFileA("CONOUT$",GENERIC_READ|GENERIC_WRITE,0,&sa,OPEN_EXISTING,0,NULL);CHECK(file!=INVALID_HANDLE_VALUE);
    CHECK(GetHandleInformation(file,&flags) && flags==HANDLE_FLAG_INHERIT && CloseHandle(file));
    thread=CreateThread(&sa,0,Worker,NULL,CREATE_SUSPENDED,NULL);CHECK(thread);
    CHECK(GetHandleInformation(thread,&flags) && flags==HANDLE_FLAG_INHERIT);
    query=Copy(thread,THREAD_QUERY_INFORMATION,FALSE,0);suspend=Copy(thread,THREAD_SUSPEND_RESUME,FALSE,0);
    apc=Copy(thread,THREAD_SET_CONTEXT,FALSE,0);threadWait=Copy(thread,SYNCHRONIZE,FALSE,0);CHECK(query && suspend && apc && threadWait);
    code=99;CHECK(!GetExitCodeThread(threadWait,&code) && GetLastError()==ERROR_ACCESS_DENIED && code==99);
    CHECK(GetExitCodeThread(query,&code) && code==STILL_ACTIVE);
    CHECK(ResumeThread(query)==(DWORD)-1 && GetLastError()==ERROR_ACCESS_DENIED);
    CHECK(!QueueUserAPC(Apc,query,1) && GetLastError()==ERROR_ACCESS_DENIED && !callbacks);
    CHECK(WaitForSingleObject(query,0)==WAIT_FAILED && GetLastError()==ERROR_ACCESS_DENIED);
    CHECK(QueueUserAPC(Apc,apc,2) && CloseHandle(thread) && ResumeThread(suspend)==1);
    CHECK(WaitForSingleObject(threadWait,2000)==WAIT_OBJECT_0 && callbacks==2);
    CHECK(GetExitCodeThread(query,&code) && code==42);
    CHECK(CloseHandle(query) && CloseHandle(suspend) && CloseHandle(apc) && CloseHandle(threadWait));
    puts("PASSED: per-HANDLE flags/protected close, duplicate rights, file/CRT I/O permissions and thread/APC access masks");
    return 0;
}
