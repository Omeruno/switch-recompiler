/* Public deterministic PE workload, not Microsoft Paint/Minesweeper. */
#include <windows.h>
__declspec(dllexport) volatile DWORD WorkloadVersion=1,WorkloadDone,WorkloadChecksum;
static void check(BOOL ok) {if(!ok)ExitProcess(10);}
void WINAPI start(void) {
    const char* cmd=GetCommandLineA();const char* mode=cmd;
    for(const char* p=cmd;*p;++p)if(*p==' ')mode=p+1;
    if(*mode=='c') {
        volatile DWORD x=0x12345678;
        for(DWORD i=0;i<20000;++i)x=(x*1664525u+1013904223u)^(i>>3);
        WorkloadChecksum=x;check(x==0x7a218cb8u);
    } else {
        HDC screen=GetDC(NULL),dc=CreateCompatibleDC(screen),tile=CreateCompatibleDC(screen);
        BITMAPINFO info={0};void* pixels=0;void* small=0;
        info.bmiHeader.biSize=40;info.bmiHeader.biWidth=480;info.bmiHeader.biHeight=-256;
        info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        HBITMAP bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&pixels,0,0);
        info.bmiHeader.biWidth=16;info.bmiHeader.biHeight=-32;
        HBITMAP tileBitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&small,0,0);
        check(screen && dc && tile && bitmap && tileBitmap && pixels && small);
        HGDIOBJ old=SelectObject(dc,bitmap),oldTile=SelectObject(tile,tileBitmap);
        for(int i=0;i<480*256;++i)((DWORD*)pixels)[i]=0xffffff;
        if(*mode=='t') {
            for(int i=0;i<512;++i)((DWORD*)small)[i]=i<256?0x204080:0xc04020;
            for(int pass=0;pass<8;++pass)for(int y=0;y<16;++y)for(int x=0;x<30;++x) {
                check(BitBlt(dc,x*16,y*16,16,16,tile,0,((x+y)&1)*16,SRCCOPY));
            }
            check(GetPixel(dc,8,8)==RGB(0x20,0x40,0x80));
            check(GetPixel(dc,24,8)==RGB(0xc0,0x40,0x20));
            WorkloadChecksum=0x204080;
        } else if(*mode=='s') {
            HPEN pen=CreatePen(PS_SOLID,1,RGB(0x20,0x80,0x40));check(pen!=NULL);
            HGDIOBJ oldPen=SelectObject(dc,pen);
            for(int pass=0;pass<16;++pass)for(int y=0;y<256;y+=2) {
                check(MoveToEx(dc,0,y,NULL));check(LineTo(dc,480,y));
            }
            SelectObject(dc,oldPen);check(DeleteObject(pen));
            check(GetPixel(dc,240,0)==RGB(0x20,0x80,0x40));
            check(GetPixel(dc,240,1)==RGB(255,255,255));WorkloadChecksum=0x208040;
        } else ExitProcess(11);
        check(BitBlt(screen,0,0,480,256,dc,0,0,SRCCOPY));
        SelectObject(dc,old);SelectObject(tile,oldTile);
        check(DeleteObject(bitmap));check(DeleteObject(tileBitmap));
        check(DeleteDC(dc));check(DeleteDC(tile));check(ReleaseDC(NULL,screen));
    }
    WorkloadDone=1;ExitProcess(0);
}
