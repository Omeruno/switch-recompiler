/* Real Win32 guest regression: object identity vs HANDLE ownership. */
#include <windows.h>
#include <stdio.h>
#define CHECK(expr) do { if (!(expr)) { printf("FAIL line %d error %lu\n", __LINE__, GetLastError()); return __LINE__; } } while (0)
static HANDLE ownerMutex;
static DWORD WINAPI SignalAlias(LPVOID handle) {
    Sleep(2);
    return SetEvent((HANDLE)handle) ? 42 : 43;
}
static DWORD WINAPI AbandonMutex(LPVOID unused) {
    (void)unused;
    if (WaitForSingleObject(ownerMutex,INFINITE)!=WAIT_OBJECT_0) return 10;
    if (WaitForSingleObject(ownerMutex,0)!=WAIT_OBJECT_0) return 11;
    return 42; /* Exit while recursively owning the mutex. */
}
static HANDLE Copy(HANDLE source) {
    HANDLE copy=NULL;
    if (!DuplicateHandle(GetCurrentProcess(),source,GetCurrentProcess(),
                         &copy,0,FALSE,DUPLICATE_SAME_ACCESS)) return NULL;
    return copy;
}
int main(void) {
    HANDLE event=CreateEventA(NULL,FALSE,FALSE,NULL), alias=Copy(event);
    CHECK(event && alias && event!=alias);
    DWORD id=0, code=0;
    HANDLE worker=CreateThread(NULL,0,SignalAlias,alias,CREATE_SUSPENDED,&id);
    HANDLE workerCopy=Copy(worker);
    CHECK(worker && workerCopy && worker!=workerCopy);
    CHECK(CloseHandle(worker));
    CHECK(!CloseHandle(worker) && GetLastError()==ERROR_INVALID_HANDLE);
    CHECK(ResumeThread(workerCopy)==1);
    /* SetEvent(alias) must wake a guest waiting on event. */
    CHECK(WaitForSingleObject(event,1000)==WAIT_OBJECT_0);
    CHECK(WaitForSingleObject(alias,0)==WAIT_TIMEOUT);
    CHECK(WaitForSingleObject(workerCopy,INFINITE)==WAIT_OBJECT_0);
    CHECK(GetExitCodeThread(workerCopy,&code) && code==42);
    CHECK(CloseHandle(workerCopy));
    CHECK(CloseHandle(event) && SetEvent(alias));
    CHECK(WaitForSingleObject(event,0)==WAIT_FAILED);
    CHECK(WaitForSingleObject(alias,0)==WAIT_OBJECT_0);
    CHECK(CloseHandle(alias));

    HANDLE sem=CreateSemaphoreA(NULL,0,2,NULL), semCopy=Copy(sem);
    CHECK(sem && semCopy && CloseHandle(sem));
    CHECK(ReleaseSemaphore(semCopy,2,NULL));
    CHECK(WaitForSingleObject(semCopy,0)==WAIT_OBJECT_0);
    HANDLE pair[2]={semCopy,Copy(semCopy)};
    CHECK(pair[1]);
    CHECK(WaitForMultipleObjects(2,pair,TRUE,0)==WAIT_FAILED);
    CHECK(GetLastError()==ERROR_INVALID_PARAMETER);
    /* Invalid wait-all aliases must not consume the remaining token. */
    CHECK(WaitForSingleObject(semCopy,0)==WAIT_OBJECT_0);
    CHECK(WaitForSingleObject(pair[1],0)==WAIT_TIMEOUT);
    CHECK(!SetEvent(semCopy) && GetLastError()==ERROR_INVALID_HANDLE);
    CHECK(CloseHandle(pair[1]) && CloseHandle(semCopy));

    ownerMutex=CreateMutexA(NULL,FALSE,NULL);
    HANDLE mutexCopy=Copy(ownerMutex);
    worker=CreateThread(NULL,0,AbandonMutex,NULL,0,&id);
    CHECK(ownerMutex && mutexCopy && worker);
    CHECK(WaitForSingleObject(worker,INFINITE)==WAIT_OBJECT_0);
    CHECK(GetExitCodeThread(worker,&code) && code==42);
    event=CreateEventA(NULL,TRUE,FALSE,NULL);
    pair[0]=event; pair[1]=mutexCopy;
    /* A partial wait-all may not clear mutex abandonment. */
    CHECK(WaitForMultipleObjects(2,pair,TRUE,0)==WAIT_TIMEOUT);
    CHECK(WaitForMultipleObjects(2,pair,FALSE,0)==WAIT_ABANDONED_0+1);
    CHECK(ReleaseMutex(ownerMutex));
    CHECK(!ReleaseMutex(mutexCopy) && GetLastError()==ERROR_NOT_OWNER);
    CHECK(WaitForSingleObject(mutexCopy,0)==WAIT_OBJECT_0);
    CHECK(ReleaseMutex(mutexCopy));
    CHECK(CloseHandle(worker));
    /* Re-abandon, then acquire the entire set atomically. */
    worker=CreateThread(NULL,0,AbandonMutex,NULL,0,&id);
    CHECK(worker && WaitForSingleObject(worker,INFINITE)==WAIT_OBJECT_0);
    CHECK(SetEvent(event));
    CHECK(WaitForMultipleObjects(2,pair,TRUE,0)==WAIT_ABANDONED_0+1);
    CHECK(ReleaseMutex(mutexCopy));
    CHECK(CloseHandle(worker) && CloseHandle(ownerMutex));
    CHECK(CloseHandle(mutexCopy) && CloseHandle(event));

    /* Close-source transfer and close-only form. */
    event=CreateEventA(NULL,TRUE,TRUE,NULL); alias=NULL;
    CHECK(DuplicateHandle(GetCurrentProcess(),event,GetCurrentProcess(),
                         &alias,0,FALSE,DUPLICATE_SAME_ACCESS|DUPLICATE_CLOSE_SOURCE));
    CHECK(event!=alias && WaitForSingleObject(event,0)==WAIT_FAILED);
    CHECK(WaitForSingleObject(alias,0)==WAIT_OBJECT_0);
    CHECK(DuplicateHandle(GetCurrentProcess(),alias,NULL,NULL,0,FALSE,DUPLICATE_CLOSE_SOURCE));
    CHECK(WaitForSingleObject(alias,0)==WAIT_FAILED);
    /* Per contract, CLOSE_SOURCE also applies if target validation fails. */
    event=CreateEventA(NULL,TRUE,FALSE,NULL);
    CHECK(!DuplicateHandle(GetCurrentProcess(),event,GetCurrentProcess(),
                          (LPHANDLE)1,0,FALSE,DUPLICATE_SAME_ACCESS|DUPLICATE_CLOSE_SOURCE));
    CHECK(WaitForSingleObject(event,0)==WAIT_FAILED);
    /* Invalid input must leave output and unrelated objects untouched. */
    alias=(HANDLE)0x1234;
    CHECK(!DuplicateHandle(GetCurrentProcess(),(HANDLE)0x654321,GetCurrentProcess(),
                          &alias,0,FALSE,DUPLICATE_SAME_ACCESS));
    CHECK(alias==(HANDLE)0x1234);
    /* File aliases share position, and delete-on-close waits for the last one. */
    HANDLE file=CreateFileA("handle_lifetime.tmp",GENERIC_READ|GENERIC_WRITE,
                           0,NULL,CREATE_ALWAYS,FILE_FLAG_DELETE_ON_CLOSE,NULL);
    CHECK(file!=INVALID_HANDLE_VALUE);
    HANDLE fileCopy=Copy(file);
    DWORD count=0;
    char ch=0;
    CHECK(fileCopy && WriteFile(file,"AB",2,&count,NULL) && count==2);
    CHECK(SetFilePointer(fileCopy,0,NULL,FILE_BEGIN)==0);
    CHECK(ReadFile(file,&ch,1,&count,NULL) && count==1 && ch=='A');
    CHECK(ReadFile(fileCopy,&ch,1,&count,NULL) && count==1 && ch=='B');
    CHECK(CloseHandle(file));
    CHECK(GetFileAttributesA("handle_lifetime.tmp")!=INVALID_FILE_ATTRIBUTES);
    CHECK(CloseHandle(fileCopy));
    CHECK(GetFileAttributesA("handle_lifetime.tmp")==INVALID_FILE_ATTRIBUTES);
    HANDLE output=Copy(GetStdHandle(STD_OUTPUT_HANDLE));
    CHECK(output && WriteConsoleA(output,"CONSOLE_ALIAS_OK\n",17,&count,NULL) && count==17);
    CHECK(CloseHandle(output));
    puts("PASSED: shared HANDLE objects, alias wake, thread lifetime, abandoned mutex");
    return 0;
}
