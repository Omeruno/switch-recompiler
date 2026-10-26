#include "source/ui/desktop_shell.h"
#include "source/ui/font.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <unistd.h>
using Shell=ui::DesktopShell;
static void Click(Shell& s,int target){auto r=s.ControlBounds(target);assert(!r.Empty());const int x=r.left+r.Width()/2,y=r.top+r.Height()/2;s.PointerDown(x,y);s.PointerUp(x,y);}
static void Move(Shell& s,ui::Rect r,int dx,int dy,bool cancel=false){s.PointerDown(r.left+70,r.top+12);assert(s.DraggingWindow());s.PointerMove(r.left+70+dx,r.top+12+dy);if(cancel)s.CancelPointer();else s.PointerUp(r.left+70+dx,r.top+12+dy);}
static void Resize(Shell& s,ui::Rect r,int dx,int dy){s.PointerDown(r.right-2,r.bottom-2);assert(s.DraggingWindow());s.PointerMove(r.right-2+dx,r.bottom-2+dy);s.PointerUp(r.right-2+dx,r.bottom-2+dy);}
static void Action(Shell& s,int action,bool folder=false){s.OpenContext(folder);assert(s.ContextOpen());for(size_t i=0;i<s.ContextCount();++i)if(s.ContextAction(i)==action){auto r=s.ContextBounds(Shell::ContextBase+int(i));s.PointerDown(r.left+5,r.top+5);s.PointerUp(r.left+5,r.top+5);return;}assert(false);}
static void Bitmap(const std::filesystem::path& p){unsigned char b[78]={};auto w16=[&](int at,unsigned v){b[at]=v;b[at+1]=v>>8;};auto w32=[&](int at,unsigned v){for(int i=0;i<4;++i)b[at+i]=v>>(8*i);};b[0]='B';b[1]='M';w32(2,78);w32(10,54);w32(14,40);w32(18,4);w32(22,2);w16(26,1);w16(28,24);w32(34,24);for(int i=54;i<78;i+=3){b[i]=0x33;b[i+1]=0x66;b[i+2]=0x99;}std::ofstream f(p,std::ios::binary);f.write(reinterpret_cast<char*>(b),sizeof(b));assert(f.good());}
int main(int argc,char** argv){
    ui::FontCache::SetDirectory(argc>1?argv[1]:"../../romfs/fonts");
    ui::FontDesc font;font.face="Liberation Sans";font.height=-16;assert(!ui::FontCache::Get(font)->Metrics().fixedPitch);char temp[]="/tmp/sr-native-windows-XXXXXX";assert(mkdtemp(temp));const std::filesystem::path root=temp;
    struct Cleanup{std::filesystem::path p;~Cleanup(){std::filesystem::remove_all(p);}} cleanup{root};
    auto snapshot=[&](Shell& shell,const char* name){
        if(argc<3)return;
        ui::OwnedSurface image;image.Allocate(1280,720);shell.Render(image.s,u"12:34");
        const auto path=std::string(argv[2])+name+".ppm";FILE* f=std::fopen(path.c_str(),"wb");assert(f);std::fprintf(f,"P6\n1280 720\n255\n");
        for(int y=0;y<720;++y)for(int x=0;x<1280;++x){const auto p=image.s.Get(x,y);const unsigned char rgb[]={uint8_t(p>>16),uint8_t(p>>8),uint8_t(p)};assert(std::fwrite(rgb,1,3,f)==3);}
        assert(std::fclose(f)==0);
    };
    Shell s;std::string error;assert(s.ConfigureFilesystem(root.string(),"","",error));
    for(int i=0;i<40;++i){std::ofstream f(root/"Programs"/("probe"+std::to_string(i)+".exe"));f<<"public inventory entry";}
    ui::ExecutableCatalog catalog;catalog.ScanRoot((root/"Programs").string()+"/",0);s.SetCatalog(catalog);s.Enter();
    Click(s,Shell::Programs);assert(s.PanelBounds()==Shell::PanelArea() && s.VisibleRows()==14);
    s.Move(35);const auto selection=s.Selection();Resize(s,s.PanelBounds(),-242,-108);
    assert(s.PanelBounds().Width()==860 && s.PanelBounds().Height()==560 && s.VisibleRows()==11 && s.Selection()==selection);
    assert(s.ControlBounds(Shell::RowBase+11).Empty());assert(s.HitTest(s.ControlBounds(Shell::RowBase+2).left+10,s.ControlBounds(Shell::RowBase+2).top+10)==Shell::RowBase+2);
    const auto before=s.PanelBounds();Move(s,before,40,35);assert(s.PanelBounds()==before.Offset(40,35));const auto moved=s.PanelBounds();snapshot(s,"programs-resized");
    Move(s,moved,-20,-20,true);assert(s.PanelBounds()==moved);
    // Every edge/corner changes a real rectangle; cancellation restores exactly.
    for(int edge=0;edge<8;++edge){auto r=s.PanelBounds();int x=r.left+60,y=r.top+60;
        if(edge==0||edge==3||edge==6)x=r.left+1;
        if(edge==1||edge==4||edge==7)x=r.right-2;
        if(edge==2||edge==3||edge==4)y=r.top+1;
        if(edge==5||edge==6||edge==7)y=r.bottom-2;
        s.PointerDown(x,y);assert(s.DraggingWindow());s.PointerMove(x+(edge==1||edge==4||edge==7?10:-10),y+(edge==5||edge==6||edge==7?10:-10));assert(s.PanelBounds()!=r);s.CancelPointer();assert(s.PanelBounds()==r);
    }
    Click(s,Shell::Maximize);assert(s.PanelBounds()==ui::Rect(0,0,1280,680)&&s.NativeMaximized());Click(s,Shell::Minimize);
    assert(s.NativeMinimized()&&s.PanelBounds().Empty()&&s.ActivePanel()==Shell::Panel::Desktop);Click(s,Shell::NativeTask);assert(s.NativeMaximized());
    Click(s,Shell::Maximize);assert(s.PanelBounds()==moved&&s.Selection()==selection);Click(s,Shell::NativeTask);Click(s,Shell::Start);assert(s.StartOpen() && s.NativeMinimized());s.Back();assert(!s.StartOpen() && s.NativeMinimized());Click(s,Shell::NativeTask);assert(s.PanelBounds()==moved);
    auto close=s.ControlBounds(Shell::Close);s.PointerDown(close.left+5,close.top+5);s.PointerUp(10,10);assert(s.ActivePanel()==Shell::Panel::Programs);
    Click(s,Shell::Close);assert(s.ActivePanel()==Shell::Panel::Desktop&&!s.Enabled(Shell::NativeTask));Click(s,Shell::Programs);assert(s.PanelBounds()==moved);Click(s,Shell::Close);
    Click(s,Shell::Documents);assert(s.ActivePanel()==Shell::Panel::Explorer);const auto docs=s.Browser().Path();Resize(s,s.PanelBounds(),-242,-108);assert(s.VisibleRows()==10);
    assert(s.ControlBounds(Shell::BrowserRowBase+10).Empty());Action(s,Shell::TextNew,true);assert(s.ActiveDialog()==Shell::Dialog::TextEditor);
    Click(s,Shell::TextEdit);s.CompleteTextInput(true,"real saved text");assert(s.Document().Dirty());
    auto document=s.DialogBounds();Move(s,document,-70,-80);document=s.DialogBounds();Click(s,Shell::DialogMaximize);assert(s.DialogBounds()==ui::Rect(0,0,1280,680)&&s.VisibleTextRows()==16);
    snapshot(s,"editor-maximized");
    for(int i=0;i<18;++i)Click(s,Shell::TextInsert);
    assert(s.TextSelection()==18);assert(s.HitTest(s.ControlBounds(Shell::TextLineBase+2).left+5,s.ControlBounds(Shell::TextLineBase+2).top+5)==Shell::TextLineBase+2);
    Click(s,Shell::DialogMinimize);assert(!s.ModalActive()&&s.DialogBounds().Empty()&&s.Document().Dirty());Click(s,Shell::NativeTask);
    assert(s.ActiveDialog()==Shell::Dialog::TextEditor&&s.NativeMaximized()&&s.TextSelection()==18);Click(s,Shell::DialogMaximize);assert(s.DialogBounds()==document&&s.VisibleTextRows()==5);
    Resize(s,document,80,60);assert(s.DialogBounds().Width()==712&&s.VisibleTextRows()==7);Click(s,Shell::DialogClose);assert(s.ActiveDialog()==Shell::Dialog::TextClose&&s.Document().Dirty());
    Click(s,Shell::DialogClose);assert(s.ActiveDialog()==Shell::Dialog::TextEditor);Click(s,Shell::TextSaveAs);assert(s.PendingTextInput()==Shell::TextInput::TextSavePath);
    s.CompleteTextInput(true,docs+"\\window.txt");assert(!s.Document().Dirty());std::ifstream file(s.Browser().Filesystem().ToHost(s.Document().Path()),std::ios::binary);
    const std::string savedBytes{std::istreambuf_iterator<char>(file),{}};assert(savedBytes.find("real saved text")==0);
    Click(s,Shell::DialogClose);assert(s.ActiveDialog()==Shell::Dialog::None);
    const auto hostDocs=root/"Documents";std::filesystem::create_directories(hostDocs);Bitmap(hostDocs/"window.bmp");
    // Native navigation to the same real directory, independent of mounted-root defaults.
    Click(s,Shell::Address);s.CompleteTextInput(true,"C:\\Documents");
    size_t index=0;while(index<s.Browser().Entries().size()&&s.Browser().Entries()[index].name!="window.bmp")++index;assert(index<s.Browser().Entries().size());
    for(size_t i=0;i<index;++i)s.Move(1);
    Action(s,Shell::ImageOpen);assert(s.ActiveDialog()==Shell::Dialog::ImageViewer);
    const auto image=s.DialogBounds();Click(s,Shell::DialogMaximize);assert(s.DocumentViewport().Width()==1238&&s.DocumentViewport().Height()==542);
    ui::OwnedSurface pixels;pixels.Allocate(1280,720);s.Render(pixels.s,u"12:34");auto viewport=s.DocumentViewport();assert(pixels.s.Get((viewport.left+viewport.right)/2,(viewport.top+viewport.bottom)/2)==0x996633);
    snapshot(s,"bitmap-maximized");
    Click(s,Shell::DialogMinimize);assert(s.Image().Width()==4&&s.NativeMinimized());Click(s,Shell::NativeTask);Click(s,Shell::DialogMaximize);assert(s.DialogBounds()==image);
    Click(s,Shell::DialogClose);assert(s.ActiveDialog()==Shell::Dialog::None);Click(s,Shell::Close);s.Enter();assert(!s.DraggingWindow());
    Click(s,Shell::Start);Click(s,Shell::About);assert(s.ActivePanel()==Shell::Panel::About);
    const auto about=s.PanelBounds();Resize(s,about,-242,-108);const auto smallAbout=s.PanelBounds();
    Click(s,Shell::Maximize);Click(s,Shell::Minimize);Click(s,Shell::NativeTask);assert(s.NativeMaximized());Click(s,Shell::Maximize);assert(s.PanelBounds()==smallAbout);
    s.PointerDown(smallAbout.left+70,smallAbout.top+12);s.PointerMove(smallAbout.left+90,smallAbout.top+32);assert(s.DraggingWindow());
    s.SetGuestActive("public lifecycle probe");assert(!s.DraggingWindow() && s.ActivePanel()==Shell::Panel::Desktop);
    s.EndGuest();assert(s.ActivePanel()==Shell::Panel::About && s.PanelBounds()==smallAbout);
    Click(s,Shell::Close);s.Enter();assert(s.ActivePanel()==Shell::Panel::Desktop && !s.Enabled(Shell::NativeTask));
    puts("Native windows: PASS (panel/document geometry, all edges, reflow/pages, max/min/task restore, dirty close/save, real BMP viewport, cancellation)");
}
