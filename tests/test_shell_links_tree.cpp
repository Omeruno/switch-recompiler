#include "source/ui/desktop_shell.h"
#include "source/ui/folder_tree.h"
#include "source/ui/font.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
using Shell=ui::DesktopShell;namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"links/tree %d: %s errno=%d\n",__LINE__,#x,errno);std::exit(1);}}while(0)
struct Fault:rt::FileCopyIo {
    enum class Mode{Write,Flush,Close,Rename,Short};Mode mode;int live=0;explicit Fault(Mode m):mode(m){}
    int CreateExclusive(const std::string& p) override {int fd=FileCopyIo::CreateExclusive(p);if(fd>=0)++live;return fd;}
    ssize_t Write(int fd,const void* p,size_t n) override {if(mode==Mode::Write){errno=ENOSPC;return -1;}return FileCopyIo::Write(fd,p,std::min(n,size_t(7)));}
    int Flush(int fd) override {if(mode==Mode::Flush){errno=EIO;return -1;}return FileCopyIo::Flush(fd);}
    int Close(int fd) override {const auto r=FileCopyIo::Close(fd);--live;if(mode==Mode::Close){errno=EIO;return -1;}return r;}
    int Rename(const std::string& a,const std::string& b) override {if(mode==Mode::Rename){errno=EIO;return -1;}return FileCopyIo::Rename(a,b);}
};
static void Click(Shell& s,int target){auto r=Shell::Bounds(target);s.PointerDown(r.left+5,r.top+5);s.PointerUp(r.left+5,r.top+5);}
static void Action(Shell& s,int action) {
    Click(s,Shell::FileActions);CHECK(s.ContextOpen());
    for(size_t n=0;n<s.ContextCount();++n)if(s.ContextAction(n)==action){auto r=s.ContextBounds(Shell::ContextBase+int(n));s.PointerDown(r.left+5,r.top+5);s.PointerUp(r.left+5,r.top+5);return;}
    CHECK(false);
}
static void Snapshot(Shell& shell,const std::string& path) {
    ui::OwnedSurface image;image.Allocate(1280,720);shell.Render(image.s,u"14:32");FILE* file=std::fopen(path.c_str(),"wb");CHECK(file);std::fprintf(file,"P6\n1280 720\n255\n");
    for(int y=0;y<720;++y)for(int x=0;x<1280;++x){auto pixel=image.s.Get(x,y);unsigned char rgb[]={uint8_t(pixel>>16),uint8_t(pixel>>8),uint8_t(pixel)};CHECK(std::fwrite(rgb,1,3,file)==3);}
    CHECK(std::fclose(file)==0);
}
int main(int argc,char** argv) {
    ui::FontCache::SetDirectory(argc>2?argv[2]:"../../romfs/fonts");ui::FontDesc font;font.face="Liberation Sans";CHECK(!ui::FontCache::Get(font)->Metrics().fixedPitch);
    char pattern[]="/tmp/sr-links-XXXXXX";CHECK(mkdtemp(pattern));const fs::path root=pattern;
    struct Cleanup{fs::path p;~Cleanup(){fs::remove_all(p);}} cleanup{root};
    Shell shell;std::string error;CHECK(shell.ConfigureFilesystem(root.string(),"","",error));
    fs::create_directories(root/"Programs/Приложение");std::ofstream(root/"Programs/Приложение/probe.exe")<<"public path fixture";
    ui::ShellLink link;link.target="C:\\Programs\\Приложение\\probe.exe";link.arguments="\"space value\" \"\"";link.workingDirectory="C:\\TEMP";link.name="Проба 😀";link.icon=link.target;
    const auto encoded=link.Encode();CHECK(!encoded.empty());ui::ShellLink decoded;CHECK(decoded.Decode(encoded) && decoded.target==link.target && decoded.arguments==link.arguments && decoded.name==link.name && decoded.workingDirectory==link.workingDirectory);
    if(argc>1){std::ofstream f(argv[1],std::ios::binary);f.write(reinterpret_cast<const char*>(encoded.data()),encoded.size());}
    for(size_t n=0;n<encoded.size();++n){auto bad=encoded;bad.resize(n);CHECK(!decoded.Decode(bad));}
    for(size_t field:{0,4,20,76,80,84,88,92,100,104,108}){auto bad=encoded;for(unsigned j=0;j<4;++j)bad[field+j]=255;CHECK(!decoded.Decode(bad));}
    const std::string path="C:\\Documents and Settings\\Player\\Desktop\\Пример.lnk";
    for(auto mode:{Fault::Mode::Write,Fault::Mode::Flush,Fault::Mode::Close,Fault::Mode::Rename}) {
        Fault io(mode);CHECK(!link.Save(shell.Browser().Filesystem(),"C:\\TEMP\\failed.lnk",&io) && io.live==0 && !fs::exists(root/"TEMP/failed.lnk"));
    }
    Fault shorts(Fault::Mode::Short);CHECK(link.Save(shell.Browser().Filesystem(),"C:\\TEMP\\short.lnk",&shorts) && shorts.live==0);
    CHECK(link.Save(shell.Browser().Filesystem(),path));CHECK(!link.Save(shell.Browser().Filesystem(),path) && errno==EEXIST);
    ui::ShellBrowser browser;CHECK(browser.Configure(root.string(),"","",error));ui::ShellLaunchRequest request;
    CHECK(browser.OpenPath(path,request) && request.guestPath==link.target && request.arguments==std::vector<std::string>({"space value",""}) && request.workingDirectory=="C:\\TEMP");
    auto invalid=link;invalid.workingDirectory="C:\\missing";CHECK(invalid.Save(browser.Filesystem(),"C:\\TEMP\\bad-cwd.lnk"));CHECK(!browser.OpenPath("C:\\TEMP\\bad-cwd.lnk",request) && request.nativePath.empty());
    invalid=link;invalid.target="C:\\missing.exe";CHECK(invalid.Save(browser.Filesystem(),"C:\\TEMP\\missing.lnk"));CHECK(!browser.OpenPath("C:\\TEMP\\missing.lnk",request));
    std::ofstream(root/"TEMP/corrupt.lnk")<<"broken";CHECK(!browser.OpenPath("C:\\TEMP\\corrupt.lnk",request) && browser.LastError()==ui::ShellBrowser::Error::InvalidShortcut);
    // Real creation commands, native desktop discovery and common launch route.
    shell.Enter();CHECK(shell.DesktopLinks().size()==1);Click(shell,Shell::DesktopLinkBase);CHECK(shell.LaunchRequest().guestPath==link.target && shell.LaunchRequest().arguments.size()==2);
    shell.Enter();Click(shell,Shell::Computer);Click(shell,Shell::QuickC);Click(shell,Shell::Address);shell.CompleteTextInput(true,"C:\\Programs\\Приложение");
    CHECK(shell.Browser().Entries().size()==1);Action(shell,Shell::DesktopShortcut);CHECK(shell.PendingTextInput()==Shell::TextInput::ShortcutCommand);
    shell.CompleteTextInput(false,"");CHECK(shell.DesktopLinks().size()==1);
    Action(shell,Shell::DesktopShortcut);shell.CompleteTextInput(true,rt::QuoteCommandArgument(link.target)+" verify");CHECK(shell.DesktopLinks().size()==2);
    Action(shell,Shell::StartShortcut);shell.CompleteTextInput(true,rt::QuoteCommandArgument(link.target)+" verify");
    Shell fresh;CHECK(fresh.ConfigureFilesystem(root.string(),"","",error));fresh.Enter();CHECK(fresh.DesktopLinks().size()==2);
    if(argc>3)Snapshot(fresh,std::string(argv[3])+"-desktop.ppm");
    fresh.ToggleStart();Click(fresh,Shell::MenuPrograms);CHECK(fresh.Count()==1);fresh.Accept();CHECK(fresh.LaunchRequest().guestPath==link.target && fresh.LaunchRequest().arguments==std::vector<std::string>{"verify"});
    // Real bounded tree: select root, expand, collapse and navigate.
    ui::FolderTree tree;tree.Reset(browser.Filesystem());CHECK(tree.Nodes().size()==1 && tree.Expand(0));
    size_t programs=0;while(programs<tree.Nodes().size() && tree.Nodes()[programs].name!="Programs")++programs;CHECK(programs<tree.Nodes().size());
    CHECK(tree.Expand(programs));CHECK(tree.Nodes()[programs+1].name=="Приложение");tree.Collapse(programs);CHECK(!tree.Nodes()[programs].expanded);
    shell.Enter();Click(shell,Shell::FolderTreeButton);CHECK(shell.ActiveDialog()==Shell::Dialog::FolderTree);
    Click(shell,Shell::TreeExpand);CHECK(shell.Tree().Nodes().size()>1);
    if(argc>3)Snapshot(shell,std::string(argv[3])+"-tree.ppm");
    shell.Move(int(programs));Click(shell,Shell::TreeOpen);CHECK(shell.Browser().Path()=="C:\\Programs" && !shell.ModalActive());
    // Fan-out bound, stale folders and readonly trees are genuine filesystem state.
    for(unsigned i=0;i<200;++i)fs::create_directory(root/("folder-"+std::to_string(i)));
    tree.Reset(browser.Filesystem());CHECK(tree.Expand(0) && tree.Truncated() && tree.Nodes().size()<=ui::FolderTree::MaxNodes);
    fs::create_symlink(root/"Programs",root/"linked-folder");tree.Reset(browser.Filesystem());CHECK(tree.Expand(0));for(const auto& node:tree.Nodes())CHECK(node.name!="linked-folder");
    puts("PASSED: real Unicode MS-SHLLINK subset, Desktop/Start persistence/launch/args/CWD and bounded native directory tree");
}
