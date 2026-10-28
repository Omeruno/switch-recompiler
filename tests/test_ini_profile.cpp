#include "source/runtime/ini_profile.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <unistd.h>
#include <sys/stat.h>
#include <cerrno>
using namespace rt;
static std::string Bytes(const std::string& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
static void Store(const std::string& p,const std::string& bytes){std::ofstream f(p,std::ios::binary);f<<bytes;assert(f.good());}
struct Fault:FileCopyIo {
    enum Mode{ReadError,WriteError,ZeroWrite,FlushError,CloseError,RenameError,ShortIo} mode;
    bool interrupted=false;
    explicit Fault(Mode m):mode(m){}
    ssize_t Read(int fd,void* p,size_t n) override{if(mode==ReadError){errno=EIO;return -1;}return FileCopyIo::Read(fd,p,mode==ShortIo?std::min(n,size_t(7)):n);}
    ssize_t Write(int fd,const void* p,size_t n) override{if(mode==WriteError){errno=ENOSPC;return -1;}if(mode==ZeroWrite)return 0;
        if(mode==ShortIo && !interrupted){interrupted=true;errno=EINTR;return -1;}return FileCopyIo::Write(fd,p,mode==ShortIo?std::min(n,size_t(3)):n);}
    int Flush(int fd) override{if(mode==FlushError){errno=EIO;return -1;}return FileCopyIo::Flush(fd);}
    int Close(int fd) override{const int r=FileCopyIo::Close(fd);if(mode==CloseError){errno=EIO;return -1;}return r;}
    int Rename(const std::string& a,const std::string& b) override{if(mode==RenameError){errno=EIO;return -1;}return FileCopyIo::Rename(a,b);}
};
int main(){
    char temp[]="/tmp/sr-profile-XXXXXX";const auto dir=mkdtemp(temp);assert(dir);Vfs vfs;vfs.SetRoot(dir);
    const auto path=std::string(dir)+"/settings.ini";const std::string original="; keep comment\n [Game] \n Number = 0x2a \nText='hello there'\n\n[Other]\nunchanged=yes\n";Store(path,original);
    IniProfile ini;assert(!ini.Load(vfs,"C:\\SETTINGS.INI"));assert(ini.Read(u"game",u"NUMBER",u"bad")==u"0x2a");assert(ini.Read(u"Game",u"Text",{})==u"hello there");assert(ini.Read(u"none",u"none",u" default  ")==u" default");
    const auto section=std::u16string(u"Game"),key=std::u16string(u"Number"),value=std::u16string(u"73");
    for(auto mode:{Fault::ReadError,Fault::WriteError,Fault::ZeroWrite,Fault::FlushError,Fault::CloseError,Fault::RenameError}){Fault io(mode);assert(ini.Write(vfs,"C:\\settings.ini",section,&key,&value,&io));assert(Bytes(path)==original);}
    // Close failure specifically after writing, rather than at the load close.
    struct WriteClose:FileCopyIo{int count=0;int Close(int fd) override{const int r=FileCopyIo::Close(fd);if(++count==2){errno=EIO;return -1;}return r;}} close;
    assert(ini.Write(vfs,"C:\\settings.ini",section,&key,&value,&close));assert(Bytes(path)==original);
    Fault shortIo(Fault::ShortIo);assert(!ini.Write(vfs,"C:\\settings.ini",section,&key,&value,&shortIo));assert(!ini.Load(vfs,"C:\\settings.ini") && ini.Read(section,key,{})==value);assert(Bytes(path).find("; keep comment")!=std::string::npos && Bytes(path).find("unchanged=yes")!=std::string::npos);
    assert(!ini.Write(vfs,"C:\\settings.ini",section,&key,nullptr));assert(!ini.Load(vfs,"C:\\settings.ini") && ini.Read(section,key,u"missing")==u"missing");
    assert(!ini.Write(vfs,"C:\\settings.ini",section,nullptr,nullptr));assert(!ini.Load(vfs,"C:\\settings.ini") && ini.List(nullptr)==std::vector<std::u16string>{u"Other"});
    assert(vfs.Mount('R',dir,true));assert(ini.Write(vfs,"R:\\settings.ini",section,&key,&value)==19);
    assert(chmod(path.c_str(),0400)==0);assert(ini.Write(vfs,"C:\\settings.ini",section,&key,&value)==5);assert(chmod(path.c_str(),0600)==0);
    Store(path,std::string(IniProfile::MaxBytes+1,'x'));assert(ini.Load(vfs,"C:\\settings.ini")==223);
    Store(path,"[X]\nx="+std::string(IniProfile::MaxLine,'x'));assert(ini.Load(vfs,"C:\\settings.ini")==223);
    std::string unicode("\xff\xfe",2);for(char16_t c:std::u16string(u"[X]\r\nk=\u0416\r\n")){unicode.push_back(char(c));unicode.push_back(char(c>>8));}Store(path,unicode);assert(!ini.Load(vfs,"C:\\settings.ini"));assert(ini.Read(u"X",u"k",{})==u"\u0416");
    assert(!ini.Write(vfs,"C:\\settings.ini",u"X",&key,&value));assert(Bytes(path).substr(0,2)==std::string("\xff\xfe",2));
    Store(path,std::string("\xff\xfe\0\xd8",4));assert(ini.Load(vfs,"C:\\settings.ini")==1113);
    Store(path,std::string("[X]\nk=\xe9\n"));assert(ini.Load(vfs,"C:\\settings.ini")==1113);
    for(const auto& pair:{std::pair<const char16_t*,uint32_t>{u"-1",0xffffffffu},{u"0x2a",42},{u"4294967297",1},{u"42tail",42},{u"bad",0},{u"010",10}})assert(IniProfile::Integer(pair.first)==pair.second);
    assert(unlink(path.c_str())==0);assert(rmdir(dir)==0);
    puts("INI provider PASS (real VFS read/write/delete/encoding/bounds, short/EINTR I/O, read/write/zero/flush/close/rename failure preserves original, readonly mounts/files)");
}
