#include "lifecycle_shared.h"
#ifdef LIFE_TLS
#include "lifecycle_tls.h"
#endif
#if LIFE_ID == 2
__declspec(dllexport) DWORD LifeTopAnchor(void) { return 2; }
#endif
#ifdef LIFE_FAIL
__declspec(dllexport) DWORD LifeFailAnchor(void) { return 6; }
#endif
#ifdef LIFE_PARENT
__declspec(dllimport) DWORD LifeFailAnchor(void);
#endif
BOOL WINAPI DllMainCRTStartup(HINSTANCE module,DWORD reason,LPVOID reserved) {
    LifeState* state=life_state();
    life_record(LIFE_ID,2,reason,
#ifdef LIFE_TLS
        life_tls()[0],
#else
        0,
#endif
        (DWORD)reserved);
#ifdef LIFE_FAIL
    if (reason==DLL_PROCESS_ATTACH) return FALSE;
#endif
#ifdef LIFE_PARENT
    if (reason==DLL_PROCESS_ATTACH && LifeFailAnchor()!=6) state->failure|=64;
#endif
#ifdef LIFE_DISABLE
    if (reason==DLL_PROCESS_ATTACH && !DisableThreadLibraryCalls(module)) state->failure|=8;
#endif
    if (LIFE_ID==2 && reason==DLL_THREAD_ATTACH) {
        state->busyTid=GetCurrentThreadId();
        Sleep(3); // a peer's TLS/DllMain must not enter while this is parked
        state->busyTid=0;
    }
    if (LIFE_ID==4 && reason==DLL_PROCESS_ATTACH) {
        state->lateAttaches++;
        state->busyTid=GetCurrentThreadId();
        // Recursive loader entry must be permitted for the owning guest.
        if (!LoadLibraryA("lifecycle_top.dll")) state->failure|=16;
        Sleep(5); // another already-running worker races LoadLibrary
        state->busyTid=0;
    }
    (void)module;
    return TRUE;
}
