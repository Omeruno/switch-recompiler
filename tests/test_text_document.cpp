#include "source/ui/text_document.h"
#include "source/ui/desktop_shell.h"
#include "source/ui/font.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
namespace fs=std::filesystem;
using Doc=ui::TextDocument;using Shell=ui::DesktopShell;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"text %d: %s errno=%d\n",__LINE__,#x,errno);std::exit(1);}}while(0)
static std::string Bytes(const fs::path& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
static void Write(const fs::path& p,const std::string& data){std::ofstream f(p,std::ios::binary);f.write(data.data(),data.size());CHECK(f.good());}
struct Fault:rt::FileCopyIo {
    enum class Mode{None,Read,ReadClose,Create,Partial,Zero,Flush,Close,MoveOld,Publish,Rollback,Cleanup,Interrupted,InterruptedForever,LateChange};
    std::string target;
    Mode mode;int writer=-1,live=0,renames=0;size_t written=0;bool interrupted=false;
    explicit Fault(Mode m):mode(m){}
    int OpenRead(const std::string& p) override {int fd=FileCopyIo::OpenRead(p);if(fd>=0)++live;return fd;}
    int CreateExclusive(const std::string& p) override {
        if(mode==Mode::Create){errno=ENOSPC;return -1;}
        writer=FileCopyIo::CreateExclusive(p);if(writer>=0)++live;return writer;
    }
    ssize_t Read(int fd,void* p,size_t n) override {if(mode==Mode::Read){errno=EIO;return -1;}return FileCopyIo::Read(fd,p,std::min(n,size_t(7)));}
    ssize_t Write(int fd,const void* p,size_t n) override {
        if(mode==Mode::InterruptedForever){errno=EINTR;return -1;}
        if(mode==Mode::Partial && written>=5){errno=ENOSPC;return -1;}
        if(mode==Mode::Zero)return 0;
        if(mode==Mode::Interrupted && !interrupted){interrupted=true;errno=EINTR;return -1;}
        auto result=FileCopyIo::Write(fd,p,std::min(n,size_t(5)));if(result>0)written+=size_t(result);return result;
    }
    int Flush(int fd) override {if(mode==Mode::Flush || mode==Mode::Cleanup){errno=EIO;return -1;}return FileCopyIo::Flush(fd);}
    int Close(int fd) override {
        auto result=FileCopyIo::Close(fd);--live;
        if(mode==Mode::LateChange && fd==writer)::Write(target,"late external writer");
        if(mode==Mode::ReadClose || (mode==Mode::Close && fd==writer)){errno=EIO;return -1;}return result;
    }
    int Rename(const std::string& a,const std::string& b) override {
        ++renames;
        if((mode==Mode::MoveOld && renames==1) || ((mode==Mode::Publish || mode==Mode::Rollback) && renames==2) || (mode==Mode::Rollback && renames==3)){errno=EIO;return -1;}
        // Model Horizon: existing destination rename is refused, even on Linux.
        if(fs::exists(b)){errno=EEXIST;return -1;}return FileCopyIo::Rename(a,b);
    }
    int Remove(const std::string& p) override {if(mode==Mode::Cleanup){errno=EACCES;return -1;}return FileCopyIo::Remove(p);}
};
static void Click(Shell& s,int target){const auto r=Shell::Bounds(target);s.PointerDown(r.left+5,r.top+5);s.PointerUp(r.left+5,r.top+5);}
static void Menu(Shell& s,int action,bool folder=false) {
    Click(s,folder?Shell::FolderActions:Shell::FileActions);CHECK(s.ContextOpen());
    for(size_t i=0;i<s.ContextCount();++i)if(s.ContextAction(i)==action){const auto r=s.ContextBounds(Shell::ContextBase+int(i));s.PointerDown(r.left+5,r.top+5);s.PointerUp(r.left+5,r.top+5);return;}
    std::fprintf(stderr,"missing action %d selected=%s count=%zu\n",action,s.Browser().Entries().empty()?"none":s.Browser().Entries()[s.Browser().Selected()].name.c_str(),s.ContextCount());
    CHECK(false);
}
int main(int argc,char** argv) {
    char pattern[]="/tmp/sr-text-XXXXXX";CHECK(mkdtemp(pattern));const fs::path root=pattern,rom=root/"rom";
    struct Cleanup{fs::path path;~Cleanup(){fs::remove_all(path);}} cleanup{root};fs::create_directory(rom);
    rt::Vfs v;v.SetRoot(root.string());CHECK(v.Mount('R',rom.string(),true));
    Doc doc;const auto p=root/"документ 😀.txt";const std::string guest="C:\\документ 😀.txt";
    const std::string original="\xef\xbb\xbfПервая 😀\r\nsecond\nthird\rlast";Write(p,original);
    CHECK(doc.Open(v,guest) && !doc.Dirty() && doc.Lines().size()==4 && doc.Format()==Doc::Encoding::Utf8Bom);
    CHECK(Doc::Utf8(doc.Lines()[0].text)=="Первая 😀");CHECK(doc.Edit(1,"изменено 😀") && doc.Dirty());
    Fault horizon(Fault::Mode::None);CHECK(doc.Save(v,"",&horizon) && horizon.live==0 && !doc.Dirty());
    CHECK(Bytes(doc.BackupPath())==original && Bytes(p)=="\xef\xbb\xbfПервая 😀\r\nизменено 😀\nthird\rlast");
    Doc fresh;CHECK(fresh.Open(v,guest) && fresh.Lines()[1].text==u"изменено 😀");
    CHECK(!doc.Edit(0,std::string(501,'x')) && doc.Error()==EFBIG);CHECK(!doc.Edit(0,"bad\nline"));
    CHECK(!doc.Edit(0,std::string("a\0b",3)) && !doc.Edit(0,"\xed\xa0\x80") && !doc.Edit(0,"\xf4\x90\x80\x80"));
    // Failed loads keep the previous live document and bytes.
    for(const auto& invalid:std::vector<std::string>{"\xc0\x80",std::string("a\0b",3),std::string("\xff\xfe\x00\xd8",4),std::string("\xff\xfe\x00",3),"\xfe\xff\xdc\x00","\x01",std::string(Doc::MaxBytes+1,'x')}) {
        Write(root/"bad.txt",invalid);CHECK(!doc.Open(v,"C:\\bad.txt") && doc.Lines()[1].text==u"изменено 😀");
    }
    // Both UTF-16 byte orders, BOM and supplementary pairs survive actual saves.
    for(bool little:{false,true}) {
        std::string bytes=little?"\xff\xfe":"\xfe\xff";
        for(char16_t c:std::u16string(u"A😀\r\nB")){bytes+=char(little?c&255:c>>8);bytes+=char(little?c>>8:c&255);}
        Write(root/"wide.txt",bytes);CHECK(fresh.Open(v,"C:\\wide.txt") && fresh.Edit(1,"Б"));
        CHECK(fresh.Save(v,"C:\\wide-copy.txt"));Doc wide;CHECK(wide.Open(v,"C:\\wide-copy.txt") && wide.Lines()[0].text==u"A😀" && wide.Lines()[1].text==u"Б" && wide.Format()==fresh.Format());
        fs::remove(root/"wide-copy.txt");
    }
    Write(p,"before");CHECK(doc.Open(v,guest));CHECK(doc.Edit(0,"after bytes"));
    Write(p,"external writer");CHECK(!doc.Save(v) && doc.Error()==EBUSY && Bytes(p)=="external writer" && doc.Dirty());
    CHECK(!doc.Save(v,guest) && doc.Error()==EEXIST && Bytes(p)=="external writer");
    CHECK(!doc.Save(v,"R:\\new.txt") && doc.Error()==EROFS);
    fs::permissions(p,fs::perms::owner_read,fs::perm_options::replace);CHECK(!doc.Save(v) && doc.Error()==EACCES);fs::permissions(p,fs::perms::owner_all,fs::perm_options::replace);
    for(auto mode:{Fault::Mode::Read,Fault::Mode::ReadClose,Fault::Mode::Create,Fault::Mode::Partial,Fault::Mode::Zero,Fault::Mode::Flush,Fault::Mode::Close,Fault::Mode::MoveOld,Fault::Mode::Publish,Fault::Mode::Rollback,Fault::Mode::Cleanup,Fault::Mode::InterruptedForever,Fault::Mode::LateChange}) {
        Write(p,"before");CHECK(doc.Open(v,guest) && doc.Edit(0,"after bytes"));Fault io(mode);io.target=p.string();
        CHECK(!doc.Save(v,"",&io) && io.live==0 && doc.Dirty());
        if(mode==Fault::Mode::Rollback){CHECK(!fs::exists(p) && Bytes(doc.BackupPath())=="before");}
        else if(mode==Fault::Mode::LateChange)CHECK(Bytes(p)=="late external writer" && doc.Error()==EBUSY);
        else CHECK(Bytes(p)=="before");
        if(mode==Fault::Mode::Cleanup)CHECK(!doc.RecoveryPath().empty() && fs::exists(doc.RecoveryPath()));
    }
    Write(p,"before");CHECK(doc.Open(v,guest) && doc.Edit(0,"after bytes"));Fault retry(Fault::Mode::Interrupted);CHECK(doc.Save(v,"",&retry) && retry.live==0);
    // Empty documents, exact byte bound, no truncation and line structure.
    doc.New();CHECK(doc.Dirty() && doc.Save(v,"C:\\empty.txt") && fs::file_size(root/"empty.txt")==0);
    CHECK(doc.InsertAfter(0) && doc.Edit(1,"last") && doc.InsertAfter(0) && doc.Edit(1,"middle") && doc.Erase(0));
    CHECK(doc.Save(v,"C:\\lines.txt") && Bytes(root/"lines.txt")=="middle\r\nlast");
    Write(root/"limit.txt",std::string(Doc::MaxBytes,'x'));CHECK(doc.Open(v,"C:\\limit.txt") && !doc.InsertAfter(0) && !doc.Dirty());
    CHECK(doc.Save(v,"C:\\limit-copy.txt") && fs::file_size(root/"limit-copy.txt")==Doc::MaxBytes);
    // Real shell controls: new -> edit -> insert -> save -> close -> fresh reopen.
    ui::FontCache::SetDirectory(argc>1?argv[1]:"../../romfs/fonts");
    ui::FontDesc font;font.face="Liberation Sans";font.height=-16;CHECK(!ui::FontCache::Get(font)->Metrics().fixedPitch);Shell shell;std::string error;
    CHECK(shell.ConfigureFilesystem((root/"system").string(),"",rom.string(),error));shell.Enter();shell.SetGuestActive("public test guest");Click(shell,Shell::Documents);
    Menu(shell,Shell::TextNew,true);CHECK(shell.ActiveDialog()==Shell::Dialog::TextEditor);
    Click(shell,Shell::TextEdit);CHECK(shell.PendingTextInput()==Shell::TextInput::TextLine);shell.CompleteTextInput(true,"Первая 😀");
    Click(shell,Shell::TextInsert);CHECK(shell.TextSelection()==1);Click(shell,Shell::TextEdit);shell.CompleteTextInput(true,"Вторая строка");
    shell.ToggleStart();CHECK(!shell.StartOpen());Click(shell,Shell::GuestTask);CHECK(shell.ModalActive());
    shell.Back();CHECK(shell.ActiveDialog()==Shell::Dialog::TextClose && shell.Focus()==Shell::CancelDialog);shell.Back();CHECK(shell.Document().Dirty());
    for(int i=0;i<12 && shell.Focus()!=Shell::TextSaveAs;++i)shell.NextFocus();
    CHECK(shell.Focus()==Shell::TextSaveAs);shell.Accept();
    const std::string saved="C:\\Documents and Settings\\Player\\My Documents\\заметка 😀.txt";
    shell.CompleteTextInput(false,"");CHECK(shell.Document().Dirty());Click(shell,Shell::TextSaveAs);shell.CompleteTextInput(true,saved);CHECK(!shell.Document().Dirty());
    const auto actual=root/"system/Documents and Settings/Player/My Documents/заметка 😀.txt";
    CHECK(Bytes(actual)=="Первая 😀\r\nВторая строка");
    if(argc>2){ui::OwnedSurface pixels;pixels.Allocate(1280,720);shell.Render(pixels.s,u"14:32");FILE* f=std::fopen(argv[2],"wb");CHECK(f);std::fprintf(f,"P6\n1280 720\n255\n");for(int y=0;y<720;++y)for(int x=0;x<1280;++x){auto c=pixels.s.Get(x,y);unsigned char rgb[]={uint8_t(c>>16),uint8_t(c>>8),uint8_t(c)};CHECK(std::fwrite(rgb,1,3,f)==3);}CHECK(std::fclose(f)==0);}
    Click(shell,Shell::CancelDialog);CHECK(!shell.ModalActive());Menu(shell,Shell::TextOpen);CHECK(shell.Document().Lines()[1].text==u"Вторая строка");
    Click(shell,Shell::TextEdit);shell.CompleteTextInput(true,"changed");Click(shell,Shell::TextSave);CHECK(!shell.Document().Dirty() && !shell.Document().BackupPath().empty());
    // Native dirty buffer survives ordinary guest exit/re-entry; explicit discard only.
    Click(shell,Shell::TextEdit);shell.CompleteTextInput(true,"pending");shell.EndGuest();shell.Enter();CHECK(shell.Document().Dirty() && shell.ActiveDialog()==Shell::Dialog::TextEditor);
    shell.Back();Click(shell,Shell::TextDiscard);CHECK(!shell.ModalActive() && Bytes(actual)=="changed\r\nВторая строка");
    Menu(shell,Shell::TextOpen);Click(shell,Shell::TextEdit);shell.CompleteTextInput(true,"closed save");shell.Back();Click(shell,Shell::TextSave);
    CHECK(!shell.ModalActive() && Bytes(actual)=="closed save\r\nВторая строка");
    Doc restarted;CHECK(restarted.Open(shell.Browser().Filesystem(),saved) && restarted.Lines()[0].text==u"closed save");
    Write(actual,std::string(600,'x')+"tail");Menu(shell,Shell::TextOpen);
    CHECK(shell.Document().Lines()[0].text.size()==604 && !shell.Enabled(Shell::TextEdit));
    Click(shell,Shell::TextEdit);CHECK(shell.PendingTextInput()==Shell::TextInput::None);
    shell.Page(1);shell.Page(-1);Click(shell,Shell::CancelDialog);CHECK(!shell.ModalActive() && Bytes(actual).size()==604);
    puts("PASSED: native TXT create/read/edit/save/reopen, UTF/BOM/line preservation, Horizon-style backup publication and injected disk failures");
}
