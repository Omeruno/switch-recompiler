#define _WIN32_WINNT 0x0501
#define LIFE_ID 9
#include "lifecycle_shared.h"
#include "lifecycle_tls.h"
__declspec(dllimport) DWORD StartupAnchor(void);
DWORD WINAPI start(void) {
    LifeState* state=life_state();DWORD code=0,written;HANDLE output=GetStdHandle(STD_OUTPUT_HANDLE);
    static const char message[]="PASSED: cooperative startup, loader-serialized child, TLS and entry execution\n";
    ++state->startupEntry;
    if(StartupAnchor()!=8 || state->startupWait!=WAIT_TIMEOUT ||
       WaitForSingleObject(state->startupReady,2000)!=WAIT_OBJECT_0 ||
       WaitForSingleObject(state->startupThread,2000)!=WAIT_OBJECT_0 ||
       !GetExitCodeThread(state->startupThread,&code) || code!=17 || state->startupWorker!=1 || state->failure)return 41;
    WriteFile(output,message,sizeof(message)-1,&written,NULL);return 0;
}
