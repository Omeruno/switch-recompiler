/* Public CRT-free multi-stream WinMM PCM/callback contract. */
#include <windows.h>
#include <mmsystem.h>
__declspec(dllexport) volatile DWORD MixerProbe[20];
#define P MixerProbe
#define CHECK(x) do{if(!(x))ExitProcess(__LINE__+1000);}while(0)
static HWAVEOUT waves[8];static WAVEHDR headers[8];static short a[512*2],longPcm[48000*5*2];static unsigned char b[441];
static HANDLE events[2],gate;static volatile DWORD opens[2],dones[2],closes[2];static DWORD callbackThread;
static void Stage(DWORD n){P[1]=n;while(P[2]!=n)Sleep(0);}
static void Message(DWORD message,DWORD wave,DWORD first){
    unsigned i=wave==(DWORD)waves[0]?0:1;CHECK(wave==(DWORD)waves[i]);
    if(message==WOM_OPEN){CHECK(!first);++opens[i];}
    if(message==WOM_DONE){CHECK(first==(DWORD)&headers[i] && (headers[i].dwFlags&WHDR_DONE) && !(headers[i].dwFlags&WHDR_INQUEUE));++dones[i];}
    if(message==WOM_CLOSE){CHECK(!first);++closes[i];}
}
static void CALLBACK Callback(HWAVEOUT wave,UINT message,DWORD_PTR instance,DWORD_PTR first,DWORD_PTR second){
    unsigned i=wave==waves[0]?0:1;CHECK(instance==0xabcd0000+i && !second && GetCurrentThreadId()!=P[8]);
    if(!callbackThread){callbackThread=GetCurrentThreadId();}CHECK(callbackThread==GetCurrentThreadId());P[9]=callbackThread;
    Message(message,(DWORD)wave,(DWORD)first);
    if(P[0]==10 && message==WOM_DONE && i==0){P[11]=1;CHECK(WaitForSingleObject(gate,INFINITE)==WAIT_OBJECT_0);}
}
static LRESULT CALLBACK WindowProc(HWND w,UINT msg,WPARAM first,LPARAM second){if(msg>=WOM_OPEN && msg<=WOM_DONE){Message(msg,first,second);return 0;}return DefWindowProcA(w,msg,first,second);}
static void Pump(void){MSG msg;while(PeekMessageA(&msg,NULL,WOM_OPEN,WOM_DONE,PM_REMOVE)){if(P[0]==3)DispatchMessageA(&msg);else{CHECK(!msg.hwnd);Message(msg.message,msg.wParam,msg.lParam);}}Sleep(0);}
static DWORD Position(unsigned i){MMTIME time={0};time.wType=TIME_SAMPLES;CHECK(waveOutGetPosition(waves[i],&time,12)==0 && time.wType==TIME_SAMPLES);return time.u.sample;}
void WINAPI start(void){
    DWORD mode=P[0],flags=(mode==0 || mode==10)?CALLBACK_FUNCTION:mode==1?CALLBACK_EVENT:mode==2?CALLBACK_THREAD:mode==3?CALLBACK_WINDOW:CALLBACK_NULL;
    WAVEFORMATEX fmt={WAVE_FORMAT_PCM,2,48000,192000,4,16,0},mono={WAVE_FORMAT_PCM,1,22050,22050,1,8,0};WAVEOUTCAPSA caps;DWORD volume=0,target=0;unsigned i;MSG message;HWND window=NULL;
    if(mode==8 || mode==9){HWAVEOUT out=(HWAVEOUT)0x12345678;CHECK(waveOutGetNumDevs()==(mode==8?1:0));CHECK(waveOutOpen(&out,1,&fmt,0,0,0)==MMSYSERR_NODRIVER && (DWORD)out==0x12345678);if(mode==8)CHECK(waveOutOpen(NULL,WAVE_MAPPER,&fmt,0,0,WAVE_FORMAT_QUERY)==0);P[3]=1;ExitProcess(0);}
    if(mode==10){gate=CreateEventA(NULL,TRUE,FALSE,NULL);CHECK(gate);}
    P[8]=GetCurrentThreadId();PeekMessageA(&message,NULL,0,0,PM_NOREMOVE);
    CHECK(waveOutGetNumDevs()==2 && waveOutGetDevCapsA(1,&caps,sizeof(caps))==0 && (caps.dwSupport&WAVECAPS_LRVOLUME));
    CHECK(waveOutOpen(NULL,1,&fmt,0,0,WAVE_FORMAT_QUERY)==0);
    if(mode==3){WNDCLASSA cls={0};cls.lpfnWndProc=WindowProc;cls.lpszClassName="MixerCallback";CHECK(RegisterClassA(&cls));window=CreateWindowA(cls.lpszClassName,"Mixer",0,0,0,1,1,NULL,NULL,NULL,NULL);CHECK(window);target=(DWORD)window;}
    if(mode==2)target=GetCurrentThreadId();
    if(mode==6){
        HWAVEOUT extra=(HWAVEOUT)0x12345678;
        for(i=0;i<8;++i){CHECK(waveOutOpen(&waves[i],i?1:WAVE_MAPPER,&fmt,0,0,0)==0 && waveOutPause(waves[i])==0);}
        CHECK(waveOutOpen(&extra,1,&fmt,0,0,0)==MMSYSERR_NOMEM && (DWORD)extra==0x12345678);
        for(i=0;i<48000*5;++i){longPcm[i*2]=(short)(1000+(int)(i%97)-48);longPcm[i*2+1]=(short)(-1000-(int)(i%89));}
        for(i=0;i<512;++i){a[i*2]=100;a[i*2+1]=-200;}
        for(i=0;i<8;++i){headers[i].lpData=(LPSTR)(i?a:longPcm);headers[i].dwBufferLength=i?sizeof(a):sizeof(longPcm);CHECK(waveOutPrepareHeader(waves[i],&headers[i],32)==0 && waveOutWrite(waves[i],&headers[i],32)==0);}
        for(i=0;i<8;++i)CHECK(waveOutRestart(waves[i])==0);
        P[6]=(DWORD)headers;CHECK(Position(0)==0);Stage(10);
        while(!(headers[0].dwFlags&WHDR_DONE))Sleep(0);
        CHECK(Position(0)==48000*5);for(i=1;i<8;++i)CHECK(Position(i)==512 && (headers[i].dwFlags&WHDR_DONE));
        for(i=0;i<8;++i){CHECK(waveOutClose(waves[i])==0);}P[3]=1;ExitProcess(0);
    }
    for(i=0;i<2;++i){
        if(mode==1){events[i]=CreateEventA(NULL,TRUE,FALSE,NULL);CHECK(events[i]);target=(DWORD)events[i];}
        if(mode==0 || mode==10)target=(DWORD)Callback;
        CHECK(waveOutOpen(&waves[i],i?1:WAVE_MAPPER,i?&mono:&fmt,target,0xabcd0000+i,flags)==0 && waveOutPause(waves[i])==0);
        P[4+i]=(DWORD)waves[i];if(mode==1){CHECK(WaitForSingleObject(events[i],0)==WAIT_OBJECT_0 && ResetEvent(events[i]));}
    }
    {HWAVEOUT direct=(HWAVEOUT)0x12345678;CHECK(waveOutOpen(&direct,0,&fmt,0,0,0)==MMSYSERR_ALLOCATED && (DWORD)direct==0x12345678);}
    if(P[10])CHECK(waveOutPause((HWAVEOUT)P[10])==MMSYSERR_INVALHANDLE);
    for(i=0;i<512;++i){a[i*2]=(short)(1000+i%17);a[i*2+1]=(short)(-2000-(int)(i%13));}
    for(i=0;i<441;++i)b[i]=(unsigned char)(128+i%7);
    headers[0].lpData=(LPSTR)a;headers[0].dwBufferLength=sizeof(a);headers[1].lpData=(LPSTR)b;headers[1].dwBufferLength=sizeof(b);P[6]=(DWORD)headers;
    for(i=0;i<2;++i)CHECK(waveOutPrepareHeader(waves[i],&headers[i],32)==0 && waveOutWrite(waves[i],&headers[i],32)==0);
    a[0]=777;b[0]=255; /* admitted snapshots must not borrow these guest arrays */
    if(mode==7){
        Stage(12);CHECK(waveOutRestart(waves[0])==0);{MMTIME time={0};CHECK(waveOutGetPosition(waves[0],&time,12)==MMSYSERR_ERROR);}Stage(13);
        CHECK(waveOutReset(waves[0])==MMSYSERR_ERROR && !(headers[0].dwFlags&WHDR_DONE) && !(headers[1].dwFlags&WHDR_DONE));Stage(14);
        CHECK(waveOutReset(waves[0])==0 && (headers[0].dwFlags&WHDR_DONE) && (headers[1].dwFlags&WHDR_DONE));CHECK(waveOutReset(waves[1])==0);
        CHECK(waveOutClose(waves[0])==0 && waveOutClose(waves[1])==0);P[3]=1;ExitProcess(0);
    }
    CHECK(waveOutRestart(waves[0])==0 && waveOutRestart(waves[1])==0 && Position(0)==0);Stage(mode==5?9:1);
    if(mode==5){Sleep(INFINITE);ExitProcess(200);}
    if(mode==4){
        CHECK(waveOutReset(waves[0])==MMSYSERR_ERROR && !(headers[0].dwFlags&WHDR_DONE) && !(headers[1].dwFlags&WHDR_DONE));Stage(11);
        CHECK(waveOutReset(waves[0])==0 && (headers[0].dwFlags&WHDR_DONE) && (headers[1].dwFlags&WHDR_DONE));
        {MMTIME time={0};CHECK(waveOutGetPosition(waves[1],&time,12)==MMSYSERR_ERROR);}CHECK(waveOutReset(waves[1])==0);
        CHECK(waveOutClose(waves[0])==0 && waveOutClose(waves[1])==0);P[3]=1;ExitProcess(0);
    }
    CHECK(Position(0)==100 && Position(1)==100*22050/48000);
    CHECK(waveOutPause(waves[0])==0 && Position(1)==100*22050/48000);Stage(2);
    CHECK(Position(0)==100 && Position(1)==300*22050/48000);
    CHECK(waveOutSetVolume(waves[0],0x00008000)==0 && waveOutGetVolume(waves[0],&volume)==0 && volume==0x8000);
    CHECK(waveOutGetVolume(waves[1],&volume)==0 && volume==0xffffffff);
    CHECK(waveOutRestart(waves[0])==0 && Position(0)==100);Stage(3);
    CHECK(Position(0)==137 && Position(1)==337*22050/48000);
    CHECK(waveOutReset(waves[0])==0 && (headers[0].dwFlags&WHDR_DONE) && !(headers[1].dwFlags&WHDR_DONE));
    CHECK(Position(0)==0 && Position(1)==337*22050/48000 && waveOutClose(waves[0])==0);
    CHECK(waveOutPause(waves[0])==MMSYSERR_INVALHANDLE && waveOutGetDevCapsA((UINT_PTR)waves[0],&caps,sizeof(caps))==MMSYSERR_BADDEVICEID);
    CHECK(Position(1)==337*22050/48000);Stage(4);
    while(!(headers[1].dwFlags&WHDR_DONE)){Pump();}CHECK(Position(1)==441);
    CHECK(waveOutClose(waves[1])==0);
    if(mode==1){CHECK(WaitForSingleObject(events[0],0)==WAIT_OBJECT_0 && WaitForSingleObject(events[1],0)==WAIT_OBJECT_0);CHECK(CloseHandle(events[0]) && CloseHandle(events[1]));}
    else{while(closes[0]!=1 || closes[1]!=1)Pump();CHECK(opens[0]==1 && opens[1]==1 && dones[0]==1 && dones[1]==1);}
    if(window){CHECK(DestroyWindow(window));}P[3]=1;ExitProcess(0);
}
