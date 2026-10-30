/* Authored public RIFF reader fixture. No game payload or private WAV. */
#include <windows.h>
#include <mmsystem.h>
__declspec(dllexport) volatile DWORD MmioProbe[4];
#define CHECK(x) do{if(!(x))ExitProcess(__LINE__);}while(0)
static BYTE wave[]={
'R','I','F','F',80,0,0,0,'W','A','V','E',
'J','U','N','K',3,0,0,0,1,2,3,0,
'f','m','t',' ',16,0,0,0,1,0,2,0,128,187,0,0,0,238,2,0,4,0,16,0,
'L','I','S','T',16,0,0,0,'I','N','F','O','I','N','A','M',3,0,0,0,'a','b','c',0,
'd','a','t','a',8,0,0,0,232,3,48,248,184,11,96,240};
static void Write(void){DWORD count=0;HANDLE f=CreateFileA("mmio-public.wav",GENERIC_WRITE,0,0,CREATE_ALWAYS,0,0);CHECK(f!=INVALID_HANDLE_VALUE);CHECK(WriteFile(f,wave,sizeof(wave),&count,0)&&count==sizeof(wave));CHECK(CloseHandle(f));}
void WINAPI start(void){
    HMMIO h;MMCKINFO riff={0},chunk={0},list={0},bad={0};BYTE data[32];MMIOINFO info={0};
    if(MmioProbe[0])CHECK(mmioClose((HMMIO)(ULONG_PTR)MmioProbe[0],0)==MMSYSERR_INVALHANDLE);
    Write();h=mmioOpenA("mmio-public.wav",0,MMIO_READ|MMIO_ALLOCBUF);CHECK(h);
    riff.fccType=mmioFOURCC('W','A','V','E');CHECK(mmioDescend(h,&riff,0,MMIO_FINDRIFF)==0 && riff.cksize==80 && riff.dwDataOffset==8);
    chunk.ckid=mmioFOURCC('f','m','t',' ');CHECK(mmioDescend(h,&chunk,&riff,MMIO_FINDCHUNK)==0 && chunk.cksize==16 && chunk.dwDataOffset==32);
    CHECK(mmioRead(h,(HPSTR)data,16)==16 && data[0]==1 && data[2]==2 && data[4]==128 && data[5]==187);
    CHECK(mmioAscend(h,&chunk,0)==0);
    list.fccType=mmioFOURCC('I','N','F','O');CHECK(mmioDescend(h,&list,&riff,MMIO_FINDLIST)==0 && list.cksize==16);
    CHECK(mmioDescend(h,&chunk,&list,0)==0 && chunk.cksize==3);
    CHECK(mmioRead(h,(HPSTR)data,1)==1 && data[0]=='a');CHECK(mmioRead(h,(HPSTR)data,2)==2 && data[0]=='b' && data[1]=='c');
    CHECK(mmioAscend(h,&chunk,0)==0 && mmioAscend(h,&list,0)==0);
    chunk.ckid=mmioFOURCC('d','a','t','a');CHECK(mmioDescend(h,&chunk,&riff,MMIO_FINDCHUNK)==0 && chunk.cksize==8);
    CHECK(mmioRead(h,(HPSTR)(ULONG_PTR)0xfffffff0,32)==-1);CHECK(mmioRead(h,(HPSTR)data,-1)==-1);
    CHECK(mmioRead(h,(HPSTR)data,32)==8 && data[0]==232 && data[1]==3 && data[6]==96 && data[7]==240);
    CHECK(mmioRead(h,(HPSTR)data,1)==0 && mmioAscend(h,&chunk,0)==0);
    bad=riff;bad.dwDataOffset=0xfffffff0;CHECK(mmioDescend(h,&chunk,&bad,0)==MMIOERR_INVALIDFILE);
    chunk.ckid=0x11223344;CHECK(mmioDescend(h,&chunk,&riff,MMIO_FINDCHUNK)==MMIOERR_CHUNKNOTFOUND && chunk.ckid==0x11223344);
    CHECK(mmioClose(h,0)==0 && mmioRead(h,(HPSTR)data,1)==-1 && mmioClose(h,0)==MMSYSERR_INVALHANDLE);
    h=mmioOpenW(L"mmio-public.wav",0,MMIO_READ);CHECK(h && mmioRead(h,(HPSTR)data,4)==4 && data[0]=='R');CHECK(mmioClose(h,0)==0);
    CHECK(!mmioOpenA("mmio-public.wav",&info,MMIO_WRITE) && info.wErrorRet==MMSYSERR_NOTSUPPORTED);
    wave[4]=255;wave[5]=255;wave[6]=255;wave[7]=255;Write();
    h=mmioOpenA("mmio-public.wav",0,MMIO_READ);CHECK(h);riff.ckid=0x55667788;
    CHECK(mmioDescend(h,&riff,0,0)==MMIOERR_INVALIDFILE && riff.ckid==0x55667788);CHECK(mmioClose(h,0)==0);
    h=mmioOpenA("mmio-public.wav",0,MMIO_READ);CHECK(h);MmioProbe[1]=(DWORD)(ULONG_PTR)h;MmioProbe[2]=1;
    if(MmioProbe[3])for(;;)Sleep(0); /* deliberate owning-handle abort */
    ExitProcess(0); /* intentionally leave HMMIO to Process-owned teardown */
}
