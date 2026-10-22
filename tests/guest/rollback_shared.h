#pragma once
#define _WIN32_WINNT 0x0501
#include <windows.h>
typedef struct RollbackState {
    DWORD testing,failTop,failDep,keepNested,failure,count;
    DWORD events[256][5]; /* module, kind, reason, tid, reserved */
    HMODULE keeper;
} RollbackState;
#ifdef ROLLBACK_CONTROL
#define ROLLBACK_API __declspec(dllexport)
#else
#define ROLLBACK_API __declspec(dllimport)
#endif
ROLLBACK_API RollbackState* RollbackControl(void);
ROLLBACK_API void RollbackRecord(DWORD id,DWORD kind,DWORD reason,DWORD value,LPVOID reserved);
