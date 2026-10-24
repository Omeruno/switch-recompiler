/* Public x86 GDI -> actual BMP -> fresh-session reader. No private assets. */
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
__declspec(dllexport) volatile unsigned BitmapProbe;
#define CHECK(x) do{if(!(x)){printf("shell_bitmap FAIL %d\n",__LINE__);return 1;}}while(0)
int main(void) {
    int argc=0;LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);CHECK(argv && argc==3);
    unsigned char fileBytes[78]={0};DWORD count=0;HANDLE file;
    if(wcscmp(argv[1],L"write")==0) {
        BITMAPINFO info;BITMAPFILEHEADER header;void* bits=0;memset(&info,0,sizeof(info));memset(&header,0,sizeof(header));
        info.bmiHeader.biSize=40;info.bmiHeader.biWidth=3;info.bmiHeader.biHeight=-2;
        info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=24;info.bmiHeader.biSizeImage=24;
        HBITMAP bitmap=CreateDIBSection(0,&info,DIB_RGB_COLORS,&bits,0,0);CHECK(bitmap && bits);memset(bits,0,24);
        HDC dc=CreateCompatibleDC(0);CHECK(dc);HGDIOBJ old=SelectObject(dc,bitmap);CHECK(old);
        const COLORREF colors[6]={RGB(255,0,0),RGB(0,255,0),RGB(0,0,255),RGB(255,255,255),RGB(0,0,0),RGB(255,255,0)};
        for(int y=0;y<2;++y)for(int x=0;x<3;++x){CHECK(SetPixel(dc,x,y,colors[y*3+x])!=CLR_INVALID);CHECK(GetPixel(dc,x,y)==colors[y*3+x]);}
        header.bfType=0x4d42;header.bfSize=78;header.bfOffBits=54;CHECK(sizeof(header)==14);
        memcpy(fileBytes,&header,14);memcpy(fileBytes+14,&info.bmiHeader,40);memcpy(fileBytes+54,bits,24);
        CHECK(SelectObject(dc,old));CHECK(DeleteDC(dc));CHECK(DeleteObject(bitmap));
        file=CreateFileW(argv[2],GENERIC_WRITE,0,0,CREATE_NEW,0,0);CHECK(file!=INVALID_HANDLE_VALUE);
        CHECK(WriteFile(file,fileBytes,78,&count,0) && count==78);CHECK(FlushFileBuffers(file));CHECK(CloseHandle(file));
    } else {
        CHECK(wcscmp(argv[1],L"read")==0 || wcscmp(argv[1],L"read-cwd")==0);
        if(wcscmp(argv[1],L"read-cwd")==0){wchar_t cwd[512];CHECK(GetCurrentDirectoryW(512,cwd)>0 && wcscmp(cwd,L"C:\\TEMP")==0);
            CHECK(SHGetSpecialFolderPathW(0,cwd,CSIDL_PROGRAMS,FALSE) && wcscmp(cwd,L"C:\\Documents and Settings\\Player\\Start Menu\\Programs")==0);
            CHECK(SHGetSpecialFolderPathW(0,cwd,CSIDL_PROGRAM_FILES,FALSE) && wcscmp(cwd,L"C:\\Programs")==0);
            CHECK(GetEnvironmentVariableW(L"ProgramFiles",cwd,512)>0 && wcscmp(cwd,L"C:\\Programs")==0);}
        file=CreateFileW(argv[2],GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,0,0);CHECK(file!=INVALID_HANDLE_VALUE);
        CHECK(ReadFile(file,fileBytes,78,&count,0) && count==78);CHECK(CloseHandle(file));
    }
    CHECK(fileBytes[0]=='B' && fileBytes[1]=='M' && fileBytes[2]==78 && fileBytes[10]==54);
    CHECK(fileBytes[18]==3 && fileBytes[22]==254 && fileBytes[23]==255 && fileBytes[28]==24);
    static const unsigned char pixels[24]={0,0,255,0,255,0,255,0,0,0,0,0,255,255,255,0,0,0,0,255,255,0,0,0};
    CHECK(memcmp(fileBytes+54,pixels,24)==0);CHECK(LocalFree(argv)==0);
    BitmapProbe=1;puts("shell_bitmap: actual GDI pixels and persistent BMP bytes PASS");return 0;
}
