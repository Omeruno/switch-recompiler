#pragma once
#define _WIN32_WINNT 0x0501
#include <windows.h>
#define INITFAULT_CODE 0xe042f001u
#define INITFAULT_INNER 0xe042f002u
#define INITFAULT_OTHER 0xe042f003u
typedef struct InitFaultState {
    DWORD mode,phase,entry,afterFault,handlerCalls,handlerAfter,failure,count;
    DWORD events[128][5]; /* id, reason, tid, reserved, value */
    DWORD afterNested,workerBody,otherHandled;
    HANDLE go,ready,worker,suspended;
} InitFaultState;
#ifdef INITFAULT_CONTROL
#define FAULT_API __declspec(dllexport)
#else
#define FAULT_API __declspec(dllimport)
#endif
FAULT_API InitFaultState* InitFaultControl(void);
FAULT_API DWORD WINAPI InitFaultWorker(LPVOID unused);
FAULT_API void InitFaultRecord(DWORD id,DWORD reason,LPVOID reserved,DWORD value);
