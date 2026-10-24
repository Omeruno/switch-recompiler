#include "source/runtime/files.h"
#include "source/ui/shell_browser.h"
#include "source/runtime/file_copy_io.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <fcntl.h>
#include <sys/stat.h>
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"copy %d: %s errno=%d\n",__LINE__,#x,errno);std::exit(1);}}while(0)
static std::string Bytes(const fs::path& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
struct FaultIo:rt::FileCopyIo {
    enum class Mode {Short,Read,Write,ZeroWrite,Flush,CloseOut,CloseIn,Publish,Cleanup,Collision,Open,Create,LateDestination};
    Mode mode;int in=-1,out=-1,live=0,reads=0,writes=0,closes=0;bool interruptedRead=false,interruptedWrite=false;
    fs::path destination;
    explicit FaultIo(Mode m):mode(m){}
    int OpenRead(const std::string& p) override {
        if(mode==Mode::Open){errno=EACCES;return -1;}
        in=FileCopyIo::OpenRead(p);if(in>=0)++live;return in;
    }
    int CreateExclusive(const std::string& p) override {
        if(mode==Mode::Create){errno=ENOSPC;return -1;}
        if(mode==Mode::Collision){errno=EEXIST;return -1;}
        out=FileCopyIo::CreateExclusive(p);if(out>=0)++live;return out;
    }
    ssize_t Read(int fd,void* p,size_t n) override {
        if(!interruptedRead){interruptedRead=true;errno=EINTR;return -1;}
        if(mode==Mode::Read && reads++){errno=EIO;return -1;}
        return FileCopyIo::Read(fd,p,std::min(n,size_t(1009)));
    }
    ssize_t Write(int fd,const void* p,size_t n) override {
        if(!interruptedWrite){interruptedWrite=true;errno=EINTR;return -1;}
        if((mode==Mode::Write || mode==Mode::Cleanup) && writes++){errno=ENOSPC;return -1;}
        if(mode==Mode::ZeroWrite)return 0;
        return FileCopyIo::Write(fd,p,std::min(n,size_t(137)));
    }
    int Flush(int fd) override {
        if(mode==Mode::Flush){errno=EIO;return -1;}
        return FileCopyIo::Flush(fd);
    }
    int Close(int fd) override {
        ++closes;const int result=FileCopyIo::Close(fd);--live;
        if((mode==Mode::CloseOut && fd==out) || (mode==Mode::CloseIn && fd==in)){errno=EIO;return -1;}
        if(mode==Mode::LateDestination && fd==in)std::ofstream(destination)<<"late file";
        return result;
    }
    int Rename(const std::string& a,const std::string& b) override {
        if(mode==Mode::Publish){errno=EACCES;return -1;}
        return FileCopyIo::Rename(a,b);
    }
    int Remove(const std::string& p) override {
        if(mode==Mode::Cleanup){errno=EACCES;return -1;}
        return FileCopyIo::Remove(p);
    }
};
int main() {
    char pattern[]="/tmp/sr-copy-XXXXXX";CHECK(mkdtemp(pattern));const fs::path base=pattern,sd=base/"sd",root=sd/"drive_c",rom=base/"rom";
    struct Cleanup{fs::path p;~Cleanup(){fs::remove_all(p);}} cleanup{base};
    fs::create_directory(sd);fs::create_directory(rom);std::ofstream(rom/"locked")<<"packaged";
    rt::Vfs v;std::string error;CHECK(v.ConfigurePersistent(root.string(),sd.string(),rom.string(),error));
    std::string payload;for(int i=0;i<20003;++i)payload+=char(i%251);
    std::ofstream(root/"source.bin",std::ios::binary).write(payload.data(),payload.size());
    CHECK(v.CopyFile("c:\\SOURCE.BIN","C:\\копия.bin") && Bytes(root/"копия.bin")==payload);
    CHECK(!v.CopyFile("C:\\source.bin","C:\\копия.BIN") && errno==EEXIST);
    CHECK(!v.CopyFile("C:\\source.bin","s:\\drive_c\\SOURCE.BIN",false) && errno==EINVAL && Bytes(root/"source.bin")==payload);
    fs::create_hard_link(root/"source.bin",root/"hard.bin");
    CHECK(!v.CopyFile("C:\\source.bin","C:\\hard.bin",false) && errno==EINVAL && Bytes(root/"source.bin")==payload);
    fs::create_symlink(root/"source.bin",root/"linked");CHECK(!v.CopyFile("C:\\linked","C:\\bad") && errno==EACCES);
    CHECK(!v.CopyFile("C:\\source.bin","C:\\linked",false) && errno==EACCES);
    CHECK(!v.CopyFile("C:\\source.bin","R:\\bad") && errno==EROFS);
    CHECK(v.CopyFile("R:\\locked","C:\\from-rom") && Bytes(root/"from-rom")=="packaged");
    CHECK(!v.CopyFile("C:\\source.bin","Z:\\bad") && errno==ENODEV);
    CHECK(!v.CopyFile("C:\\missing","C:\\bad") && errno==ENOENT);
    CHECK(!v.CopyFile("C:\\TEMP","C:\\bad") && errno==EACCES);
    CHECK(!v.CopyFile("C:\\source.bin","C:\\TEMP",false) && errno==EACCES);
    CHECK(!v.CopyFile("C:\\source.bin","C:\\") && errno==EBUSY);
    CHECK(!v.CopyFile("C:\\source.bin","C:\\absent\\bad") && errno==ENOENT);
    size_t budget=0;CHECK(!v.CopyFile("c:\\SOURCE.BIN","C:\\bad",true,&budget) && errno==EOVERFLOW);
    std::ofstream(root/"empty");CHECK(v.CopyFile("C:\\empty","C:\\empty-copy") && fs::file_size(root/"empty-copy")==0);
    std::ofstream(root/"readonly")<<"keep";CHECK(chmod((root/"readonly").c_str(),0444)==0);
    CHECK(!v.CopyFile("C:\\source.bin","C:\\readonly",false) && errno==EACCES && Bytes(root/"readonly")=="keep");
    std::ofstream(root/"destination")<<"old data";
    using Mode=FaultIo::Mode;
    for(auto mode:{Mode::Read,Mode::Write,Mode::ZeroWrite,Mode::Flush,Mode::CloseOut,Mode::CloseIn,Mode::Publish,Mode::Open,Mode::Create,Mode::Collision}) {
        for(bool existing:{false,true}) {
            if(existing)std::ofstream(root/"destination")<<"old data";else fs::remove(root/"destination");
            FaultIo io(mode);CHECK(!v.CopyFile("C:\\source.bin","C:\\destination",false,nullptr,&io));
            const int expected=mode==Mode::Write || mode==Mode::Create?ENOSPC:
                mode==Mode::Open || mode==Mode::Publish?EACCES:mode==Mode::Collision?EEXIST:EIO;
            CHECK(errno==expected);
            CHECK(io.live==0 && Bytes(root/"source.bin")==payload);
            CHECK(existing?Bytes(root/"destination")=="old data":!fs::exists(root/"destination"));
            for(const auto& e:fs::directory_iterator(root))CHECK(e.path().filename().string().find(".sr-copy-")!=0);
        }
    }
    FaultIo success(Mode::Short);CHECK(v.CopyFile("C:\\source.bin","C:\\destination",false,nullptr,&success));
    CHECK(success.live==0 && success.closes==2 && Bytes(root/"destination")==payload);
    fs::remove(root/"late");FaultIo late(Mode::LateDestination);late.destination=root/"late";
    CHECK(!v.CopyFile("C:\\source.bin","C:\\late",true,nullptr,&late) && errno==EEXIST && Bytes(root/"late")=="late file" && late.live==0);
    FaultIo cleanupFailure(Mode::Cleanup);CHECK(!v.CopyFile("C:\\source.bin","C:\\destination",false,nullptr,&cleanupFailure) && errno==ENOSPC);
    CHECK(cleanupFailure.live==0 && Bytes(root/"destination")==payload);size_t residue=0;
    for(const auto& e:fs::directory_iterator(root))if(e.path().filename().string().find(".sr-copy-")==0){++residue;fs::remove(e.path());}
    CHECK(residue==1);
    ui::ShellBrowser browser;CHECK(browser.Configure(root.string(),sd.string(),rom.string(),error));
    CHECK(browser.Navigate("C:\\TEMP"));using Result=ui::ShellBrowser::MutationResult;
    CHECK(browser.CopyHere("C:\\source.bin","browser.bin")==Result::Applied && Bytes(root/"TEMP/browser.bin")==payload);
    CHECK(browser.Entries()[browser.Selected()].name=="browser.bin");
    CHECK(browser.CopyHere("C:\\source.bin","browser.bin")==Result::Failed && browser.LastError()==ui::ShellBrowser::Error::AlreadyExists);
    CHECK(browser.CopyHere("C:\\source.bin","..\\escape")==Result::Failed);
    CHECK(browser.Navigate("R:\\"));CHECK(browser.CopyHere("C:\\source.bin","bad")==Result::Failed && browser.LastError()==ui::ShellBrowser::Error::ReadOnly);
    puts("PASSED: real staged byte copy, aliases/readonly/types, short I/O/EINTR, read/write/flush/close/publish faults and preserved source/destination");
}
