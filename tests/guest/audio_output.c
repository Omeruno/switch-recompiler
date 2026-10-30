/* Public CRT-free device/PCM ownership test, no private game/resource. */
#include <windows.h>
#include <mmsystem.h>
__declspec(dllexport) volatile DWORD AudioProbe[4];
#define CHECK(x) do{if(!(x))ExitProcess(__LINE__);}while(0)
static BYTE wave[]={82,73,70,70,44,0,0,0,87,65,86,69,102,109,116,32,16,0,0,0,1,0,2,0,128,187,0,0,0,238,2,0,4,0,16,0,100,97,116,97,8,0,0,0,232,3,48,248,184,11,96,240};
void WINAPI start(void){
    const DWORD mode=AudioProbe[0];WAVEOUTCAPSA a;WAVEOUTCAPSW w;WAVEFORMATEX format={WAVE_FORMAT_PCM,2,48000,192000,4,16,0};HWAVEOUT handle=(HWAVEOUT)(ULONG_PTR)0x12345678;
    CHECK(sndPlaySoundA(NULL,0));CHECK(sndPlaySoundW(NULL,0));
    if(!mode){
        CHECK(waveOutGetNumDevs()==0);CHECK(waveOutGetDevCapsA(0,&a,sizeof(a))==MMSYSERR_NODRIVER);
        CHECK(waveOutOpen(NULL,WAVE_MAPPER,&format,0,0,WAVE_FORMAT_QUERY)==MMSYSERR_NODRIVER);
        CHECK(!PlaySoundA((LPCSTR)wave,NULL,SND_MEMORY|SND_ASYNC) && GetLastError()==6);
    }else{
        CHECK(waveOutGetNumDevs()==2);
        CHECK(waveOutGetDevCapsA(0,&a,sizeof(a))==0 && a.wChannels==2 && (a.dwFormats&WAVE_FORMAT_48S16));
        CHECK(waveOutGetDevCapsW(WAVE_MAPPER,&w,sizeof(w))==0 && w.szPname[0]=='P' && w.wChannels==2);
        CHECK(waveOutGetDevCapsA(4,&a,sizeof(a))==MMSYSERR_BADDEVICEID);
        CHECK(waveOutGetDevCapsA(0,(LPWAVEOUTCAPSA)(ULONG_PTR)0xffffffff,sizeof(a))==MMSYSERR_INVALPARAM);
        CHECK(waveOutOpen(&handle,WAVE_MAPPER,&format,0,0,WAVE_FORMAT_QUERY)==0 && (ULONG_PTR)handle==0x12345678);
        format.nSamplesPerSec=1;CHECK(waveOutOpen(NULL,WAVE_MAPPER,&format,0,0,WAVE_FORMAT_QUERY)==WAVERR_BADFORMAT);format.nSamplesPerSec=48000;
        CHECK(waveOutOpen(&handle,WAVE_MAPPER,&format,0,0,CALLBACK_TYPEMASK)==MMSYSERR_NOTSUPPORTED && (ULONG_PTR)handle==0x12345678);
        if(mode==2){
            CHECK(!PlaySoundA((LPCSTR)wave,NULL,SND_MEMORY|SND_ASYNC) && GetLastError()==1117);
            CHECK(waveOutGetNumDevs()==0);CHECK(PlaySoundW(NULL,NULL,0));CHECK(waveOutGetNumDevs()==2);
        }else{
            CHECK(!PlaySoundW(MAKEINTRESOURCEW(999),GetModuleHandleW(NULL),SND_RESOURCE|SND_ASYNC) && GetLastError()==1814);
            CHECK(!PlaySoundA((LPCSTR)wave,NULL,SND_MEMORY) && GetLastError()==ERROR_NOT_SUPPORTED);
            CHECK(!sndPlaySoundA((LPCSTR)wave,SND_MEMORY) && GetLastError()==ERROR_NOT_SUPPORTED);
            CHECK(!sndPlaySoundW((LPCWSTR)wave,SND_RESOURCE|SND_ASYNC) && GetLastError()==ERROR_NOT_SUPPORTED);
            CHECK(!PlaySoundA((LPCSTR)(ULONG_PTR)0xfffffff0,NULL,SND_MEMORY|SND_ASYNC) && GetLastError()==ERROR_NOACCESS);
            CHECK(PlaySoundW(MAKEINTRESOURCEW(101),GetModuleHandleW(NULL),SND_RESOURCE|SND_ASYNC|SND_NODEFAULT));
            CHECK(PlaySoundA((LPCSTR)wave,NULL,SND_MEMORY|SND_ASYNC));
            CHECK(!PlaySoundA((LPCSTR)wave,NULL,SND_MEMORY|SND_ASYNC|SND_NOSTOP) && GetLastError()==170);
            CHECK(sndPlaySoundW((LPCWSTR)wave,SND_MEMORY|SND_ASYNC));CHECK(sndPlaySoundA((LPCSTR)wave,SND_MEMORY|SND_ASYNC));
            wave[44]=7;CHECK(!PlaySoundA((LPCSTR)wave,NULL,SND_MEMORY|SND_ASYNC|SND_NOSTOP) && GetLastError()==170);
            AudioProbe[1]=1;while(!AudioProbe[2])Sleep(0);
            CHECK(waveOutGetNumDevs()==2);CHECK(sndPlaySoundA((LPCSTR)wave,SND_MEMORY|SND_ASYNC));
            CHECK(sndPlaySoundW(NULL,0));
            wave[22]=0;CHECK(!PlaySoundA((LPCSTR)wave,NULL,SND_MEMORY|SND_ASYNC) && GetLastError()==ERROR_INVALID_DATA);
        }
    }
    AudioProbe[3]=1;ExitProcess(0);
}
