/* Guest x86 contention and recursion regression for Win32 critical sections. */
#include <windows.h>
#include <stdio.h>
static CRITICAL_SECTION lock;
static volatile LONG phase;
static volatile LONG protectedValue;
static DWORD WINAPI Worker(LPVOID ignored) {
    (void)ignored;
    if (TryEnterCriticalSection(&lock)) return 10; /* main owns it */
    phase = 1;
    EnterCriticalSection(&lock); /* must park, not steal the lock */
    if (protectedValue != 5) return 11;
    protectedValue = 9;
    LeaveCriticalSection(&lock);
    return 42;
}
int main(void) {
    DWORD id = 0, exitCode = 0;
    InitializeCriticalSection(&lock);
    EnterCriticalSection(&lock);
    EnterCriticalSection(&lock); /* recursion is legal */
    protectedValue = 5;
    HANDLE worker = CreateThread(NULL, 0, Worker, NULL, 0, &id);
    if (!worker) { puts("CREATE_FAILED"); return 1; }
    for (int spins = 0; spins < 1000000 && phase == 0; ++spins)
        SwitchToThread();
    if (phase != 1 || protectedValue != 5) {
        puts("TRY_ENTER_OWNERSHIP_FAILED"); return 2;
    }
    LeaveCriticalSection(&lock); /* still owned recursively */
    if (WaitForSingleObject(worker, 0) != WAIT_TIMEOUT) {
        puts("PREMATURE_LOCK_TRANSFER"); return 3;
    }
    LeaveCriticalSection(&lock); /* wakes worker */
    if (WaitForSingleObject(worker, INFINITE) != WAIT_OBJECT_0 ||
        !GetExitCodeThread(worker, &exitCode) || exitCode != 42 ||
        protectedValue != 9) {
        puts("CRITICAL_SECTION_TRANSFER_FAILED"); return 4;
    }
    if (!CloseHandle(worker)) return 5;
    DeleteCriticalSection(&lock);
    puts("PASSED: critical-section ownership, recursion, contention and wake");
    return 0;
}
