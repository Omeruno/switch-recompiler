#include "source/ui/desktop_shell.h"
#include "source/ui/font.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
namespace fs=std::filesystem;
using Shell=ui::DesktopShell;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"context %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static void Click(Shell& s,int target) {
    const auto r=target>=Shell::ContextBase && target<Shell::ContextBase+6?s.ContextBounds(target):Shell::Bounds(target);
    const int x=(r.left+r.right)/2,y=(r.top+r.bottom)/2;s.PointerDown(x,y);s.PointerUp(x,y);
}
static int Item(const Shell& s,int action) {
    for(size_t i=0;i<s.ContextCount();++i)if(s.ContextAction(i)==action)return Shell::ContextBase+int(i);
    return Shell::None;
}
static int Row(const Shell& s,const std::string& name) {
    for(size_t i=0;i<s.Browser().Entries().size();++i)if(s.Browser().Entries()[i].name==name){CHECK(i<Shell::Rows);return Shell::BrowserRowBase+int(i);}
    CHECK(false);return 0;
}
static void Right(Shell& s,int target) {
    const auto r=Shell::Bounds(target);s.SecondaryDown(r.left+20,r.top+10);s.SecondaryUp(r.left+20,r.top+10);
}
static void Input(Shell& s,int target,const std::string& text) {
    Click(s,target);CHECK(s.PendingTextInput()!=Shell::TextInput::None);s.CompleteTextInput(true,text);
}
static void Focus(Shell& s,int target) {for(int i=0;i<40;++i){if(s.Focus()==target)return;s.NextFocus();}CHECK(false);}
static void Screenshot(Shell& s,const char* path) {
    if(!path)return;
    ui::OwnedSurface pixels;pixels.Allocate(1280,720);s.Render(pixels.s,u"14:32");
    FILE* f=std::fopen(path,"wb");CHECK(f);std::fprintf(f,"P6\n1280 720\n255\n");
    for(int y=0;y<720;++y)for(int x=0;x<1280;++x){const auto c=pixels.s.Get(x,y);unsigned char rgb[]={uint8_t(c>>16),uint8_t(c>>8),uint8_t(c)};CHECK(std::fwrite(rgb,1,3,f)==3);}
    CHECK(std::fclose(f)==0);
}
int main(int argc,char** argv) {
    ui::FontCache::SetDirectory(argc>1?argv[1]:"../../romfs/fonts");ui::FontDesc font;font.face="Liberation Sans";font.height=-16;CHECK(!ui::FontCache::Get(font)->Metrics().fixedPitch);
    char pattern[]="/tmp/sr-context-XXXXXX";CHECK(mkdtemp(pattern));const fs::path base=pattern,root=base/"system",rom=base/"rom";
    struct Cleanup{fs::path p;~Cleanup(){fs::remove_all(p);}} cleanup{base};fs::create_directory(rom);std::ofstream(rom/"read.txt")<<"immutable";
    Shell s;std::string error;CHECK(s.ConfigureFilesystem(root.string(),"",rom.string(),error));s.Enter();Click(s,Shell::Computer);Input(s,Shell::Address,"C:\\TEMP");
    fs::create_directory(root/"TEMP/folder");std::ofstream(root/"TEMP/notes.txt")<<"actual bytes";std::ofstream(root/"TEMP/other.txt")<<"preserve";Click(s,Shell::Refresh);
    const int notes=Row(s,"notes.txt"),other=Row(s,"other.txt");Click(s,other);
    auto r=Shell::Bounds(notes),r2=Shell::Bounds(other);s.SecondaryDown(r.left+8,r.top+8);s.SecondaryUp(r2.left+8,r2.top+8);CHECK(!s.ContextOpen());
    Right(s,notes);CHECK(s.ContextOpen() && s.ModalActive() && s.Browser().Entries()[s.Browser().Selected()].name=="notes.txt" && s.LaunchPath().empty());
    CHECK(Item(s,Shell::Launch) && Item(s,Shell::OpenWith) && Item(s,Shell::Copy) && Item(s,Shell::Properties));
    for(size_t i=0;i<s.ContextCount();++i){const auto box=s.ContextBounds(Shell::ContextBase+int(i));const auto area=Shell::ContentArea();CHECK(box.left>=area.left && box.top>=area.top && box.right<=area.right && box.bottom<=area.bottom);}
    Screenshot(s,argc>2?argv[2]:nullptr);
    const auto copyBox=s.ContextBounds(Item(s,Shell::Copy));s.PointerMove(copyBox.left+10,copyBox.top+10);
    CHECK(s.Focus()==Item(s,Shell::Copy));
    s.Page(1);s.ToggleStart();CHECK(!s.StartOpen() && s.ContextOpen());
    Click(s,Shell::Start);CHECK(!s.ContextOpen() && !s.StartOpen()); // dismiss only
    Right(s,notes);Click(s,Item(s,Shell::Copy));CHECK(!s.ContextOpen() && s.ClipboardPath()=="C:\\TEMP\\notes.txt");
    Click(s,Shell::FolderActions);CHECK(s.ContextOpen() && Item(s,Shell::Paste) && !Item(s,Shell::Delete));
    Input(s,Item(s,Shell::Paste),"copied.txt");while(s.CopyRunning())s.ServiceCopy();CHECK(fs::file_size(root/"TEMP/copied.txt")==12);
    Right(s,Row(s,"copied.txt"));Input(s,Item(s,Shell::Rename),"renamed.txt");CHECK(fs::exists(root/"TEMP/renamed.txt") && !fs::exists(root/"TEMP/copied.txt"));
    Right(s,Row(s,"renamed.txt"));Click(s,Item(s,Shell::Delete));CHECK(s.ActiveDialog()==Shell::Dialog::DeleteConfirm && !s.ContextOpen() && s.Focus()==Shell::CancelDialog);
    s.Accept();CHECK(fs::exists(root/"TEMP/renamed.txt"));
    Right(s,Row(s,"renamed.txt"));Click(s,Item(s,Shell::Delete));Click(s,Shell::ConfirmDelete);CHECK(!fs::exists(root/"TEMP/renamed.txt"));
    // Empty listing background opens current-folder actions, independent of selection.
    s.SecondaryDown(1137,553);s.SecondaryUp(1137,553);CHECK(s.ContextOpen() && Item(s,Shell::NewFolder) && !Item(s,Shell::Copy));
    Input(s,Item(s,Shell::NewFolder),"created");CHECK(fs::is_directory(root/"TEMP/created"));
    Right(s,Row(s,"folder"));CHECK(!Item(s,Shell::Copy));Click(s,Item(s,Shell::Launch));CHECK(s.Browser().Path()=="C:\\TEMP\\folder");
    s.OpenContext();CHECK(Item(s,Shell::NewFolder));s.Back();CHECK(s.Browser().Path()=="C:\\TEMP\\folder");
    Click(s,Shell::NavUp);Click(s,Row(s,"notes.txt"));Focus(s,Shell::FileActions);s.Accept();CHECK(s.ContextOpen());
    Focus(s,Item(s,Shell::Properties));s.Accept();CHECK(s.ActiveDialog()==Shell::Dialog::Properties && s.PropertiesSnapshot().size==12);s.Back();
    // A second right press dismisses; its release cannot open a new menu.
    Right(s,Row(s,"notes.txt"));s.SecondaryDown(1137,553);s.SecondaryUp(1137,553);CHECK(!s.ContextOpen());
    // Disappeared/type-changed target is revalidated before dispatching an action.
    Right(s,Row(s,"notes.txt"));const int copy=Item(s,Shell::Copy);fs::remove(root/"TEMP/notes.txt");Click(s,copy);
    CHECK(!s.ContextOpen() && s.Browser().LastError()==ui::ShellBrowser::Error::NotFound);
    std::ofstream(root/"TEMP/notes.txt")<<"new";Click(s,Shell::Refresh);Right(s,Row(s,"notes.txt"));const int remove=Item(s,Shell::Delete);
    fs::remove(root/"TEMP/notes.txt");fs::create_directory(root/"TEMP/notes.txt");Click(s,remove);CHECK(s.ActiveDialog()==Shell::Dialog::None && fs::is_directory(root/"TEMP/notes.txt"));
    Click(s,Shell::QuickR);Right(s,Row(s,"read.txt"));CHECK(s.Enabled(Item(s,Shell::Copy)) && !s.Enabled(Item(s,Shell::Delete)) && !s.Enabled(Item(s,Shell::Rename)));
    Click(s,Item(s,Shell::Delete));CHECK(s.ContextOpen() && fs::exists(rom/"read.txt"));s.Back();
    Click(s,Shell::FolderActions);CHECK(!s.Enabled(Item(s,Shell::NewFolder)) && !s.Enabled(Item(s,Shell::Paste)));s.Enter();CHECK(!s.ContextOpen());
    CHECK(s.LaunchPath().empty());
    puts("PASSED: actual native object/folder context actions, right-edge targeting/dismissal, controller focus, readonly and stale-target validation");
}
