#include "source/ui/shell_preferences.h"
#include "source/ui/desktop_shell.h"
#include "source/ui/font.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
namespace fs=std::filesystem;
using Store=ui::ShellPreferenceStore;using Shell=ui::DesktopShell;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"preferences %d: %s errno=%d\n",__LINE__,#x,errno);std::exit(1);}}while(0)
static std::string Bytes(const fs::path& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
static void Write(const fs::path& p,const std::string& data){std::ofstream f(p,std::ios::binary);f.write(data.data(),data.size());}
struct Fault:rt::FileCopyIo {
    enum class Mode{Partial,Flush,Close,Read,Open,Short};Mode mode;int writer=-1,live=0;size_t written=0;
    explicit Fault(Mode m):mode(m){}
    int OpenRead(const std::string& p) override {int fd=FileCopyIo::OpenRead(p);if(fd>=0)++live;return fd;}
    int OpenReplace(const std::string& p) override {
        if(mode==Mode::Open){errno=EACCES;return -1;}
        writer=FileCopyIo::OpenReplace(p);if(writer>=0)++live;return writer;
    }
    ssize_t Read(int fd,void* p,size_t n) override {if(mode==Mode::Read){errno=EIO;return -1;}return FileCopyIo::Read(fd,p,std::min(n,size_t(17)));}
    ssize_t Write(int fd,const void* p,size_t n) override {
        if(mode==Mode::Partial && written>=11){errno=ENOSPC;return -1;}
        const auto r=FileCopyIo::Write(fd,p,std::min(n,size_t(11)));if(r>0)written+=size_t(r);return r;
    }
    int Flush(int fd) override {if(mode==Mode::Flush){errno=EIO;return -1;}return FileCopyIo::Flush(fd);}
    int Close(int fd) override {const auto result=FileCopyIo::Close(fd);--live;if(mode==Mode::Close && fd==writer){errno=EIO;return -1;}return result;}
};
static void Click(Shell& s,int target){const auto r=Shell::Bounds(target);s.PointerDown(r.left+10,r.top+10);s.PointerUp(r.left+10,r.top+10);}
static void Settings(Shell& s){s.ToggleStart();Click(s,Shell::Settings);CHECK(s.ActiveDialog()==Shell::Dialog::Preferences);}
int main(int argc,char** argv) {
    char pattern[]="/tmp/sr-prefs-XXXXXX";CHECK(mkdtemp(pattern));const fs::path base=pattern,root=base/"system",sd=base/"sd",rom=base/"rom";
    struct Cleanup{fs::path p;~Cleanup(){fs::remove_all(p);}} cleanup{base};fs::create_directory(sd);fs::create_directory(rom);
    rt::Vfs v;std::string error;CHECK(v.ConfigurePersistent(root.string(),sd.string(),rom.string(),error));
    Store store;ui::ShellPreferences loaded,p;p.sort=1;p.associations={{"txt","C:\\Programs\\reader.exe"}};
    CHECK(store.Load(v,loaded)==Store::Status::Missing && loaded.associations.empty());CHECK(store.Save(v,p));
    Store fresh;CHECK(fresh.Load(v,loaded)==Store::Status::Loaded && loaded.sort==1 && loaded.associations[0].application==p.associations[0].application);
    const auto directory=root/"Documents and Settings/Player/Application Data/SwitchRecompiler";const auto first=Bytes(directory/"prefs-0.bin");
    for(auto mode:{Fault::Mode::Partial,Fault::Mode::Flush,Fault::Mode::Close,Fault::Mode::Read,Fault::Mode::Open}) {
        fs::remove(directory/"prefs-1.bin");Write(directory/"prefs-0.bin",first);
        ui::ShellPreferences changed=p;changed.sort=2;Fault io(mode);CHECK(!store.Save(v,changed,&io) && io.live==0);
        CHECK(Bytes(directory/"prefs-0.bin")==first);Store recovered;const auto status=recovered.Load(v,loaded);
        CHECK(status==Store::Status::Loaded || status==Store::Status::Recovered);CHECK(loaded.associations.size()==1);
        if(mode==Fault::Mode::Partial || mode==Fault::Mode::Open || mode==Fault::Mode::Read)CHECK(loaded.sort==1);
    }
    fs::remove(directory/"prefs-1.bin");Write(directory/"prefs-0.bin",first);
    Fault shorts(Fault::Mode::Short);p.sort=2;CHECK(store.Save(v,p,&shorts) && shorts.live==0);
    CHECK(fresh.Load(v,loaded)==Store::Status::Loaded && loaded.sort==2);
    Write(directory/"prefs-1.bin","truncated");CHECK(fresh.Load(v,loaded)==Store::Status::Recovered && loaded.sort==1);
    Write(directory/"prefs-1.bin","SRPREF02future");CHECK(fresh.Load(v,loaded)==Store::Status::Unsupported);
    const auto future=Bytes(directory/"prefs-1.bin");CHECK(!fresh.Save(v,p) && Bytes(directory/"prefs-1.bin")==future);
    Write(directory/"prefs-0.bin","corrupt");Write(directory/"prefs-1.bin","corrupt");CHECK(fresh.Load(v,loaded)==Store::Status::Corrupt && loaded.associations.empty());
    CHECK(fresh.Save(v,p));CHECK(fresh.Load(v,loaded)==Store::Status::Recovered && loaded.sort==2);
    CHECK(v.Mount('C',root.string(),true));CHECK(!fresh.Save(v,p) && errno==EROFS);CHECK(v.Mount('C',root.string()));
    p.associations.push_back(p.associations[0]);CHECK(!fresh.Save(v,p) && errno==EINVAL);p.associations.pop_back();
    ui::ShellPreferences maximum;
    for(unsigned i=0;i<Store::MaxAssociations;++i)maximum.associations.push_back({"t"+std::to_string(i),"C:\\"+std::string(4089,'a')+".exe"});
    CHECK(fresh.Save(v,maximum));CHECK(fresh.Load(v,loaded)==Store::Status::Loaded && loaded.associations.size()==Store::MaxAssociations);
    CHECK(loaded.associations[0].application.size()==4096 && fs::file_size(directory/"prefs-1.bin")<=Store::MaxBytes);
    maximum.associations.push_back({"extra","C:\\reader.exe"});CHECK(!fresh.Save(v,maximum) && errno==EINVAL);
    auto damaged=Bytes(directory/"prefs-1.bin");damaged.back()^=1;Write(directory/"prefs-1.bin",damaged);
    CHECK(fresh.Load(v,loaded)==Store::Status::Recovered && loaded.sort==2);
    Write(directory/"prefs-0.bin",std::string(Store::MaxBytes+1,'x'));
    CHECK(fresh.Load(v,loaded)==Store::Status::Corrupt && loaded.associations.empty());
    // Genuine shell reconstruction, persisted sort/association and management UI.
    fs::remove_all(directory);std::ofstream(root/"Programs/reader.exe")<<"public path fixture";
    std::ofstream(root/"Programs/other.exe")<<"other path fixture";std::ofstream(root/"TEMP/документ.txt")<<"document";
    {
        ui::ShellBrowser browser;CHECK(browser.Configure(root.string(),sd.string(),rom.string(),error));
        ui::ShellLaunchRequest request;CHECK(browser.OpenWith("C:\\TEMP\\документ.txt","C:\\Programs\\reader.exe",request));browser.CycleSort();
    }
    ui::FontCache::SetDirectory(argc>1?argv[1]:"../../romfs/fonts");
    Shell shell;CHECK(shell.ConfigureFilesystem(root.string(),sd.string(),rom.string(),error));shell.Enter();
    CHECK(shell.Browser().Ordering()==ui::ShellBrowser::Sort::NameDescending && shell.Browser().ApplicationFor("a.txt")=="C:\\Programs\\reader.exe");
    Settings(shell);for(int i=0;i<20 && shell.Focus()!=Shell::AssociationChange;++i)shell.NextFocus();
    CHECK(shell.Focus()==Shell::AssociationChange);shell.Accept();CHECK(shell.PendingTextInput()==Shell::TextInput::AssociationApplication);
    shell.CompleteTextInput(true,"C:\\Programs\\other.exe");CHECK(shell.Browser().ApplicationFor("a.txt")=="C:\\Programs\\other.exe");
    if(argc>2) {
        ui::OwnedSurface pixels;pixels.Allocate(1280,720);shell.Render(pixels.s,u"14:32");FILE* f=std::fopen(argv[2],"wb");CHECK(f);std::fprintf(f,"P6\n1280 720\n255\n");
        for(int y=0;y<720;++y)for(int x=0;x<1280;++x){auto c=pixels.s.Get(x,y);unsigned char rgb[]={uint8_t(c>>16),uint8_t(c>>8),uint8_t(c)};CHECK(std::fwrite(rgb,1,3,f)==3);}
        CHECK(std::fclose(f)==0);
    }
    Shell another;CHECK(another.ConfigureFilesystem(root.string(),sd.string(),rom.string(),error));CHECK(another.Browser().ApplicationFor("a.txt")=="C:\\Programs\\other.exe");
    Click(shell,Shell::AssociationChange);shell.CompleteTextInput(true,"C:\\missing.exe");CHECK(shell.Browser().ApplicationFor("a.txt")=="C:\\Programs\\other.exe" && shell.Browser().LastError()==ui::ShellBrowser::Error::NotFound);
    Click(shell,Shell::AssociationForget);CHECK(shell.Browser().Associations().empty());
    ui::ShellBrowser removed;CHECK(removed.Configure(root.string(),sd.string(),rom.string(),error));CHECK(removed.ApplicationFor("a.txt").empty());
    Write(directory/"prefs-1.bin","SRPREF02future");Click(shell,Shell::PreferenceSort);CHECK(shell.Browser().PreferenceStatus()==Store::Status::SaveFailed && shell.Browser().PreferenceWarning());
    CHECK(Bytes(directory/"prefs-1.bin")=="SRPREF02future");
    puts("PASSED: dual-slot real preferences, corruption/future/I/O recovery, fresh shell restore and association management");
}
