/* Public authored PCM and ordinary x86 WinMM/USER calls, no game payload. */
#include <windows.h>
#include <mmsystem.h>
__declspec(dllexport) volatile DWORD WaveProbe[10];
#define CHECK(x) do{if(!(x))ExitProcess(__LINE__);}while(0)
static short samples[5][4]={{1000,-2000,3000,-4000},{11,-22,33,-44},{55,-66,77,-88},{99,-100,101,-102},{123,-234,345,-456}};
static WAVEHDR headers[5],limitHeaders[60];
static HWND window;static HWAVEOUT handle;static volatile UINT doneCount,openCount,closeCount;
static volatile DWORD receiverReady,receiverQuit,pumpRequest,pumpAck;static HANDLE receiver;static DWORD doneAddress[8];
static void Received(UINT msg,WPARAM wp,LPARAM lp){
    CHECK((DWORD)wp==(DWORD)handle);
    if(msg==MM_WOM_OPEN){CHECK(!lp);++openCount;}
    if(msg==MM_WOM_CLOSE){CHECK(!lp);++closeCount;}
    if(msg==MM_WOM_DONE){WAVEHDR* h=(WAVEHDR*)lp;CHECK((h->dwFlags&WHDR_DONE) && !(h->dwFlags&WHDR_INQUEUE));CHECK(doneCount<8);doneAddress[doneCount++]=(DWORD)lp;}
}
static LRESULT CALLBACK WindowProc(HWND w,UINT msg,WPARAM wp,LPARAM lp){
    if(msg>=MM_WOM_OPEN && msg<=MM_WOM_DONE){Received(msg,wp,lp);return 0;}return DefWindowProcA(w,msg,wp,lp);
}
static DWORD WINAPI Receiver(LPVOID unused){
    MSG msg;(void)unused;PeekMessageA(&msg,NULL,0,0,PM_NOREMOVE);receiverReady=1;
    while(!receiverQuit){
        while(PeekMessageA(&msg,NULL,MM_WOM_OPEN,MM_WOM_DONE,PM_REMOVE)){CHECK(!msg.hwnd);Received(msg.message,msg.wParam,msg.lParam);}
        if(pumpAck!=pumpRequest){
            while(PeekMessageA(&msg,NULL,MM_WOM_OPEN,MM_WOM_DONE,PM_REMOVE)){CHECK(!msg.hwnd);Received(msg.message,msg.wParam,msg.lParam);}
            pumpAck=pumpRequest;
        }
        Sleep(0);
    }
    return 0;
}
static void Pump(DWORD mode){MSG msg;if(mode==2){DWORD request=++pumpRequest;while(pumpAck!=request)Sleep(0);CHECK(!PeekMessageA(&msg,NULL,MM_WOM_OPEN,MM_WOM_DONE,PM_REMOVE));return;}while(PeekMessageA(&msg,NULL,MM_WOM_OPEN,MM_WOM_DONE,PM_REMOVE)){if(mode==2 || mode==6){CHECK(!msg.hwnd);Received(msg.message,msg.wParam,msg.lParam);}else{CHECK(msg.hwnd==window);DispatchMessageA(&msg);}}}
static void Stage(DWORD stage){WaveProbe[4]=stage;while(WaveProbe[5]!=stage)Sleep(0);}
void WINAPI start(void){
    DWORD mode=WaveProbe[0],i;HWAVEOUT extra=(HWAVEOUT)(ULONG_PTR)0x12345678;WAVEFORMATEX format={WAVE_FORMAT_PCM,2,48000,192000,4,16,0};DWORD flags=mode==1?CALLBACK_WINDOW:(mode==2 || mode==6)?CALLBACK_THREAD:CALLBACK_NULL;DWORD target=0;MSG msg;
    PeekMessageA(&msg,NULL,0,0,PM_NOREMOVE);
    if(mode==1){WNDCLASSA cls={0};cls.lpfnWndProc=WindowProc;cls.lpszClassName="PublicWave";CHECK(RegisterClassA(&cls));window=CreateWindowA(cls.lpszClassName,"Public wave",0,0,0,1,1,NULL,NULL,NULL,NULL);CHECK(window);target=(DWORD)window;}
    if(mode==2){receiver=CreateThread(NULL,0,Receiver,NULL,0,&target);CHECK(receiver);while(!receiverReady)Sleep(0);}
    if(mode==6)target=GetCurrentThreadId();
    CHECK(waveOutOpen(&handle,WAVE_MAPPER,&format,target,0x76543210,flags)==0 && handle);WaveProbe[1]=(DWORD)handle;
    if(WaveProbe[7])CHECK(waveOutClose((HWAVEOUT)(ULONG_PTR)WaveProbe[7])==MMSYSERR_INVALHANDLE);
    CHECK(waveOutOpen(&extra,WAVE_MAPPER,&format,0,0,0)==MMSYSERR_ALLOCATED && (DWORD)extra==0x12345678);
    CHECK(!PlaySoundW(NULL,NULL,0) && GetLastError()==170);Pump(mode);CHECK(openCount==(mode==1 || mode==2 || mode==6));
    for(i=0;i<5;++i){headers[i].lpData=(LPSTR)samples[i];headers[i].dwBufferLength=sizeof(samples[i]);headers[i].dwUser=0xdead0000+i;
        CHECK(waveOutWrite(handle,&headers[i],sizeof(WAVEHDR))==WAVERR_UNPREPARED);
        CHECK(waveOutPrepareHeader(handle,&headers[i],sizeof(WAVEHDR))==0 && (headers[i].dwFlags&WHDR_PREPARED));CHECK(!(headers[i].dwFlags&WHDR_DONE));}
    for(i=0;i<60;++i){limitHeaders[i].lpData=(LPSTR)samples[4];limitHeaders[i].dwBufferLength=8;CHECK(waveOutPrepareHeader(handle,&limitHeaders[i],32)==(i<59?0:MMSYSERR_NOMEM));}
    for(i=0;i<59;++i)CHECK(waveOutUnprepareHeader(handle,&limitHeaders[i],32)==0);
    CHECK(waveOutPrepareHeader(handle,(LPWAVEHDR)(ULONG_PTR)0xfffffff0,32)==MMSYSERR_INVALPARAM);
    if(mode==5){
        WAVEHDR* dynamic=(WAVEHDR*)VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);CHECK(dynamic);dynamic->lpData=(LPSTR)samples[0];dynamic->dwBufferLength=8;
        CHECK(waveOutPrepareHeader(handle,dynamic,32)==0 && waveOutWrite(handle,dynamic,32)==0);WaveProbe[2]=(DWORD)dynamic;Stage(5);
        CHECK(waveOutReset(handle)==MMSYSERR_ERROR);CHECK(waveOutWrite(handle,dynamic,32)==MMSYSERR_ERROR);
        CHECK(waveOutClose(handle)==0);WaveProbe[6]=1;ExitProcess(0);
    }
    CHECK(waveOutWrite(handle,(LPWAVEHDR)(ULONG_PTR)0xfffffff0,32)==MMSYSERR_INVALPARAM);
    if(mode==7){
        CHECK(waveOutWrite(handle,&headers[0],32)==MMSYSERR_ERROR && headers[0].dwFlags==WHDR_PREPARED);
        Stage(7);CHECK(waveOutClose(handle)==MMSYSERR_ERROR && headers[0].dwFlags==WHDR_PREPARED);
        Stage(8);CHECK(waveOutReset(handle)==0 && headers[0].dwFlags==WHDR_PREPARED);
        CHECK(waveOutWrite(handle,&headers[0],32)==0);Stage(4);CHECK(headers[0].dwFlags&WHDR_DONE);
        CHECK(waveOutClose(handle)==0);WaveProbe[6]=1;ExitProcess(0);
    }
    for(i=0;i<4;++i)CHECK(waveOutWrite(handle,&headers[i],sizeof(WAVEHDR))==0 && (headers[i].dwFlags&WHDR_INQUEUE) && !(headers[i].dwFlags&WHDR_DONE));
    CHECK(waveOutWrite(handle,&headers[4],sizeof(WAVEHDR))==MMSYSERR_NOMEM && headers[4].dwFlags==WHDR_PREPARED);
    CHECK(waveOutWrite(handle,&headers[0],sizeof(WAVEHDR))==WAVERR_STILLPLAYING);
    CHECK(waveOutUnprepareHeader(handle,&headers[0],sizeof(WAVEHDR))==WAVERR_STILLPLAYING);CHECK(waveOutClose(handle)==WAVERR_STILLPLAYING);
    samples[0][0]=777;WaveProbe[2]=(DWORD)&headers[0];WaveProbe[3]=(DWORD)&headers[1];
    if(mode==4){Stage(9);ExitProcess(99);} /* inspector deliberately aborts parked guest */
    if(mode==6){
        Stage(6);for(i=0;i<4;++i)CHECK(headers[i].dwFlags==(WHDR_DONE|WHDR_PREPARED));Pump(mode);CHECK(doneCount==4);
        CHECK(waveOutWrite(handle,&headers[4],32)==0);Stage(4);CHECK(headers[4].dwFlags&WHDR_DONE);Pump(mode);CHECK(doneCount==5);
        for(i=0;i<5;++i)CHECK(waveOutUnprepareHeader(handle,&headers[i],32)==0);
        CHECK(waveOutClose(handle)==0);Pump(mode);CHECK(closeCount==1);WaveProbe[6]=1;ExitProcess(0);
    }
    Stage(1); /* release second buffer; no MM call needed before checking flags */
    CHECK(headers[1].dwFlags==(WHDR_DONE|WHDR_PREPARED) && headers[0].dwFlags==(WHDR_INQUEUE|WHDR_PREPARED));
    Pump(mode);CHECK(doneCount==(mode==1 || mode==2 || mode==6));if(doneCount)CHECK(doneAddress[0]==(DWORD)&headers[1]);
    CHECK(waveOutWrite(handle,&headers[4],sizeof(WAVEHDR))==0 && !(headers[4].dwFlags&WHDR_DONE));
    Stage(2);CHECK(waveOutReset(handle)==MMSYSERR_ERROR);CHECK(headers[0].dwFlags==(WHDR_INQUEUE|WHDR_PREPARED));Pump(mode);CHECK(doneCount==(mode==1 || mode==2 || mode==6));
    Stage(3);CHECK(waveOutReset(handle)==0);
    for(i=0;i<5;++i)CHECK((headers[i].dwFlags&WHDR_DONE) && !(headers[i].dwFlags&WHDR_INQUEUE));
    Pump(mode);CHECK(doneCount==((mode==1 || mode==2 || mode==6)?5:0));
    CHECK(waveOutWrite(handle,&headers[0],sizeof(WAVEHDR))==0);Stage(4);CHECK(headers[0].dwFlags&WHDR_DONE);
    Pump(mode);CHECK(doneCount==((mode==1 || mode==2 || mode==6)?6:0));
    for(i=0;i<5;++i){CHECK(waveOutUnprepareHeader(handle,&headers[i],sizeof(WAVEHDR))==0 && !(headers[i].dwFlags&WHDR_PREPARED));CHECK(headers[i].dwUser==0xdead0000+i);}
    CHECK(waveOutClose(handle)==0);Pump(mode);CHECK(closeCount==(mode==1 || mode==2 || mode==6));
    CHECK(waveOutClose(handle)==MMSYSERR_INVALHANDLE);CHECK(waveOutReset(handle)==MMSYSERR_INVALHANDLE);
    if(mode==2){receiverQuit=1;CHECK(WaitForSingleObject(receiver,1000)==WAIT_OBJECT_0);CHECK(CloseHandle(receiver));}
    WaveProbe[6]=1;ExitProcess(0);
}
