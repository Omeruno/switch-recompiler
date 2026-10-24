#include "source/ui/desktop_shell.h"
#include "source/ui/font.h"
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unistd.h>

namespace fs=std::filesystem;
using Browser=ui::ShellBrowser;
using Shell=ui::DesktopShell;
static void Click(Shell& shell,int target) {
    const auto r=Shell::Bounds(target);const int x=(r.left+r.right)/2,y=(r.top+r.bottom)/2;
    shell.PointerDown(x,y);shell.PointerUp(x,y);
}
static void Input(Shell& shell,int target,const std::string& text) {
    Click(shell,target);assert(shell.PendingTextInput()!=Shell::TextInput::None);
    shell.CompleteTextInput(true,text);
}
static size_t Find(const Browser& browser,const std::string& name) {
    const auto& entries=browser.Entries();
    for(size_t i=0;i<entries.size();++i)if(entries[i].name==name)return i;
    assert(false);return 0;
}
static void Screenshot(const Shell& shell,const char* prefix,const char* name) {
    if(!prefix)return;
    ui::OwnedSurface surface;surface.Allocate(Shell::Width,Shell::Height);shell.Render(surface.s,u"13:42");
    FILE* f=std::fopen((std::string(prefix)+"-"+name+".ppm").c_str(),"wb");assert(f);
    std::fprintf(f,"P6\n%d %d\n255\n",surface.s.width,surface.s.height);
    for(int y=0;y<surface.s.height;++y)for(int x=0;x<surface.s.width;++x) {
        const auto p=surface.s.Get(x,y);const unsigned char pixel[]={uint8_t(p>>16),uint8_t(p>>8),uint8_t(p)};
        assert(std::fwrite(pixel,1,3,f)==3);
    }
    std::fclose(f);
}
int main(int argc,char** argv) {
    ui::FontCache::SetDirectory(argc>1?argv[1]:"../../romfs/fonts");const char* prefix=argc>2?argv[2]:nullptr;
    char temp[]="/tmp/sr-browser-XXXXXX";assert(mkdtemp(temp));
    const fs::path base=temp,sd=base/"sd",rom=base/"rom",system=sd/"switch-recompiler/drive_c";
    struct Cleanup {fs::path p;~Cleanup(){fs::remove_all(p);}} cleanup{base};
    fs::create_directories(sd/"Apps/Unicode 文件");fs::create_directories(rom);
    const auto app=sd/"Apps/Unicode 文件/PuBlic.EXE";
    std::ofstream(app)<<"public fixture path; PE loading is a separate test";
    std::ofstream(sd/"Apps/notes.txt")<<"saved document";std::ofstream(sd/"Apps/large.bin")<<std::string(1024,'a');
    std::ofstream(rom/"locked.txt")<<"immutable bytes";
    const auto nativeCwd=fs::current_path();
    Browser browser;ui::ShellLaunchRequest request;request.nativePath="stale";
    assert(!browser.ResolveExecutable("C:\\x.exe",request) && request.nativePath.empty());
    assert(browser.LastError()==Browser::Error::NotReady && !browser.Refresh());
    std::string error;assert(browser.Configure(system.string(),sd.string(),rom.string(),error));
    assert(browser.Entries().size()==3 && browser.Mounted('c') && !browser.Mounted('Z'));
    assert(browser.Entries()[0].name=="C:" && browser.Entries()[1].name=="R:" && browser.Entries()[2].name=="S:");
    assert(browser.Entries()[1].readOnly && browser.Entries()[1].size==0);
    assert(browser.Navigate("S:\\Apps"));assert(browser.Entries()[0].kind==Browser::Kind::Directory);
    browser.Select(Find(browser,"Unicode 文件"));assert(browser.OpenSelected(request) && request.nativePath.empty());
    assert(browser.Path()=="S:\\Apps\\Unicode 文件");browser.Select(Find(browser,"PuBlic.EXE"));
    assert(browser.OpenSelected(request) && request.nativePath==app.string());
    assert(request.guestPath=="S:\\Apps\\Unicode 文件\\PuBlic.EXE");
    assert(browser.Back() && browser.Entries()[browser.Selected()].name=="Unicode 文件");
    browser.Select(Find(browser,"notes.txt"));assert(browser.CanOpenSelected());
    assert(!browser.OpenSelected(request) && request.nativePath.empty() && browser.LastError()==Browser::Error::UnsupportedFile);
    browser.Select(Find(browser,"notes.txt"));browser.CycleSort();assert(browser.Entries()[browser.Selected()].name=="notes.txt");
    browser.CycleSort();assert(browser.Entries()[0].kind==Browser::Kind::Directory && browser.Entries()[1].name=="large.bin");
    const auto old=browser.Path();const auto selected=browser.Entries()[browser.Selected()].path;
    for(const auto& invalid:{"Z:\\", "\\\\server\\share", "S:\\Apps:stream", "S:\\missing"}) {
        assert(!browser.Navigate(invalid));assert(browser.Path()==old && browser.Entries()[browser.Selected()].path==selected);
    }
    assert(!browser.ResolveExecutable(std::string("S:\\Apps\\x\0bad.exe",17),request));
    assert(!browser.ResolveExecutable(std::string(4097,'x'),request));
    assert(browser.ResolveExecutable("s:/apps/Unicode 文件/public.exe",request) && request.nativePath==app.string());
    assert(browser.ResolveCatalogExecutable(app.string(),request));
    assert(browser.ResolveCatalogExecutable(request.nativePath,request) && request.nativePath==app.string());
    assert(!browser.ResolveCatalogExecutable((base/"outside.exe").string(),request));
    // Stale directory data cannot replay an old valid request after removal.
    assert(browser.ResolveExecutable("S:\\Apps\\Unicode 文件\\PuBlic.EXE",request));
    fs::remove(app);assert(!browser.ResolveExecutable(request.guestPath,request) && request.nativePath.empty());
    assert(browser.LastError()==Browser::Error::NotFound);
    std::ofstream(app)<<"fixture restored";
    assert(browser.Navigate("R:\\"));assert(browser.ReadOnly());
    assert(browser.Navigate("R:\\..\\..\\"));assert(browser.Path()=="R:\\" && browser.ReadOnly());
    assert(browser.Up() && browser.Path().empty());
    assert(browser.Navigate(browser.DocumentsPath()));
    const auto doc=system/"Documents and Settings/Player/My Documents/сохранено.txt";std::ofstream(doc)<<"persist";
    assert(browser.Refresh());assert(browser.Entries()[Find(browser,"сохранено.txt")].size==7);
    // Bound history and do not follow links or reinterpret native names.
    fs::create_directory(sd/"A");fs::create_directory(sd/"B");
    for(int i=0;i<40;++i)assert(browser.Navigate(i%2?"S:\\A":"S:\\B"));
    unsigned backCount=0;while(browser.CanBack()){assert(browser.Back());++backCount;}assert(backCount==Browser::MaxHistory);
    fs::create_directory_symlink(sd/"Apps",sd/"linked");std::ofstream(sd/"bad:name")<<"skip";
    assert(browser.Navigate("S:\\"));assert(browser.Errors()>=2);
    assert(!browser.Navigate("S:\\linked") && browser.LastError()==Browser::Error::AccessDenied);
    // A reduced test budget exercises production truncation without thousands of files.
    fs::create_directory(sd/"Many");
    for(int i=9;i>=0;--i)std::ofstream(sd/"Many"/(std::to_string(i)+".exe"))<<i;
    Browser limited(3,20);assert(limited.Configure(system.string(),sd.string(),rom.string(),error));
    assert(limited.Navigate("S:\\Many") && limited.Truncated() && limited.Scanned()==10 && limited.Entries().size()==3);
    assert(limited.Entries()[0].name=="0.exe" && limited.Entries()[2].name=="2.exe");
    Browser scanned(8,4);assert(scanned.Configure(system.string(),sd.string(),rom.string(),error));
    assert(scanned.Navigate("S:\\Many") && scanned.Truncated() && scanned.Scanned()==4 && scanned.Entries().size()==4);
    scanned.Move(-1);assert(scanned.Selected()==3);scanned.Page(-100,2);assert(scanned.Selected()==1);
    assert(!scanned.Navigate("S:\\Many\\MISSING") && scanned.LastError()==Browser::Error::ResolutionLimit);
    assert(scanned.Path()=="S:\\Many");
    fs::create_directory(sd/"Filtered");std::ofstream(sd/"Filtered/bad:name")<<"not representable";
    assert(browser.Navigate("S:\\Filtered") && browser.Entries().empty() && browser.Errors()==1);
    assert(!browser.OpenPath("",request) && request.nativePath.empty() && browser.LastError()==Browser::Error::InvalidPath);
    // Opt-in cumulative VFS case-search budget; exact-case fast lookup needs no enumeration.
    rt::Vfs vfs;vfs.SetRoot(sd.string());size_t budget=0;
    assert(vfs.ToHost("C:\\Apps",false,&budget)==(sd/"Apps").string());
    assert(vfs.ToHost("C:\\aPpS",false,&budget).empty() && errno==EOVERFLOW);
    budget=4096;assert(vfs.ToHost("C:\\aPpS",false,&budget)==(sd/"Apps").string() && budget<4096);
    assert(vfs.ToHost("C:\\aPpS")== (sd/"Apps").string()); // Guest/default policy unchanged.

    Shell shell;assert(shell.ConfigureFilesystem(system.string(),sd.string(),rom.string(),error));shell.Enter();
    assert(shell.Enabled(Shell::Computer) && shell.Enabled(Shell::Documents) && shell.Enabled(Shell::MenuRun));
    Click(shell,Shell::Computer);assert(shell.ActivePanel()==Shell::Panel::Explorer && shell.Browser().Path().empty());
    Screenshot(shell,prefix,"computer");
    Click(shell,Shell::QuickDocuments);assert(shell.Browser().Path()==browser.DocumentsPath());
    Screenshot(shell,prefix,"documents");
    Input(shell,Shell::Address,"S:\\Apps\\Unicode 文件");
    assert(shell.Browser().Path()=="S:\\Apps\\Unicode 文件");
    const int row=Shell::BrowserRowBase+int(Find(shell.Browser(),"PuBlic.EXE"));Click(shell,row);shell.Accept();
    assert(shell.LaunchRequest().nativePath==app.string());const auto fromExplorer=shell.LaunchRequest();
    shell.Enter();assert(shell.LaunchPath().empty() && shell.Browser().Path()=="S:\\Apps\\Unicode 文件");
    const int focus=shell.Focus();shell.ToggleStart();shell.Back();assert(shell.Focus()==focus);
    shell.ToggleStart();Input(shell,Shell::MenuRun,"\"S:\\Apps\\Unicode 文件\\PuBlic.EXE\"");
    assert(shell.LaunchRequest().nativePath==fromExplorer.nativePath && shell.LaunchRequest().guestPath==fromExplorer.guestPath);
    shell.Enter();ui::ExecutableCatalog catalog;catalog.ScanRoot((sd/"Apps/Unicode 文件").string()+"/",0);shell.SetCatalog(catalog);
    Click(shell,Shell::Close);Click(shell,Shell::Programs);shell.Accept();assert(shell.LaunchPath()==fromExplorer.nativePath);
    shell.Enter();shell.ToggleStart();Input(shell,Shell::MenuRun,"S:\\missing.exe");assert(shell.LaunchPath().empty());
    shell.ToggleStart();Input(shell,Shell::MenuRun,"S:\\Apps");assert(shell.ActivePanel()==Shell::Panel::Explorer);
    Screenshot(shell,prefix,"explorer");
    const auto beforeCancel=shell.Browser().Path();Click(shell,Shell::Address);shell.CompleteTextInput(false,"Z:\\");
    assert(shell.Browser().Path()==beforeCancel && shell.LaunchPath().empty());
    Input(shell,Shell::Address,"Z:\\");assert(shell.Browser().Path()==beforeCancel);
    // Y/NextFocus reaches toolbar and quick roots without moving the selected row.
    bool addressReached=false;for(unsigned i=0;i<30;++i){shell.NextFocus();if(shell.Focus()==Shell::Address)addressReached=true;}
    assert(addressReached);
    // Arguments and explicit document associations are shell-owned across Enter.
    shell.ToggleStart();Input(shell,Shell::MenuRun,"\"S:\\Apps\\Unicode 文件\\PuBlic.EXE\" \"space value\" \"\" \"slash\\\\\"");
    assert(shell.LaunchRequest().arguments==std::vector<std::string>({"space value","","slash\\"}));
    shell.Enter();Input(shell,Shell::Address,"S:\\Apps");
    Click(shell,Shell::BrowserRowBase+int(Find(shell.Browser(),"notes.txt")));shell.Accept();
    assert(shell.PendingTextInput()==Shell::TextInput::OpenWith && shell.LaunchPath().empty());
    shell.CompleteTextInput(false,"");assert(shell.LaunchPath().empty() && shell.Browser().ApplicationFor("a.txt").empty());
    Click(shell,Shell::OpenWith);shell.CompleteTextInput(true,"S:\\Apps\\Unicode 文件\\PuBlic.EXE");
    assert(shell.LaunchRequest().arguments==std::vector<std::string>{"S:\\Apps\\notes.txt"});
    shell.Enter();shell.Accept();assert(shell.LaunchRequest().arguments.size()==1 && !shell.LaunchPath().empty());
    Screenshot(shell,prefix,"associated-document");
    shell.Enter();shell.ToggleStart();Input(shell,Shell::MenuRun,"\"bad quote");assert(shell.LaunchPath().empty());
    Browser associations;assert(associations.Configure((sd/"association-system").string(),sd.string(),rom.string(),error));
    for(size_t i=0;i<Browser::MaxAssociations;++i) {
        const auto name="type.t"+std::to_string(i);std::ofstream(sd/name)<<"data";
        assert(associations.OpenWith("S:\\"+name,"S:\\Apps\\Unicode 文件\\PuBlic.EXE",request));
    }
    std::ofstream(sd/"type.extra")<<"data";
    assert(!associations.OpenWith("S:\\type.extra","S:\\Apps\\Unicode 文件\\PuBlic.EXE",request));
    assert(associations.LastError()==Browser::Error::AssociationLimit && request.nativePath.empty());
    std::ofstream(sd/"UPPER.T0")<<"data";assert(associations.OpenPath("S:\\UPPER.T0",request));
    fs::remove(app);assert(!associations.OpenPath("S:\\UPPER.T0",request) && request.nativePath.empty());
    assert(associations.LastError()==Browser::Error::MissingApplication);std::ofstream(app)<<"fixture restored";
    // A Start action whose backing folder disappeared preserves visible focus.
    Click(shell,Shell::Close);fs::remove(doc);fs::remove_all(system/"Documents and Settings/Player/My Documents");
    const int previousFocus=shell.Focus();shell.ToggleStart();Click(shell,Shell::MenuDocuments);
    assert(!shell.StartOpen() && shell.ActivePanel()==Shell::Panel::Desktop && shell.Focus()==previousFocus);
    assert(shell.Browser().LastError()==Browser::Error::NotFound);
    assert(fs::current_path()==nativeCwd);
    std::ifstream locked(rom/"locked.txt");std::string bytes;std::getline(locked,bytes);assert(bytes=="immutable bytes");
    puts("Shell browser: actual C/S/R, bounded listing/case lookup/history, path errors, readonly/Unicode and Start/Explorer/Run launch parity PASS");
}
