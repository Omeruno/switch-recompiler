/* Public F1 fixture: real mapped volumes and cross-session documents, no GUI. */
#include <windows.h>
#include <shlobj.h>
#include <commdlg.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <errno.h>
#include <io.h>
__declspec(dllexport) volatile DWORD FilesystemProbe;
static int failures;
#define CHECK(x) do{if(!(x)){printf("F1 FAIL line %d: %s err=%lu\n",__LINE__,#x,GetLastError());++failures;}}while(0)
int main(int argc,char**argv) {
    WCHAR cwd[MAX_PATH],module[MAX_PATH],path[MAX_PATH],profile[MAX_PATH],full[MAX_PATH],*part=NULL;
    char names[32];WCHAR wideNames[32];
    CHECK(argc==2);
    CHECK(GetCurrentDirectoryW(MAX_PATH,cwd)>0 && wcsncmp(cwd,L"S:\\Apps\\",8)==0);
    CHECK(GetModuleFileNameW(NULL,module,MAX_PATH)>0);
    swprintf(path,MAX_PATH,L"%ls\\filesystem_mounts.exe",cwd);
    CHECK(wcscmp(path,module)==0);
    CHECK(wcsstr(GetCommandLineW(),module)!=NULL);
    CHECK(GetLogicalDrives()==((1u<<2)|(1u<<17)|(1u<<18)));
    CHECK(GetLogicalDriveStringsA(sizeof(names),names)==12 && strcmp(names,"C:\\")==0 && strcmp(names+4,"R:\\")==0 && strcmp(names+8,"S:\\")==0 && names[12]==0);
    CHECK(GetLogicalDriveStringsW(32,wideNames)==12 && wcscmp(wideNames+4,L"R:\\")==0 && wideNames[12]==0);
    wideNames[0]=0x5555;CHECK(GetLogicalDriveStringsW(1,wideNames)==13 && wideNames[0]==0x5555);
    CHECK(GetDriveTypeW(L"R:\\")==DRIVE_CDROM && GetDriveTypeA("S:\\")==DRIVE_REMOVABLE && GetDriveTypeA("Z:\\")==DRIVE_NO_ROOT_DIR);
    ULARGE_INTEGER avail,total,freeBytes,wtotal;
    CHECK(GetDiskFreeSpaceExA("C:\\",&avail,&total,&freeBytes));
    CHECK(total.QuadPart>0 && avail.QuadPart<=freeBytes.QuadPart && freeBytes.QuadPart<=total.QuadPart);
    CHECK(GetDiskFreeSpaceExW(L"C:\\",NULL,&wtotal,NULL) && wtotal.QuadPart==total.QuadPart);
    DWORD sectors,bytes,freeClusters,clusters;
    CHECK(GetDiskFreeSpaceW(L"C:\\",&sectors,&bytes,&freeClusters,&clusters));
    CHECK(sectors>0 && bytes==512 && freeClusters<=clusters);
    CHECK(GetDiskFreeSpaceA("S:\\",&sectors,&bytes,&freeClusters,&clusters));
    DWORD flags;WCHAR fsname[32];
    CHECK(GetVolumeInformationW(L"R:\\",NULL,0,NULL,NULL,&flags,fsname,32));
    CHECK((flags&FILE_READ_ONLY_VOLUME)!=0 && wcscmp(fsname,L"ROMFS")==0);
    CHECK(SHGetSpecialFolderPathW(NULL,profile,CSIDL_PROFILE,FALSE));
    CHECK(wcscmp(profile,L"C:\\Documents and Settings\\Player")==0);
    CHECK(GetEnvironmentVariableW(L"USERPROFILE",full,MAX_PATH)>0 && wcscmp(profile,full)==0);
    CHECK(SHGetSpecialFolderPathW(NULL,path,CSIDL_PERSONAL,FALSE));
    CHECK(wcscmp(path,L"C:\\Documents and Settings\\Player\\My Documents")==0);
    CHECK(CreateFileA("Z:\\missing",GENERIC_READ,0,NULL,OPEN_EXISTING,0,NULL)==INVALID_HANDLE_VALUE && GetLastError()==ERROR_INVALID_DRIVE);
    CHECK(CreateFileW(L"Z:\\missing",GENERIC_WRITE,0,NULL,CREATE_ALWAYS,0,NULL)==INVALID_HANDLE_VALUE && GetLastError()==ERROR_INVALID_DRIVE);
    CHECK(CreateFileA("R:\\locked.txt",GENERIC_WRITE,0,NULL,CREATE_ALWAYS,0,NULL)==INVALID_HANDLE_VALUE && GetLastError()==ERROR_WRITE_PROTECT);
    CHECK(!DeleteFileW(L"R:\\locked.txt") && GetLastError()==ERROR_WRITE_PROTECT);
    CHECK(!CreateDirectoryA("R:\\bad",NULL) && GetLastError()==ERROR_WRITE_PROTECT);
    CHECK(!MoveFileA("R:\\locked.txt","C:\\moved.txt") && GetLastError()==ERROR_WRITE_PROTECT);
    // Shared mutation provider: real A/W and CRT entry points, not mock calls.
    CHECK(CreateDirectoryW(L"C:\\TEMP\\ops-\x6587",NULL));
    CHECK(!DeleteFileW(L"C:\\TEMP\\ops-\x6587") && GetLastError()==ERROR_ACCESS_DENIED);
    CHECK(GetFileAttributesW(L"C:\\TEMP\\ops-\x6587")&FILE_ATTRIBUTE_DIRECTORY);
    CHECK(MoveFileW(L"C:\\TEMP\\ops-\x6587",L"C:\\TEMP\\ops-renamed"));
    CHECK(CreateDirectoryA("C:\\TEMP\\ops-existing",NULL));
    CHECK(!MoveFileA("C:\\TEMP\\ops-renamed","C:\\TEMP\\ops-existing") && GetLastError()==ERROR_ALREADY_EXISTS);
    CHECK(remove("C:\\TEMP\\ops-renamed")==-1);
    CHECK(RemoveDirectoryA("C:\\TEMP\\ops-renamed"));
    CHECK(RemoveDirectoryA("C:\\TEMP\\ops-existing"));
    FILE* ops=fopen("C:\\TEMP\\ops-file.txt","wb");CHECK(ops!=NULL);
    if(ops){CHECK(fwrite("actual",1,6,ops)==6);CHECK(fclose(ops)==0);}
    CHECK(rename("C:\\TEMP\\ops-file.txt","C:\\TEMP\\ops-renamed.txt")==0);
    ops=fopen("C:\\TEMP\\ops-renamed.txt","rb");CHECK(ops!=NULL);
    if(ops){char content[7]={0};CHECK(fread(content,1,6,ops)==6 && strcmp(content,"actual")==0);fclose(ops);}
    CHECK(DeleteFileW(L"C:\\TEMP\\ops-renamed.txt"));
    FILE* ro=fopen("R:\\locked.txt","r+b");CHECK(ro==NULL && errno==EACCES);if(ro)fclose(ro);
    CHECK(_wremove(L"R:\\locked.txt")==-1);
    CHECK(SetCurrentDirectoryW(L"C:\\TEMP"));
    CHECK(GetFullPathNameW(L"C:\x6587\x4ef6.txt",MAX_PATH,full,&part)==14);
    CHECK(wcscmp(full,L"C:\\TEMP\\\x6587\x4ef6.txt")==0 && part && wcscmp(part,L"\x6587\x4ef6.txt")==0);
    CHECK(SetCurrentDirectoryW(cwd));
    // Real sibling DLL lookup must stay relative to this EXE, not C's root.
    HMODULE dll=LoadLibraryA("testdll.dll");CHECK(dll!=NULL);
    if(dll) {
        int (*add)(int,int)=(void*)GetProcAddress(dll,"add");CHECK(add && add(7,8)==15);
        CHECK(GetModuleFileNameW(dll,path,MAX_PATH)>0 && wcsncmp(path,cwd,wcslen(cwd))==0);
        CHECK(FreeLibrary(dll));
    }
    // Real modal Save/Open W round trip, clicked by the host input adapter.
    WCHAR dialogFile[MAX_PATH]=L"C:\\Documents and Settings\\Player\\My Documents\\dialog-\x6587\x4ef6.txt";
    WCHAR dialogTitle[MAX_PATH]={0};OPENFILENAMEW ofn;ZeroMemory(&ofn,sizeof(ofn));
    ofn.lStructSize=sizeof(ofn);ofn.lpstrFile=dialogFile;ofn.nMaxFile=MAX_PATH;
    ofn.lpstrFileTitle=dialogTitle;ofn.nMaxFileTitle=MAX_PATH;ofn.Flags=OFN_PATHMUSTEXIST;
    CHECK(GetSaveFileNameW(&ofn));
    CHECK(wcscmp(dialogFile,L"C:\\Documents and Settings\\Player\\My Documents\\dialog-\x6587\x4ef6.txt")==0);
    CHECK(wcscmp(dialogTitle,L"dialog-\x6587\x4ef6.txt")==0);
    CHECK(wcscmp(dialogFile+ofn.nFileOffset,dialogTitle)==0 && wcscmp(dialogFile+ofn.nFileExtension,L"txt")==0);
    HANDLE dialogOut=CreateFileW(dialogFile,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,0,NULL);
    CHECK(dialogOut!=INVALID_HANDLE_VALUE);
    if(dialogOut!=INVALID_HANDLE_VALUE){DWORD n=0;CHECK(WriteFile(dialogOut,"ok",2,&n,NULL) && n==2);CHECK(CloseHandle(dialogOut));}
    ofn.Flags=OFN_PATHMUSTEXIST|OFN_FILEMUSTEXIST;CHECK(GetOpenFileNameW(&ofn));
    wcscpy(dialogFile,L"R:\\locked.txt");ofn.Flags=OFN_PATHMUSTEXIST;
    CHECK(!GetSaveFileNameW(&ofn) && CommDlgExtendedError()==0);
    const WCHAR* shared=L"C:\\Documents and Settings\\Player\\My Documents\\\x6587\x4ef6.txt";
    const int writer=strcmp(argv[1],"writer")==0 || strcmp(argv[1],"abort")==0;
    HANDLE file=CreateFileW(shared,writer?GENERIC_WRITE:GENERIC_READ,0,NULL,writer?CREATE_ALWAYS:OPEN_EXISTING,0,NULL);
    CHECK(file!=INVALID_HANDLE_VALUE);
    if(file!=INVALID_HANDLE_VALUE) {
        DWORD n=0;char content[16]={};
        if(writer)CHECK(WriteFile(file,"shared bytes",12,&n,NULL) && n==12);
        else CHECK(ReadFile(file,content,12,&n,NULL) && n==12 && strcmp(content,"shared bytes")==0);
        CHECK(CloseHandle(file));
    }
    CHECK(!CopyFileW(shared,L"R:\\copy.txt",FALSE) && GetLastError()==ERROR_WRITE_PROTECT);
    CHECK(!CreateDirectoryW(L"R:\\bad",NULL) && GetLastError()==ERROR_WRITE_PROTECT);
    if(writer)CHECK(SetEnvironmentVariableA("F1_TRANSIENT","dirty"));
    else {char value[20];CHECK(GetEnvironmentVariableA("F1_TRANSIENT",value,sizeof(value))==0);}
    if(failures)return 1;
    FilesystemProbe=1;
    if(strcmp(argv[1],"abort")==0)Sleep(INFINITE);
    puts("PASSED: F1 guest mounted paths, A/W volumes, readonly APIs, DLL/argv/CWD/profile and persistent Unicode document");
    return 0;
}
