#include "source/ui/desktop_shell.h"
#include "source/ui/font.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
namespace fs=std::filesystem;
using Shell=ui::DesktopShell;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"shell file actions %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static void Click(Shell& s,int target) {
    const auto r=Shell::Bounds(target);const int x=(r.left+r.right)/2,y=(r.top+r.bottom)/2;
    s.PointerDown(x,y);s.PointerUp(x,y);
}
static void Address(Shell& s,const std::string& path) {
    Click(s,Shell::Address);CHECK(s.PendingTextInput()==Shell::TextInput::Address);s.CompleteTextInput(true,path);
    CHECK(s.Browser().Path()==path);
}
static void Select(Shell& s,const std::string& name) {
    for(size_t i=0;i<s.Browser().Entries().size();++i)if(s.Browser().Entries()[i].name==name) {
        CHECK(i<Shell::Rows);Click(s,Shell::BrowserRowBase+int(i));return;
    }
    CHECK(false);
}
static bool Focus(Shell& s,int target) {
    for(int i=0;i<40;++i){if(s.Focus()==target)return true;s.NextFocus();}return false;
}
static void Picture(Shell& s,const char* prefix,const char* name) {
    ui::OwnedSurface surface;surface.Allocate(Shell::Width,Shell::Height);CHECK(surface.s.Valid());s.Render(surface.s,u"14:32");
    if(!prefix)return;
    const auto path=std::string(prefix)+"-"+name+".ppm";FILE* out=std::fopen(path.c_str(),"wb");CHECK(out);
    std::fprintf(out,"P6\n%d %d\n255\n",Shell::Width,Shell::Height);
    for(int y=0;y<Shell::Height;++y)for(int x=0;x<Shell::Width;++x) {
        const auto c=surface.s.Get(x,y);unsigned char rgb[]={uint8_t(c>>16),uint8_t(c>>8),uint8_t(c)};
        CHECK(std::fwrite(rgb,1,3,out)==3);
    }
    CHECK(std::fclose(out)==0);
}
int main(int argc,char** argv) {
    ui::FontCache::SetDirectory(argc>1?argv[1]:"../../romfs/fonts");
    ui::FontDesc font;font.face="Liberation Sans";font.height=-16;CHECK(!ui::FontCache::Get(font)->Metrics().fixedPitch);
    const char* prefix=argc>2?argv[2]:nullptr;
    char pattern[]="/tmp/sr-shell-files-XXXXXX";CHECK(mkdtemp(pattern));
    const fs::path base=pattern,sd=base/"sd",rom=base/"rom",system=sd/"drive_c";
    struct Cleanup {fs::path p;~Cleanup(){fs::remove_all(p);}} cleanup{base};
    fs::create_directory(sd);fs::create_directory(rom);std::ofstream(rom/"locked.txt")<<"locked";
    Shell s;std::string error;CHECK(s.ConfigureFilesystem(system.string(),sd.string(),rom.string(),error));s.Enter();
    Click(s,Shell::Computer);CHECK(!s.Enabled(Shell::NewFolder));CHECK(!s.Enabled(Shell::Delete));CHECK(!s.Enabled(Shell::Rename));
    Address(s,"C:\\TEMP");CHECK(s.Enabled(Shell::NewFolder) && !s.Enabled(Shell::Properties));
    CHECK(Focus(s,Shell::NewFolder));s.Accept();CHECK(s.PendingTextInput()==Shell::TextInput::NewFolder);
    // Pending name entry freezes path, focus, selection, Start and click-through.
    s.Page(1);s.Move(1);s.NextFocus();s.ToggleStart();Click(s,Shell::QuickR);
    CHECK(s.Browser().Path()=="C:\\TEMP" && !s.StartOpen());
    s.CompleteTextInput(false,"cancelled");CHECK(!fs::exists(system/"TEMP/cancelled"));
    Click(s,Shell::NewFolder);s.CompleteTextInput(true,"../escape");
    CHECK(s.Browser().LastError()==ui::ShellBrowser::Error::InvalidPath && !fs::exists(system/"escape"));
    Click(s,Shell::NewFolder);s.CompleteTextInput(true,"Документы");
    CHECK(fs::is_directory(system/"TEMP/Документы") && s.Browser().Entries()[s.Browser().Selected()].name=="Документы");
    Picture(s,prefix,"explorer");
    CHECK(Focus(s,Shell::Rename));s.Accept();CHECK(s.InitialText()=="Документы");
    s.CompleteTextInput(false,"cancelled");CHECK(fs::exists(system/"TEMP/Документы"));
    Click(s,Shell::Rename);s.CompleteTextInput(true,"Папка");CHECK(fs::is_directory(system/"TEMP/Папка") && !fs::exists(system/"TEMP/Документы"));
    std::ofstream(system/"TEMP/keep.txt")<<"actual saved bytes";
    Click(s,Shell::Refresh);Select(s,"keep.txt");
    Click(s,Shell::Properties);CHECK(s.ActiveDialog()==Shell::Dialog::Properties);
    CHECK(s.PropertiesSnapshot().size==18 && s.PropertiesSnapshot().kind==rt::FileProperties::Kind::File);
    CHECK(s.DialogPath()=="C:\\TEMP\\keep.txt" && s.Focus()==Shell::CancelDialog);
    Picture(s,prefix,"properties");
    Click(s,Shell::Close);s.ToggleStart();Click(s,Shell::QuickR);s.Page(1);
    CHECK(s.ActiveDialog()==Shell::Dialog::Properties && s.Browser().Path()=="C:\\TEMP" && !s.StartOpen());
    s.Accept();CHECK(!s.ModalActive());
    std::ofstream(system/"TEMP/keep.txt",std::ios::app)<<"!";
    Click(s,Shell::Properties);CHECK(s.PropertiesSnapshot().size==19);s.Back();
    Click(s,Shell::Rename);s.CompleteTextInput(true,"Папка");CHECK(s.Browser().LastError()==ui::ShellBrowser::Error::AlreadyExists);
    CHECK(fs::file_size(system/"TEMP/keep.txt")==19);
    // Enter defaults to Cancel; outside clicks, Start and paging cannot retarget.
    Click(s,Shell::Delete);CHECK(s.ActiveDialog()==Shell::Dialog::DeleteConfirm && s.Focus()==Shell::CancelDialog);
    const auto target=s.DialogPath();Click(s,Shell::BrowserRowBase);s.Page(1);s.ToggleStart();
    CHECK(s.DialogPath()==target && s.Browser().Entries()[s.Browser().Selected()].name=="keep.txt");
    Picture(s,prefix,"delete");s.Accept();CHECK(!s.ModalActive() && fs::exists(system/"TEMP/keep.txt"));
    CHECK(Focus(s,Shell::Delete));s.Accept();s.Move(1);CHECK(s.Focus()==Shell::ConfirmDelete);s.Accept();
    CHECK(!fs::exists(system/"TEMP/keep.txt"));
    Select(s,"Папка");std::ofstream(system/"TEMP/Папка/child.txt")<<"keep";
    Click(s,Shell::Delete);Click(s,Shell::ConfirmDelete);
    CHECK(s.Browser().LastError()==ui::ShellBrowser::Error::NotEmpty && fs::exists(system/"TEMP/Папка/child.txt"));
    Click(s,Shell::Properties);CHECK(s.PropertiesSnapshot().kind==rt::FileProperties::Kind::Directory && s.PropertiesSnapshot().size==0);s.Back();
    fs::remove(system/"TEMP/Папка/child.txt");Click(s,Shell::Delete);s.Back();CHECK(fs::is_directory(system/"TEMP/Папка"));
    // A type replacement while confirmation is open must not change delete semantics.
    Click(s,Shell::Delete);fs::remove(system/"TEMP/Папка");std::ofstream(system/"TEMP/Папка")<<"replacement";
    Click(s,Shell::ConfirmDelete);CHECK(fs::is_regular_file(system/"TEMP/Папка"));
    Click(s,Shell::Refresh);Click(s,Shell::Delete);fs::remove(system/"TEMP/Папка");
    Click(s,Shell::ConfirmDelete);CHECK(s.Browser().LastError()==ui::ShellBrowser::Error::NotFound);
    Click(s,Shell::QuickR);CHECK(!s.Enabled(Shell::NewFolder) && !s.Enabled(Shell::Rename) && !s.Enabled(Shell::Delete));
    Click(s,Shell::Properties);CHECK(s.PropertiesSnapshot().mountReadOnly && s.PropertiesSnapshot().size==6);s.Back();
    // Clipboard is a path/name snapshot, not retained file bytes/handles.
    Click(s,Shell::Copy);CHECK(s.ClipboardPath()=="R:\\locked.txt");
    Click(s,Shell::QuickC);Address(s,"C:\\TEMP");CHECK(s.Enabled(Shell::Paste));
    CHECK(Focus(s,Shell::Paste));s.Accept();CHECK(s.PendingTextInput()==Shell::TextInput::PasteName && s.InitialText()=="locked.txt");
    s.CompleteTextInput(false,"no-copy");CHECK(!fs::exists(system/"TEMP/no-copy"));
    Click(s,Shell::Paste);s.CompleteTextInput(true,"copied.txt");CHECK(s.CopyRunning() && s.ActiveDialog()==Shell::Dialog::CopyProgress);
    Picture(s,prefix,"copy-progress");
    Click(s,Shell::QuickR);s.ToggleStart();CHECK(s.Browser().Path()=="C:\\TEMP" && !s.StartOpen());
    while(s.CopyRunning())CHECK(s.ServiceCopy());
    CHECK(!s.ModalActive() && fs::file_size(system/"TEMP/copied.txt")==6 && s.Browser().Entries()[s.Browser().Selected()].name=="copied.txt");
    Click(s,Shell::Paste);s.CompleteTextInput(true,"copied.txt");CHECK(!s.CopyRunning() && s.Browser().LastError()==ui::ShellBrowser::Error::AlreadyExists);
    CHECK(fs::file_size(system/"TEMP/copied.txt")==6);
    Click(s,Shell::Paste);s.CompleteTextInput(true,"../escape");CHECK(!s.CopyRunning() && s.Browser().LastError()==ui::ShellBrowser::Error::InvalidPath);
    std::ofstream(system/"TEMP/large.bin",std::ios::binary)<<std::string(200003,'q');Click(s,Shell::Refresh);Select(s,"large.bin");Click(s,Shell::Copy);
    Click(s,Shell::Paste);s.CompleteTextInput(true,"cancel-large.bin");CHECK(s.CopyRunning());s.ServiceCopy();CHECK(s.CopyRunning());
    s.Back();CHECK(!s.CopyRunning() && !s.ModalActive() && !fs::exists(system/"TEMP/cancel-large.bin"));
    s.SetGuestActive("copy owner");Click(s,Shell::Computer);Address(s,"C:\\TEMP");
    Click(s,Shell::Paste);s.CompleteTextInput(true,"end-guest.bin");CHECK(s.CopyRunning());s.EndGuest();CHECK(!s.CopyRunning() && !fs::exists(system/"TEMP/end-guest.bin"));
    s.Enter();CHECK(s.ClipboardPath()=="C:\\TEMP\\large.bin");
    Click(s,Shell::Paste);s.CompleteTextInput(true,"restart-copy.bin");CHECK(s.CopyRunning());s.Enter();CHECK(!s.CopyRunning() && !fs::exists(system/"TEMP/restart-copy.bin"));
    fs::remove(system/"TEMP/large.bin");Click(s,Shell::Paste);s.CompleteTextInput(true,"missing-source.bin");CHECK(!s.CopyRunning() && s.Browser().LastError()==ui::ShellBrowser::Error::NotFound);
    Click(s,Shell::QuickR);CHECK(!s.Enabled(Shell::Paste));
    // Dialog/name cancellation at frontend re-entry discards transient commands.
    Click(s,Shell::QuickC);Click(s,Shell::NewFolder);s.Enter();s.CompleteTextInput(true,"must-not-create");CHECK(!fs::exists(system/"must-not-create"));
    CHECK(s.LaunchPath().empty() && !s.ExitRequested());
    std::puts("PASSED: native file actions, controller focus, real mutation/properties, modal targeting/cancel/readonly/stale errors");
}
