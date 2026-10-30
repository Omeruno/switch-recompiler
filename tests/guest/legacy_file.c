/* Public CRT-free legacy file contract; authored bytes only. */
#include <windows.h>
__declspec(dllexport) volatile DWORD LegacyFileProbe[4];
#define CHECK(x) do{if(!(x))ExitProcess(__LINE__);}while(0)
static int equal(const char* a,const char* b){while(*a && *a==*b){++a;++b;}return *a==*b;}
static void closeFile(HFILE h){CHECK(_lclose(h)==0);}
void WINAPI start(void){
    OFSTRUCT ofs;static char data[21000];HFILE h;DWORD mode=LegacyFileProbe[0];unsigned i;
    CHECK(sizeof(ofs)==136);
    if(mode==0){
        h=_lcreat("C:\\legacy.bin",0);CHECK(h!=HFILE_ERROR);
        for(i=0;i<20003;++i)data[i]=(char)(i*17+3);
        CHECK(_hwrite(h,data,20003)==20003);
        CHECK(_llseek(h,19001,FILE_BEGIN)==19001 && _lwrite(h,NULL,0)==0);
        CHECK(_llseek(h,20001,FILE_BEGIN)==20001 && _hwrite(h,NULL,0)==0);
        CHECK(FlushFileBuffers((HANDLE)(INT_PTR)h));closeFile(h);
    }
    CHECK(OpenFile("C:\\legacy.bin",&ofs,OF_EXIST|OF_SHARE_DENY_NONE)==1 && ofs.cBytes==136 && !ofs.nErrCode);
    CHECK(equal(ofs.szPathName,"C:\\legacy.bin"));
    h=OpenFile(NULL,&ofs,OF_REOPEN|OF_READ);CHECK(h!=HFILE_ERROR);
    CHECK(_lread(h,data,9001)==9001 && _hread(h,data+9001,12000)==11000);
    for(i=0;i<19001;++i)CHECK(data[i]==(char)(i*17+3));
    for(i=19001;i<20001;++i)CHECK(data[i]==0);
    CHECK(_lread(h,data,1)==0 && _hread(h,NULL,0)==0);
    CHECK(_llseek(h,-1,FILE_END)==20000 && _lread(h,data,1)==1 && data[0]==0);
    CHECK(_llseek(h,0,7)==HFILE_ERROR && GetLastError()==ERROR_INVALID_PARAMETER);
    CHECK(_llseek(h,0,FILE_CURRENT)==20001);
    CHECK(_llseek(h,-20002,FILE_CURRENT)==HFILE_ERROR && GetLastError()==ERROR_NEGATIVE_SEEK);
    CHECK(_hread(h,data,-1)==HFILE_ERROR && GetLastError()==ERROR_INVALID_PARAMETER);
    CHECK(_hwrite(h,data,-1)==HFILE_ERROR && GetLastError()==ERROR_INVALID_PARAMETER);
    CHECK(_lwrite(h,"x",1)==(UINT)HFILE_ERROR && GetLastError()==ERROR_ACCESS_DENIED);
    CHECK(_lwrite(h,NULL,0)==(UINT)HFILE_ERROR && GetLastError()==ERROR_ACCESS_DENIED);
    {HANDLE alias=NULL;DWORD got=0;
        CHECK(DuplicateHandle(GetCurrentProcess(),(HANDLE)(INT_PTR)h,GetCurrentProcess(),&alias,0,FALSE,DUPLICATE_SAME_ACCESS));
        closeFile(h);CHECK(_lread(h,data,1)==(UINT)HFILE_ERROR && GetLastError()==ERROR_INVALID_HANDLE);
        CHECK(SetFilePointer(alias,0,NULL,FILE_BEGIN)==0 && ReadFile(alias,data,1,&got,NULL) && got==1 && data[0]==3);
        CHECK(CloseHandle(alias));
    }
    CHECK(OpenFile("missing.dat",&ofs,OF_PARSE)==0 && equal(ofs.szPathName,"C:\\cwd\\missing.dat") && !ofs.nErrCode);
    CHECK(GetFileAttributesA("C:\\cwd\\missing.dat")==INVALID_FILE_ATTRIBUTES);
    CHECK(OpenFile("missing.dat",&ofs,OF_READ)==HFILE_ERROR && ofs.nErrCode==ERROR_FILE_NOT_FOUND);
    h=OpenFile("priority.dat",&ofs,OF_READ);CHECK(h!=HFILE_ERROR && _lread(h,data,1)==1 && data[0]=='A');closeFile(h);
    h=_lopen("priority.dat",OF_READ);CHECK(h!=HFILE_ERROR && _lread(h,data,1)==1 && data[0]=='B');closeFile(h);
    h=OpenFile("system.dat",&ofs,OF_READ);CHECK(h!=HFILE_ERROR && _lread(h,data,1)==1 && data[0]=='S');closeFile(h);
    CHECK(SetEnvironmentVariableA("PATH","C:\\env"));
    h=OpenFile("environment.dat",&ofs,OF_READ);CHECK(h!=HFILE_ERROR && _lread(h,data,1)==1 && data[0]=='P');closeFile(h);
    CHECK(OpenFile("C:\\delete.dat",&ofs,OF_CREATE)!=HFILE_ERROR);h=OpenFile(NULL,&ofs,OF_REOPEN|OF_WRITE);CHECK(h!=HFILE_ERROR);closeFile(h);
    /* The CREATE handle above is intentionally owned until process/session exit. */
    CHECK(OpenFile("C:\\delete.dat",&ofs,OF_DELETE)==1 && GetFileAttributesA("C:\\delete.dat")==INVALID_FILE_ATTRIBUTES);
    h=OpenFile("C:\\caf\xe9.dat",&ofs,OF_CREATE|OF_READWRITE);CHECK(h!=HFILE_ERROR);
    CHECK(_lwrite(h,"cp1252",6)==6 && FlushFileBuffers((HANDLE)(INT_PTR)h));closeFile(h);
    CHECK(OpenFile(NULL,&ofs,OF_REOPEN|OF_EXIST)==1 && equal(ofs.szPathName,"C:\\caf\xe9.dat"));
    CHECK(OpenFile("R:\\readonly.dat",&ofs,OF_CREATE)==HFILE_ERROR && GetLastError()==ERROR_WRITE_PROTECT && ofs.nErrCode==ERROR_WRITE_PROTECT);
    h=OpenFile("R:\\readonly.dat",&ofs,OF_READ);CHECK(h!=HFILE_ERROR && _lread(h,data,1)==1 && data[0]=='R');closeFile(h);
    CHECK(OpenFile("C:\\legacy.bin",&ofs,OF_READ|OF_SHARE_EXCLUSIVE)==HFILE_ERROR && GetLastError()==ERROR_NOT_SUPPORTED);
    CHECK(OpenFile("C:\\legacy.bin",&ofs,OF_VERIFY)==HFILE_ERROR && GetLastError()==ERROR_NOT_SUPPORTED);
    CHECK(OpenFile("C:\\legacy.bin",&ofs,OF_PROMPT)==HFILE_ERROR && GetLastError()==ERROR_NOT_SUPPORTED);
    CHECK(OpenFile("C:\\legacy.bin",&ofs,0x10000)==HFILE_ERROR && GetLastError()==ERROR_NOT_SUPPORTED);
    CHECK(OpenFile("C:\\legacy.bin",&ofs,OF_READWRITE|1)==HFILE_ERROR && GetLastError()==ERROR_INVALID_PARAMETER);
    CHECK(OpenFile("C:\\*.dat",&ofs,OF_CREATE)==HFILE_ERROR && GetLastError()==ERROR_INVALID_NAME);
    CHECK(OpenFile("Z:\\bad.dat",&ofs,OF_CREATE)==HFILE_ERROR && GetLastError()==ERROR_INVALID_DRIVE);
    CHECK(OpenFile((LPCSTR)(ULONG_PTR)0xffffffff,&ofs,OF_CREATE)==HFILE_ERROR && GetLastError()==ERROR_NOACCESS && ofs.nErrCode==ERROR_NOACCESS);
    CHECK(OpenFile("C:\\bad.dat",(LPOFSTRUCT)(ULONG_PTR)0xfffffff0,OF_CREATE)==HFILE_ERROR && GetLastError()==ERROR_NOACCESS);
    CHECK(_lcreat("C:\\bad.dat",FILE_ATTRIBUTE_HIDDEN)==HFILE_ERROR && GetLastError()==ERROR_NOT_SUPPORTED);
    h=_lopen("C:\\legacy.bin",OF_READ);CHECK(h!=HFILE_ERROR);LegacyFileProbe[2]=(DWORD)h;LegacyFileProbe[1]=1;
    if(mode==3){LegacyFileProbe[3]=1;Sleep(INFINITE);ExitProcess(200);}
    if(mode!=2)closeFile(h);
    ExitProcess(0);
}
