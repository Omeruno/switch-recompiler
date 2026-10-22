#include <windows.h>
#define LIFE_SEED 0x1234abcdUL
typedef struct { DWORD module, kind, reason, tid, value, reserved; } LifeEvent;
typedef struct {
    DWORD count, failure, busyTid, lateAttaches;
    LifeEvent events[256];
    DWORD exitTest,exitMode,exitOwner;
    HANDLE exitBlocked,exitSuspended,exitFinished;
    HANDLE exitDetachGate;
    DWORD startupMode,startupEntry,startupWorker,startupAfterExit,startupWait;
    HANDLE startupReady,startupThread,startupSuspended,startupTimer,startupMutex;
} LifeState;
#ifdef LIFE_STATE_IMPL
#define LIFE_IMPORT __declspec(dllexport)
#else
#define LIFE_IMPORT __declspec(dllimport)
#endif
LIFE_IMPORT LifeState* life_state(void);
LIFE_IMPORT void life_record(DWORD module,DWORD kind,DWORD reason,DWORD value,DWORD reserved);
