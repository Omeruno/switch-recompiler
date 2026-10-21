/* Stress cooperative wait-all deadlines, auto-reset event acquisition
   and HANDLE closure by another running guest thread. */
#include <windows.h>
#include <stdio.h>
static HANDLE first, second, closing;
static DWORD WINAPI PartialSignals(LPVOID unused) {
    (void)unused;
    for (int i=0;i<5;++i) {
        Sleep(2);
        if (!SetEvent(first)) return 10;
    }
    return 42;
}
static DWORD WINAPI CloseEvent(LPVOID unused) {
    (void)unused;
    Sleep(3);
    return CloseHandle(closing) ? 42 : 43;
}
int main(void) {
    first=CreateEventA(NULL,FALSE,FALSE,NULL);
    second=CreateEventA(NULL,FALSE,FALSE,NULL);
    if (!first || !second) return 1;
    HANDLE pair[2]={first,second};
    DWORD id=0,code=0;
    HANDLE w=CreateThread(NULL,0,PartialSignals,NULL,0,&id);
    if (!w) return 2;
    /* Repeated first-event signals must not keep restarting this timeout. */
    if (WaitForMultipleObjects(2,pair,TRUE,18)!=WAIT_TIMEOUT) {
        puts("WAIT_ALL_DEADLINE_NOT_PRESERVED"); return 3;
    }
    if (WaitForSingleObject(w,INFINITE)!=WAIT_OBJECT_0 ||
        !GetExitCodeThread(w,&code) || code!=42) return 4;
    /* Preflight must not consume the first auto-reset signal while second
       is not yet ready. */
    if (!SetEvent(first)) return 5;
    if (WaitForMultipleObjects(2,pair,TRUE,0)!=WAIT_TIMEOUT) return 6;
    if (WaitForSingleObject(first,0)!=WAIT_OBJECT_0) {
        puts("WAIT_ALL_PARTIAL_CONSUMPTION"); return 7;
    }
    if (!SetEvent(first) || !SetEvent(second)) return 8;
    if (WaitForMultipleObjects(2,pair,TRUE,0)!=WAIT_OBJECT_0) {
        puts("AUTO_RESET_WAIT_ALL_FAILED"); return 9;
    }
    if (WaitForSingleObject(first,0)!=WAIT_TIMEOUT ||
        WaitForSingleObject(second,0)!=WAIT_TIMEOUT) return 10;
    if (!CloseHandle(w) || !CloseHandle(first) || !CloseHandle(second)) return 11;
    closing=CreateEventA(NULL,TRUE,FALSE,NULL);
    if (!closing) return 12;
    HANDLE closer=CreateThread(NULL,0,CloseEvent,NULL,0,&id);
    if (!closer) return 13;
    if (WaitForSingleObject(closing,INFINITE)!=WAIT_FAILED) {
        puts("CLOSED_HANDLE_WAIT_STUCK_OR_SUCCEEDED"); return 14;
    }
    if (WaitForSingleObject(closer,INFINITE)!=WAIT_OBJECT_0 ||
        !GetExitCodeThread(closer,&code) || code!=42) return 15;
    if (!CloseHandle(closer)) return 16;
    /* Duplicate references survive closure of the original HANDLE. */
    HANDLE original=CreateEventA(NULL,TRUE,FALSE,NULL), duplicate=NULL;
    if (!original) return 17;
    if (!DuplicateHandle(GetCurrentProcess(),original,GetCurrentProcess(),
                        &duplicate,0,FALSE,DUPLICATE_SAME_ACCESS) ||
        duplicate==original) return 18;
    if (!CloseHandle(original) || !SetEvent(duplicate) ||
        WaitForSingleObject(duplicate,0)!=WAIT_OBJECT_0) return 19;
    if (!CloseHandle(duplicate) || CloseHandle(duplicate)) return 20;
    /* Supported file HANDLE copies have independent ownership. */
    HANDLE file=CreateFileA("dup_handle_test.tmp",GENERIC_READ|GENERIC_WRITE,
                            0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    if (file==INVALID_HANDLE_VALUE) return 21;
    HANDLE fileCopy=NULL;
    if (!DuplicateHandle(GetCurrentProcess(),file,GetCurrentProcess(),
                         &fileCopy,0,FALSE,DUPLICATE_SAME_ACCESS) || !fileCopy)
        return 22;
    if (!CloseHandle(file)) return 23;
    DWORD wrote=0;
    if (!WriteFile(fileCopy,"OK",2,&wrote,NULL) || wrote!=2) {
        puts("FILE_DUPLICATE_DID_NOT_SURVIVE_SOURCE_CLOSE");
        return 24;
    }
    if (!CloseHandle(fileCopy) || !DeleteFileA("dup_handle_test.tmp")) return 25;
    puts("PASSED: wait-all deadline, auto-reset atomicity and close wake");
    return 0;
}
