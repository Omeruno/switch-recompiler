#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <stdio.h>
#define CHECK(x) do { if (!(x)) { printf("FAIL named line %d error %lu\n",__LINE__,GetLastError()); return __LINE__; } } while (0)
static DWORD WINAPI Worker(LPVOID unused) {
    HANDLE event=OpenEventW(EVENT_MODIFY_STATE,FALSE,L"Local\\NamedEvent");
    HANDLE mutex=OpenMutexA(SYNCHRONIZE,FALSE,"NamedMutex");
    (void)unused;
    if(!event || !mutex) return 10;
    if(WaitForSingleObject(mutex,2000)!=WAIT_OBJECT_0 || !ReleaseMutex(mutex) || !SetEvent(event)) return 11;
    CloseHandle(event);CloseHandle(mutex);return 42;
}
int main(void) {
    SECURITY_ATTRIBUTES sa={sizeof(sa),NULL,TRUE};
    HANDLE event,alias,waiter,modifier,mutex,semaphore,other,worker,copy;
    HANDLE pair[2];
    DWORD flags,code;
    char longName[262];
    int i;
    event=CreateEventA(&sa,FALSE,FALSE,"NamedEvent");CHECK(event && GetLastError()==0);
    CHECK(GetHandleInformation(event,&flags) && flags==HANDLE_FLAG_INHERIT);
    alias=CreateEventW(NULL,TRUE,TRUE,L"Local\\NamedEvent");CHECK(alias && alias!=event && GetLastError()==ERROR_ALREADY_EXISTS);
    CHECK(WaitForSingleObject(alias,0)==WAIT_TIMEOUT);
    CHECK(GetHandleInformation(alias,&flags) && flags==0);
    waiter=OpenEventA(SYNCHRONIZE,TRUE,"NamedEvent");modifier=OpenEventW(EVENT_MODIFY_STATE,FALSE,L"NamedEvent");CHECK(waiter && modifier);
    CHECK(GetHandleInformation(waiter,&flags) && flags==HANDLE_FLAG_INHERIT);
    other=OpenEventW(GENERIC_WRITE,FALSE,L"NamedEvent");CHECK(other && SetEvent(other) && CloseHandle(other));
    CHECK(WaitForSingleObject(waiter,0)==WAIT_OBJECT_0);
    other=OpenEventA(MAXIMUM_ALLOWED,FALSE,"NamedEvent");CHECK(other && ResetEvent(other) && CloseHandle(other));
    CHECK(!OpenEventA(0x00400000,FALSE,"NamedEvent") && GetLastError()==ERROR_ACCESS_DENIED);
    CHECK(!SetEvent(waiter) && GetLastError()==ERROR_ACCESS_DENIED);
    CHECK(WaitForSingleObject(modifier,0)==WAIT_FAILED && GetLastError()==ERROR_ACCESS_DENIED);
    CHECK(SetEvent(modifier) && WaitForSingleObject(waiter,0)==WAIT_OBJECT_0 && WaitForSingleObject(alias,0)==WAIT_TIMEOUT);
    CHECK(!CreateMutexA(NULL,FALSE,"NamedEvent") && GetLastError()==ERROR_INVALID_HANDLE);
    CHECK(!OpenSemaphoreW(SEMAPHORE_ALL_ACCESS,FALSE,L"NamedEvent") && GetLastError()==ERROR_INVALID_HANDLE);
    CHECK(!OpenEventA(EVENT_ALL_ACCESS,FALSE,"namedevent") && GetLastError()==ERROR_FILE_NOT_FOUND);
    other=CreateEventW(NULL,TRUE,TRUE,L"Global\\NamedEvent");CHECK(other && GetLastError()==0);
    CHECK(WaitForSingleObject(other,0)==WAIT_OBJECT_0 && WaitForSingleObject(event,0)==WAIT_TIMEOUT && CloseHandle(other));
    CHECK(!CreateEventA(NULL,FALSE,FALSE,"Bogus\\Name") && GetLastError()==ERROR_INVALID_NAME);
    CHECK(!CreateEventW(NULL,FALSE,FALSE,L"Local\\") && GetLastError()==ERROR_INVALID_NAME);
    CHECK(!CreateEventA(NULL,FALSE,FALSE,"Local\\Global\\Name") && GetLastError()==ERROR_INVALID_NAME);
    for(i=0;i<261;i++) longName[i]='X';longName[261]=0;
    CHECK(!CreateEventA(NULL,FALSE,FALSE,longName) && GetLastError()==ERROR_FILENAME_EXCED_RANGE);
    CHECK(!OpenEventA(SYNCHRONIZE,FALSE,NULL) && GetLastError()==ERROR_INVALID_PARAMETER);
    CHECK(!CreateEventA(NULL,FALSE,FALSE,(LPCSTR)1) && GetLastError()==ERROR_INVALID_PARAMETER);
    // ANSI ACP 1252 and UTF-16 must refer to exactly the same name.
    other=CreateEventA(NULL,TRUE,TRUE,"Euro-\x80");CHECK(other);
    copy=OpenEventW(SYNCHRONIZE,FALSE,L"Euro-\x20ac");CHECK(copy && WaitForSingleObject(copy,0)==WAIT_OBJECT_0);
    CHECK(CloseHandle(other) && CloseHandle(copy));
    other=CreateEventW(NULL,TRUE,TRUE,L"Unicode-\x0416-\xd83d\xde00");CHECK(other);
    copy=OpenEventW(SYNCHRONIZE,FALSE,L"Unicode-\x0416-\xd83d\xde00");CHECK(copy && CloseHandle(other) && CloseHandle(copy));
    mutex=CreateMutexA(NULL,TRUE,"NamedMutex");CHECK(mutex);
    other=CreateMutexW(NULL,TRUE,L"Local\\NamedMutex");CHECK(other && GetLastError()==ERROR_ALREADY_EXISTS);
    CHECK(ReleaseMutex(mutex)); // Reopening with initialOwner must not acquire again.
    CHECK(!ReleaseMutex(other) && GetLastError()==ERROR_NOT_OWNER);
    worker=CreateThread(NULL,0,Worker,NULL,0,NULL);CHECK(worker);
    CHECK(WaitForSingleObject(waiter,2000)==WAIT_OBJECT_0 && WaitForSingleObject(worker,2000)==WAIT_OBJECT_0);
    CHECK(GetExitCodeThread(worker,&code) && code==42 && CloseHandle(worker));
    CHECK(CloseHandle(mutex) && CloseHandle(other));
    semaphore=CreateSemaphoreA(NULL,1,3,"NamedSemaphore");CHECK(semaphore);
    other=CreateSemaphoreW(NULL,0,1,L"NamedSemaphore");CHECK(other && GetLastError()==ERROR_ALREADY_EXISTS);
    CHECK(ReleaseSemaphore(other,2,NULL)); // Original maxCount=3 remains.
    CHECK(WaitForSingleObject(other,0)==WAIT_OBJECT_0);
    pair[0]=event;pair[1]=OpenSemaphoreA(SEMAPHORE_MODIFY_STATE,FALSE,"NamedSemaphore");CHECK(pair[1]);
    CHECK(SetEvent(event));
    CHECK(WaitForMultipleObjects(2,pair,TRUE,0)==WAIT_FAILED && GetLastError()==ERROR_ACCESS_DENIED);
    CHECK(WaitForMultipleObjectsEx(2,pair,TRUE,0,TRUE)==WAIT_FAILED && GetLastError()==ERROR_ACCESS_DENIED);
    CHECK(MsgWaitForMultipleObjectsEx(2,pair,0,QS_POSTMESSAGE,MWMO_WAITALL)==WAIT_FAILED && GetLastError()==ERROR_ACCESS_DENIED);
    CHECK(WaitForSingleObject(event,0)==WAIT_OBJECT_0 && WaitForSingleObject(semaphore,0)==WAIT_OBJECT_0);
    CHECK(WaitForSingleObject(semaphore,0)==WAIT_OBJECT_0 && WaitForSingleObject(semaphore,0)==WAIT_TIMEOUT);
    CHECK(CloseHandle(pair[1]) && CloseHandle(semaphore) && CloseHandle(other));
    CHECK(DuplicateHandle(GetCurrentProcess(),event,GetCurrentProcess(),&copy,0,FALSE,DUPLICATE_SAME_ACCESS));
    CHECK(CloseHandle(event) && CloseHandle(alias) && CloseHandle(waiter) && CloseHandle(modifier));
    other=OpenEventA(EVENT_ALL_ACCESS,FALSE,"NamedEvent");CHECK(other && CloseHandle(other));
    CHECK(CloseHandle(copy));
    CHECK(!OpenEventW(SYNCHRONIZE,FALSE,L"NamedEvent") && GetLastError()==ERROR_FILE_NOT_FOUND);
    event=CreateEventA(NULL,TRUE,TRUE,"NamedEvent");CHECK(event && GetLastError()==0);
    CHECK(WaitForSingleObject(event,0)==WAIT_OBJECT_0 && WaitForSingleObject(event,0)==WAIT_OBJECT_0 && CloseHandle(event));
    puts("PASSED: named A/W sync identity, type/session namespaces, ACP/Unicode, initialization, lifetime and restricted atomic waits");
    return 0;
}
