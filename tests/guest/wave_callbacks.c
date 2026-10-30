/* Public authored PCM, real WinMM stdcall callbacks and kernel events. */
#include <windows.h>
#include <mmsystem.h>
__declspec(dllexport) volatile DWORD CallbackProbe[16];
#define P CallbackProbe
#define CHECK(x) do {if(!(x))ExitProcess(__LINE__+1000);} while(0)
static HWAVEOUT handle;
static WAVEHDR headers[4];
static short pcm[4][4]={{100,-200,300,-400},{11,-22,33,-44},{55,-66,77,-88},{99,-100,101,-102}};
static HANDLE gate,signalEvent;
static DWORD tls;
static volatile DWORD done,opened,closed,blocked,callbackId,callbackTeb;
static DWORD Teb(void){DWORD value;__asm__ volatile("movl %%fs:0x18,%0":"=r"(value));return value;}
static void Stage(DWORD n){P[3]=n;while(P[4]!=n)Sleep(0);}
static void WaitCount(volatile DWORD* p,DWORD n){while(*p<n)Sleep(0);CHECK(*p==n);}
static void CALLBACK Callback(HWAVEOUT wave,UINT msg,DWORD_PTR instance,DWORD_PTR first,DWORD_PTR second){
    DWORD id=GetCurrentThreadId(),teb=Teb();
    CHECK(instance==0x7654ab21 && !second && id!=P[11] && teb!=P[13]);
    CHECK(*(DWORD*)(teb+0x24)==id && *(DWORD*)(teb+0x18)==teb);
    if(!callbackId){callbackId=id;callbackTeb=teb;CHECK(!TlsGetValue(tls));CHECK(TlsSetValue(tls,(void*)0x2222));SetLastError(0x4444);}
    else {CHECK(callbackId==id && callbackTeb==teb && GetLastError()==0x4444);CHECK((DWORD)TlsGetValue(tls)==0x2222);SetLastError(0x4444);}
    P[7]=id;P[8]=teb;
    if(P[0]!=9)CHECK(wave==handle);
    if(msg==WOM_OPEN){CHECK(!first);++opened;P[10]=opened;
        if(P[0]==3)ExitProcess(37);
        if(P[0]==4)ExitThread(73);
        if(P[0]==9 && opened==1){blocked=1;CHECK(WaitForSingleObject(gate,INFINITE)==WAIT_OBJECT_0);}
    }else if(msg==WOM_CLOSE){CHECK(!first);++closed;P[9]=closed;}
    else {WAVEHDR* h=(WAVEHDR*)first;CHECK(msg==WOM_DONE && (h->dwFlags&WHDR_DONE) && !(h->dwFlags&WHDR_INQUEUE));
        if(!done){blocked=1;P[14]=1;CHECK(WaitForSingleObject(gate,INFINITE)==WAIT_OBJECT_0);}
        ++done;P[5]=done;
    }
}
void WINAPI start(void){
    DWORD mode=P[0],i;WAVEFORMATEX format={WAVE_FORMAT_PCM,2,48000,192000,4,16,0};
    HWAVEOUT bad=(HWAVEOUT)0x12345678;HANDLE mutex,readonly;
    P[11]=GetCurrentThreadId();P[13]=Teb();tls=TlsAlloc();CHECK(tls!=TLS_OUT_OF_INDEXES && TlsSetValue(tls,(void*)0x1111));
    gate=CreateEventA(NULL,TRUE,FALSE,NULL);CHECK(gate);P[12]=(DWORD)gate;
    CHECK(waveOutOpen(&bad,0 /* direct compatibility device */,&format,0,0,CALLBACK_FUNCTION)==MMSYSERR_INVALPARAM && (DWORD)bad==0x12345678);
    CHECK(waveOutOpen(&bad,0 /* direct compatibility device */,&format,0xfffffff0,0,CALLBACK_FUNCTION)==MMSYSERR_INVALPARAM && (DWORD)bad==0x12345678);
    mutex=CreateMutexA(NULL,FALSE,NULL);CHECK(mutex);
    CHECK(waveOutOpen(&bad,0 /* direct compatibility device */,&format,(DWORD)mutex,0,CALLBACK_EVENT)==MMSYSERR_INVALPARAM);CHECK(CloseHandle(mutex));
    signalEvent=CreateEventA(NULL,mode!=2,FALSE,NULL);CHECK(signalEvent);
    CHECK(DuplicateHandle(GetCurrentProcess(),signalEvent,GetCurrentProcess(),&readonly,SYNCHRONIZE,FALSE,0));
    CHECK(waveOutOpen(&bad,0 /* direct compatibility device */,&format,(DWORD)readonly,0,CALLBACK_EVENT)==MMSYSERR_INVALPARAM);CHECK(CloseHandle(readonly));
    SetLastError(0x3333);
    CHECK(waveOutOpen(&handle,0 /* direct compatibility device */,&format,(mode==1 || mode==2)?(DWORD)signalEvent:(DWORD)Callback,0x7654ab21,(mode==1 || mode==2)?CALLBACK_EVENT:CALLBACK_FUNCTION)==0);P[1]=(DWORD)handle;
    if(mode==1 || mode==2){CHECK(WaitForSingleObject(signalEvent,0)==WAIT_OBJECT_0);if(mode==2)CHECK(WaitForSingleObject(signalEvent,0)==WAIT_TIMEOUT);else CHECK(ResetEvent(signalEvent));}
    else WaitCount(&opened,1);
    if(mode==4){
        Stage(4); /* inspector confirms the actual service thread exit status */
        headers[0].lpData=(LPSTR)pcm[0];headers[0].dwBufferLength=8;CHECK(waveOutPrepareHeader(handle,&headers[0],32)==0);
        CHECK(waveOutWrite(handle,&headers[0],32)==MMSYSERR_ERROR);CHECK(waveOutClose(handle)==MMSYSERR_NOMEM);P[6]=1;ExitProcess(0);
    }
    if(mode==5){P[6]=1;ExitThread(55);}
    if(mode==9){
        while(!blocked){Sleep(0);}
        CHECK(waveOutClose(handle)==0);
        /* CLOSE plus 63 complete pairs and one OPEN fill the 128-record queue. */
        for(i=0;i<63;++i){CHECK(waveOutOpen(&handle,0 /* direct compatibility device */,&format,(DWORD)Callback,0x7654ab21,CALLBACK_FUNCTION)==0);CHECK(waveOutClose(handle)==0);}
        CHECK(waveOutOpen(&handle,0 /* direct compatibility device */,&format,(DWORD)Callback,0x7654ab21,CALLBACK_FUNCTION)==0);
        CHECK(waveOutClose(handle)==MMSYSERR_NOMEM);CHECK(opened==1 && !closed);CHECK(SetEvent(gate));
        WaitCount(&opened,65);WaitCount(&closed,64);CHECK(waveOutClose(handle)==0);WaitCount(&closed,65);
        CHECK((DWORD)TlsGetValue(tls)==0x1111);P[6]=1;ExitProcess(0);
    }
    for(i=0;i<4;++i){headers[i].lpData=(LPSTR)pcm[i];headers[i].dwBufferLength=8;headers[i].dwUser=0xdead0000+i;
        CHECK(waveOutPrepareHeader(handle,&headers[i],32)==0 && waveOutWrite(handle,&headers[i],32)==0);}
    P[2]=(DWORD)&headers[0];Stage(1);
    if(mode==1 || mode==2){
        CHECK(WaitForSingleObject(signalEvent,1000)==WAIT_OBJECT_0);CHECK(headers[1].dwFlags==(WHDR_PREPARED|WHDR_DONE));
        if(mode==1)CHECK(ResetEvent(signalEvent));else CHECK(WaitForSingleObject(signalEvent,0)==WAIT_TIMEOUT);
        /* The stream owns the event object; aliases still observe it. */
        CHECK(DuplicateHandle(GetCurrentProcess(),signalEvent,GetCurrentProcess(),&readonly,SYNCHRONIZE,FALSE,0));CHECK(CloseHandle(signalEvent));signalEvent=readonly;
        CHECK(waveOutReset(handle)==0);CHECK(WaitForSingleObject(signalEvent,1000)==WAIT_OBJECT_0);
        if(mode==1)CHECK(WaitForSingleObject(signalEvent,0)==WAIT_OBJECT_0);
    }else{
        while(!blocked)Sleep(0);
        if(mode==6){Stage(9);ExitProcess(99);} /* deliberate native abort inside callback */
        Stage(2);CHECK(!done);CHECK(waveOutReset(handle)==0);
        for(i=0;i<20;++i){Sleep(0);}
        CHECK(!done);CHECK(SetEvent(gate));WaitCount(&done,4);
        CHECK(waveOutWrite(handle,&headers[0],32)==0);Stage(3);WaitCount(&done,5);
    }
    for(i=0;i<4;++i){CHECK(headers[i].dwUser==0xdead0000+i);CHECK(waveOutUnprepareHeader(handle,&headers[i],32)==0);}
    if(mode==1 || mode==2){CHECK(waveOutClose(handle)==0);CHECK(WaitForSingleObject(signalEvent,0)==WAIT_OBJECT_0);CHECK(CloseHandle(signalEvent));}
    else{CHECK(waveOutClose(handle)==0);WaitCount(&closed,1);
        if(mode==7){CHECK(waveOutOpen(&handle,0 /* direct compatibility device */,&format,(DWORD)Callback,0x7654ab21,CALLBACK_FUNCTION)==0);WaitCount(&opened,2);CHECK(waveOutClose(handle)==0);WaitCount(&closed,2);}
        CHECK(GetLastError()==0x3333 && (DWORD)TlsGetValue(tls)==0x1111);
    }
    CHECK(CloseHandle(gate) && TlsFree(tls));P[6]=1;ExitProcess(0);
}
