#include <windows.h>
#include "native_dispatch_kernel.h"
__declspec(dllexport) volatile DWORD NativeDispatchProbe[32];
#define P NativeDispatchProbe
#define CHECK(x) do{if(!(x))ExitProcess(__LINE__);}while(0)
static void Stage(DWORD stage){P[0]=stage;while(P[30]!=stage)Sleep(0);}
static LRESULT CALLBACK Window(HWND window,UINT message,WPARAM w,LPARAM l){
    if(message==WM_APP+1){DWORD b;P[7]=Kernel(256,&b)^b;P[8]=GetCurrentThreadId();
        if(P[31]==2){P[0]=9;SleepEx(INFINITE,TRUE);}return P[7];}
    return DefWindowProcW(window,message,w,l);
}
static void CALLBACK Apc(ULONG_PTR value){DWORD b;P[9]=Kernel(256,&b)^b;P[10]=GetCurrentThreadId();P[11]=value;}
static DWORD WINAPI Worker(void* argument){DWORD b;(void)argument;SetLastError(0x1234);P[12]=Kernel(512,&b)^b;P[13]=GetCurrentThreadId();CHECK(GetLastError()==0x1234);return 17;}
void WINAPI start(void){
    DWORD b,written,exitCode;HMODULE dll;HANDLE file,worker;HWND window;WNDCLASSW cls={0};
    P[15]=GetCurrentThreadId();CHECK(P[15]);
    dll=LoadLibraryA("native_dispatch.dll");CHECK(dll);
    {DWORD (WINAPI* result)(void)=(void*)GetProcAddress(dll,"NativeDllChecksum@0");
        if(!result)result=(void*)GetProcAddress(dll,"NativeDllChecksum");
        CHECK(result);P[6]=result();}
    cls.lpfnWndProc=Window;cls.lpszClassName=L"NativeDispatchPublic";cls.hInstance=GetModuleHandleA(NULL);CHECK(RegisterClassW(&cls));
    window=CreateWindowExW(0,cls.lpszClassName,L"Public dispatch",WS_OVERLAPPEDWINDOW,5,5,160,120,NULL,NULL,cls.hInstance,NULL);CHECK(window);
    CHECK((DWORD)SendMessageW(window,WM_APP+1,0,0)==P[7] && P[8]==P[15]);
    CHECK(QueueUserAPC(Apc,GetCurrentThread(),0x5678));CHECK(SleepEx(0,TRUE)==WAIT_IO_COMPLETION && P[10]==P[15] && P[11]==0x5678);
    SetLastError(0x4321);worker=CreateThread(NULL,0,Worker,NULL,0,NULL);CHECK(worker);
    CHECK(WaitForSingleObject(worker,INFINITE)==WAIT_OBJECT_0 && GetExitCodeThread(worker,&exitCode) && exitCode==17);
    CHECK(P[13] && P[13]!=P[15] && CloseHandle(worker));
    Stage(1);P[1]=Kernel(4096,&b);P[2]=b;
    Stage(2);P[3]=Kernel(12288,&b);P[4]=b;Stage(3);
    file=CreateFileA("native-dispatch-public.txt",GENERIC_WRITE,0,NULL,CREATE_ALWAYS,0,NULL);CHECK(file!=INVALID_HANDLE_VALUE);
    {const char text[]="native dispatch public file\r\n";CHECK(WriteFile(file,text,sizeof(text)-1,&written,NULL) && written==sizeof(text)-1);}
    CHECK(FlushFileBuffers(file) && CloseHandle(file) && FreeLibrary(dll));
    P[0]=4;if(P[31]==1){SleepEx(INFINITE,TRUE);ExitProcess(201);}ExitProcess(0);
}
