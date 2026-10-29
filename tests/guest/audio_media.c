/* Repository-authored x86 WinMM controls/MCI client; MP3 signal is public. */
#include <windows.h>
#include <mmsystem.h>
__declspec(dllexport) volatile DWORD MediaProbe[16];
#define P MediaProbe
#define CHECK(x) do {if(!(x))ExitProcess(__LINE__+1000);}while(0)
static BYTE input[2][8192];static WAVEHDR headers[2];
static MCIDEVICEID device;static UINT successful,aborted;
static LRESULT CALLBACK WindowProc(HWND w,UINT msg,WPARAM wp,LPARAM lp){
    if(msg==MM_MCINOTIFY){CHECK((DWORD)lp==device);if(wp==MCI_NOTIFY_SUCCESSFUL)++successful;else if(wp==MCI_NOTIFY_ABORTED)++aborted;else CHECK(0);return 0;}
    return DefWindowProcA(w,msg,wp,lp);
}
static void Pump(void){MSG msg;while(PeekMessageA(&msg,NULL,MM_MCINOTIFY,MM_MCINOTIFY,PM_REMOVE))DispatchMessageA(&msg);}
static void Stage(DWORD n){P[3]=n;while(P[4]!=n)Sleep(0);}
void WINAPI start(void){
    DWORD mode=P[0];
    if(mode<4){
        WAVEFORMATEX fmt={WAVE_FORMAT_PCM,2,48000,192000,4,16,0};HWAVEOUT wave=NULL;MMTIME time={0};DWORD volume=0;UINT i,j;WAVEOUTCAPSA caps;
        if(mode==1){fmt.nChannels=1;fmt.nSamplesPerSec=44100;}
        if(mode==2){fmt.nChannels=1;fmt.nSamplesPerSec=22050;fmt.wBitsPerSample=8;}
        if(mode==3)fmt.nSamplesPerSec=96000;
        fmt.nBlockAlign=fmt.nChannels*fmt.wBitsPerSample/8;fmt.nAvgBytesPerSec=fmt.nSamplesPerSec*fmt.nBlockAlign;
        CHECK(waveOutGetDevCapsA(0,&caps,sizeof(caps))==0 && (caps.dwSupport&WAVECAPS_VOLUME) && !(caps.dwSupport&WAVECAPS_LRVOLUME));
        CHECK(waveOutOpen(NULL,WAVE_MAPPER,&fmt,0,0,WAVE_FORMAT_QUERY)==0);
        CHECK(waveOutOpen(&wave,WAVE_MAPPER,&fmt,0,0,CALLBACK_NULL)==0);P[1]=(DWORD)wave;
        for(i=0;i<2;++i){for(j=0;j<2048;++j){if(fmt.wBitsPerSample==8)input[i][j]=(BYTE)(128+j%64);else{short* samples=(short*)input[i];samples[j*fmt.nChannels]=1000;if(fmt.nChannels==2)samples[j*2+1]=-2000;}}
            headers[i].lpData=(LPSTR)input[i];headers[i].dwBufferLength=2048*fmt.nBlockAlign;CHECK(waveOutPrepareHeader(wave,&headers[i],32)==0 && waveOutWrite(wave,&headers[i],32)==0);}
        P[2]=(DWORD)headers;Stage(1);time.wType=TIME_SAMPLES;CHECK(waveOutGetPosition(wave,&time,sizeof(time))==0 && time.u.sample==240*fmt.nSamplesPerSec/48000);
        time.wType=TIME_MS;CHECK(waveOutGetPosition(wave,&time,sizeof(time))==0 && time.u.ms==(240*fmt.nSamplesPerSec/48000)*1000/fmt.nSamplesPerSec);
        time.wType=0xdead;CHECK(waveOutGetPosition(wave,&time,sizeof(time))==0 && time.wType==TIME_BYTES && time.u.cb==(240*fmt.nSamplesPerSec/48000)*fmt.nBlockAlign);
        CHECK(waveOutGetPosition(wave,(LPMMTIME)0xfffffff0,sizeof(time))==MMSYSERR_INVALPARAM);
        CHECK(waveOutSetVolume(wave,0x40004000)==0 && waveOutGetVolume(wave,&volume)==0 && volume==0x40004000);
        CHECK(waveOutSetVolume(wave,0xffff0000)==MMSYSERR_NOTSUPPORTED && waveOutGetVolume(wave,&volume)==0 && volume==0x40004000);
        CHECK(waveOutPause(wave)==0);Stage(2);time.wType=TIME_SAMPLES;CHECK(waveOutGetPosition(wave,&time,sizeof(time))==0 && time.u.sample==240*fmt.nSamplesPerSec/48000);CHECK(!(headers[0].dwFlags&WHDR_DONE));
        CHECK(waveOutRestart(wave)==0);Stage(3);CHECK(headers[0].dwFlags&WHDR_DONE);time.wType=TIME_SAMPLES;CHECK(waveOutGetPosition(wave,&time,sizeof(time))==0 && time.u.sample==P[12]*fmt.nSamplesPerSec/48000);
        CHECK(waveOutReset(wave)==0);time.wType=TIME_SAMPLES;CHECK(waveOutGetPosition(wave,&time,sizeof(time))==0 && !time.u.sample);CHECK(waveOutClose(wave)==0);CHECK(waveOutPause(wave)==MMSYSERR_INVALHANDLE);
    }else{
        WNDCLASSA cls={0};HWND window;MCI_OPEN_PARMSA open={0};MCI_OPEN_PARMSW wide={0};MCI_PLAY_PARMS play={0};MCI_STATUS_PARMS status={0};MCI_GETDEVCAPS_PARMS caps={0};MCI_SEEK_PARMS seek={0};MCI_SET_PARMS set={0};WCHAR error[64];
        cls.lpszClassName="PublicMedia";cls.lpfnWndProc=WindowProc;CHECK(RegisterClassA(&cls));window=CreateWindowA(cls.lpszClassName,"Media",0,0,0,1,1,NULL,NULL,NULL,NULL);CHECK(window);
        open.dwCallback=(DWORD)window;open.wDeviceID=0x1234;open.lpstrDeviceType="sequencer";open.lpstrElementName="C:\\tone.mp3";
        CHECK(mciSendCommandA(0,MCI_OPEN,MCI_OPEN_TYPE|MCI_OPEN_ELEMENT,(DWORD)&open)==MCIERR_UNSUPPORTED_FUNCTION && open.wDeviceID==0x1234);
        open.lpstrDeviceType="mpegvideo";open.lpstrAlias="tone";
        if(mode==7){CHECK(mciSendCommandA(0,MCI_OPEN,MCI_OPEN_TYPE|MCI_OPEN_ELEMENT,(DWORD)&open)==MCIERR_DEVICE_NOT_READY && open.wDeviceID==0x1234);P[6]=1;ExitProcess(0);}
        if(mode==5){wide.dwCallback=(DWORD)window;wide.lpstrDeviceType=L"mpegvideo";wide.lpstrElementName=L"C:\\tone.mp3";wide.lpstrAlias=L"tone";CHECK(mciSendCommandW(0,MCI_OPEN,MCI_OPEN_TYPE|MCI_OPEN_ELEMENT|MCI_OPEN_ALIAS|MCI_NOTIFY,(DWORD)&wide)==0);device=wide.wDeviceID;}
        else{CHECK(mciSendCommandA(0,MCI_OPEN,MCI_OPEN_TYPE|MCI_OPEN_ELEMENT|MCI_OPEN_ALIAS|MCI_NOTIFY,(DWORD)&open)==0);device=open.wDeviceID;}
        P[1]=device;Pump();CHECK(successful==1 && mciGetDeviceIDA("tone")==device && mciGetDeviceIDW(L"tone")==device);
        if(P[14])CHECK(mciSendCommandA(P[14],MCI_PAUSE,0,0)==MCIERR_INVALID_DEVICE_ID);
        CHECK(mciGetErrorStringW(MCIERR_UNSUPPORTED_FUNCTION,error,64) && error[0]);error[0]=0x1234;CHECK(!mciGetErrorStringW(0xffffffff,error,64) && error[0]==0x1234);
        status.dwItem=MCI_STATUS_LENGTH;CHECK(mciSendCommandA(device,MCI_STATUS,MCI_STATUS_ITEM,(DWORD)&status)==0 && status.dwReturn==8000);
        set.dwTimeFormat=MCI_FORMAT_SAMPLES;CHECK(mciSendCommandA(device,MCI_SET,MCI_SET_TIME_FORMAT,(DWORD)&set)==0);
        CHECK(mciSendCommandA(device,MCI_STATUS,MCI_STATUS_ITEM,(DWORD)&status)==0 && status.dwReturn==352800);
        set.dwTimeFormat=MCI_FORMAT_MILLISECONDS;CHECK(mciSendCommandA(device,MCI_SET,MCI_SET_TIME_FORMAT,(DWORD)&set)==0);
        caps.dwItem=MCI_GETDEVCAPS_HAS_AUDIO;CHECK(mciSendCommandA(device,MCI_GETDEVCAPS,MCI_GETDEVCAPS_ITEM,(DWORD)&caps)==0 && caps.dwReturn==1);
        caps.dwItem=MCI_GETDEVCAPS_HAS_VIDEO;CHECK(mciSendCommandA(device,MCI_GETDEVCAPS,MCI_GETDEVCAPS_ITEM,(DWORD)&caps)==0 && !caps.dwReturn);
        play.dwCallback=(DWORD)window;
        if(mode==6){P[3]=9;CHECK(mciSendCommandA(device,MCI_PLAY,MCI_WAIT,(DWORD)&play)==0);ExitProcess(99);}
        CHECK(mciSendCommandA(device,MCI_PLAY,MCI_NOTIFY,(DWORD)&play)==0);Stage(1);Pump();CHECK(successful==1);
        status.dwItem=MCI_STATUS_POSITION;CHECK(mciSendCommandA(device,MCI_STATUS,MCI_STATUS_ITEM,(DWORD)&status)==0 && status.dwReturn==50);
        set.dwTimeFormat=MCI_FORMAT_SAMPLES;CHECK(mciSendCommandA(device,MCI_SET,MCI_SET_TIME_FORMAT,(DWORD)&set)==0);
        CHECK(mciSendCommandA(device,MCI_STATUS,MCI_STATUS_ITEM,(DWORD)&status)==0 && status.dwReturn==2205);
        set.dwTimeFormat=MCI_FORMAT_MILLISECONDS;CHECK(mciSendCommandA(device,MCI_SET,MCI_SET_TIME_FORMAT,(DWORD)&set)==0);
        CHECK(mciSendCommandA(device,MCI_PAUSE,0,0)==0);Stage(2);CHECK(mciSendCommandA(device,MCI_STATUS,MCI_STATUS_ITEM,(DWORD)&status)==0 && status.dwReturn==50);
        CHECK(mciSendCommandA(device,MCI_RESUME,0,0)==0);CHECK(mciSendCommandA(device,MCI_STOP,0,0)==0);Pump();CHECK(aborted==1 && successful==1);
        set.dwTimeFormat=MCI_FORMAT_SAMPLES;CHECK(mciSendCommandA(device,MCI_SET,MCI_SET_TIME_FORMAT,(DWORD)&set)==0);
        seek.dwTo=176400;CHECK(mciSendCommandA(device,MCI_SEEK,MCI_TO,(DWORD)&seek)==0);
        set.dwTimeFormat=MCI_FORMAT_MILLISECONDS;CHECK(mciSendCommandA(device,MCI_SET,MCI_SET_TIME_FORMAT,(DWORD)&set)==0);CHECK(mciSendCommandA(device,MCI_STATUS,MCI_STATUS_ITEM,(DWORD)&status)==0 && status.dwReturn==4000);
        P[3]=3;CHECK(mciSendCommandA(device,MCI_PLAY,mode==4?MCI_WAIT:MCI_NOTIFY,(DWORD)&play)==0);
        if(mode!=4){while(successful<2){Pump();Sleep(0);}CHECK(successful==2);}
        status.dwItem=MCI_STATUS_MODE;CHECK(mciSendCommandA(device,MCI_STATUS,MCI_STATUS_ITEM,(DWORD)&status)==0 && status.dwReturn==MCI_MODE_STOP);
        CHECK(mciSendCommandA(device,MCI_CLOSE,0,0)==0 && !mciGetDeviceIDA("tone"));CHECK(mciSendCommandA(device,MCI_PAUSE,0,0)==MCIERR_INVALID_DEVICE_ID);
        CHECK(DestroyWindow(window));
    }
    P[6]=1;ExitProcess(0);
}
