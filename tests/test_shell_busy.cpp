#include "source/runtime/guest_session.h"
#include "source/runtime/guest_execution.h"
#include "source/runtime/clock.h"
#include "source/cpu/interpreter.h"
#include "source/win32/shell_session.h"
#include "source/win32/presentation_session.h"
#include "source/ui/font.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>
namespace user=win32::user;
using IE=user::InputEvent;using Shell=ui::DesktopShell;using Bridge=user::ShellSession;
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"busy shell line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static std::vector<IE> pending;
static unsigned frameCount=0;
static std::u16string textReply;
static std::string storageRoot;
static void Event(IE::Type type,int x=0,int y=0,uint32_t vk=0) {IE e;e.type=type;e.x=x;e.y=y;e.vk=vk;pending.push_back(e);}
static void Click(Bridge& b,int target) {
    const auto r=Shell::Bounds(target);const int x=(r.left+r.right)/2,y=(r.top+r.bottom)/2;
    Event(IE::MouseDown,x,y);Event(IE::MouseUp,x,y);CHECK(b.ServiceHost()==Bridge::HostStatus::Running);
}
static void Start(Bridge& b) {Event(IE::KeyDown,0,0,0x5B);Event(IE::KeyUp,0,0,0x5B);CHECK(b.ServiceHost()==Bridge::HostStatus::Running);}
static void Right(Bridge& bridge,const ui::Rect& r) {
    IE event;event.type=IE::MouseDown;event.button=1;event.x=r.left+10;event.y=r.top+10;pending.push_back(event);
    event.type=IE::MouseUp;pending.push_back(event);CHECK(bridge.ServiceHost()==Bridge::HostStatus::Running);
}
static void MenuClick(Bridge& bridge,Shell& shell,int action) {
    for(size_t i=0;i<shell.ContextCount();++i)if(shell.ContextAction(i)==action) {
        const auto r=shell.ContextBounds(Shell::ContextBase+int(i));
        Event(IE::MouseDown,r.left+10,r.top+10);Event(IE::MouseUp,r.left+10,r.top+10);
        CHECK(bridge.ServiceHost()==Bridge::HostStatus::Running);return;
    }
    CHECK(false);
}
static void Run(Shell& shell,const char* exe,int mode) {
    rt::GuestSession session;auto& p=session.ProcessObject();auto& m=session.Memory();
    user::PresentationSession platform(true,nullptr);
    user::g_platform.wait=[](int,std::vector<IE>& events){events.swap(pending);return true;};
    user::g_platform.textInput=[](const std::u16string&,bool,bool,std::u16string& out){out=textReply;return true;};
    user::g_platform.present=[](const ui::Surface& s){CHECK(s.Valid());++frameCount;};
    user::EnableUi(1280,720);Bridge bridge(shell);CHECK(bridge.Attach("Busy public application"));
    std::string error;CHECK(p.LoadMainExe(exe,error));CHECK(p.PrepareMainThread(p.MainThread().cpu,error));
    auto exportAt=[&](const char* name){auto a=p.GetProcAddress(p.MainModule(),name);CHECK(a);return a;};
    const auto stage=exportAt("BusyStage"),resume=exportAt("BusyResume"),finish=exportAt("BusyFinish"),commands=exportAt("BusyCommands");
    m.Write32(exportAt("BusyWorker"),mode==6?1:0);
    const auto owner=exportAt("BusyOwner");
    const auto async=exportAt("BusyAsync"),pollMode=exportAt("BusyMode"),cursorX=exportAt("BusyCursorX"),cursorY=exportAt("BusyCursorY");
    const auto clicks=exportAt("BusyClicks"),keys=exportAt("BusyKeys"),wrong=exportAt("BusyWrongThread"),heartbeat=exportAt("BusyHeartbeat");
    rt::GuestExecution execution(p,[&](rt::GuestThread& t) {
        win32::HostCallOutcome out;if(win32::TryHandleHostCall(t.cpu,m,out))return !out.requestExit;
        cpu::Interpreter cpu(m,t.teb,t.teb);CHECK(cpu.Step(t.cpu)==cpu::StepResult::Continue);return true;
    },rt::GuestExecution::Startup::InitializeModules);
    bool inspected=false,done=false,restorePosted=false,asyncReleased=false,finishRequested=false;uint32_t hwnd=0,previousBeat=0;
    for(unsigned i=0;i<500000;++i) {
        const auto result=execution.RunOne();CHECK(result!=rt::GuestExecution::Result::AllocationFailed);
        CHECK(!rt::GuestExecution::Active() && !user::G().c && !user::G().m);
        if(m.Read32(stage)==1 && !inspected) {
            inspected=true;CHECK(user::Desktop()->children.size()==1);
            if(mode==6)CHECK(m.Read32(owner)!=p.MainThread().id);
            hwnd=user::Desktop()->children[0];
            CHECK(bridge.ServiceHost()==Bridge::HostStatus::Running);Start(bridge);CHECK(shell.StartOpen());
            const auto before=frameCount;user::PresentIfNeeded(true);CHECK(frameCount>before);
            Click(bridge,Shell::Settings);CHECK(shell.ActiveDialog()==Shell::Dialog::Preferences);
            ui::OwnedSurface nativeSettings;nativeSettings.Allocate(1280,720);shell.Render(nativeSettings.s,u"");
            const auto& settingsFrame=bridge.Compose(user::S().screen.s);
            for(int y=196;y<552;++y)for(int x=396;x<1034;++x)CHECK(settingsFrame.Get(x,y)==nativeSettings.s.Get(x,y));
            Click(bridge,Shell::PreferenceSort);CHECK(shell.Browser().PreferenceStatus()==ui::ShellPreferenceStore::Status::Saved);
            Click(bridge,Shell::CancelDialog);CHECK(!shell.ModalActive());Start(bridge);CHECK(shell.StartOpen());
            Click(bridge,Shell::MenuComputer);CHECK(shell.ActivePanel()==Shell::Panel::Explorer);
            CHECK(bridge.PendingGuestInput()==0 && user::Q().input.empty() && m.Read32(keys)==0);
            // Native file actions operate on actual storage even while this x86
            // app does not pump USER. No callback or second guest is required.
            Click(bridge,Shell::QuickC);textReply=u"session-folder";Click(bridge,Shell::NewFolder);
            CHECK(std::filesystem::is_directory(storageRoot+"/session-folder"));
            Click(bridge,Shell::Properties);CHECK(shell.ActiveDialog()==Shell::Dialog::Properties);
            CHECK(shell.PropertiesSnapshot().kind==rt::FileProperties::Kind::Directory);
            const auto postedBefore=user::QueueFor(user::Win(hwnd)->threadId).posted.size();
            Start(bridge);CHECK(!shell.StartOpen() && shell.ModalActive());
            Click(bridge,Shell::GuestTask);CHECK(shell.ModalActive());
            CHECK(user::QueueFor(user::Win(hwnd)->threadId).posted.size()==postedBefore);
            Event(IE::MouseDown,400,250);Event(IE::MouseUp,400,250);
            Event(IE::KeyDown,0,0,'J');Event(IE::KeyUp,0,0,'J');
            Event(IE::KeyDown,0,0,0x1B);Event(IE::KeyUp,0,0,0x1B);
            CHECK(bridge.ServiceHost()==Bridge::HostStatus::Running && !shell.ModalActive());
            CHECK(bridge.PendingGuestInput()==0 && user::Q().input.empty() && m.Read32(keys)==0 && m.Read32(clicks)==0);
            textReply=u"renamed-session-folder";Click(bridge,Shell::Rename);
            CHECK(!std::filesystem::exists(storageRoot+"/session-folder") && std::filesystem::is_directory(storageRoot+"/renamed-session-folder"));
            Click(bridge,Shell::Delete);CHECK(shell.Focus()==Shell::CancelDialog);
            Event(IE::KeyDown,0,0,0x0D);Event(IE::KeyUp,0,0,0x0D);CHECK(bridge.ServiceHost()==Bridge::HostStatus::Running);
            CHECK(!shell.ModalActive() && std::filesystem::is_directory(storageRoot+"/renamed-session-folder"));
            Click(bridge,Shell::Delete);Click(bridge,Shell::ConfirmDelete);
            CHECK(!std::filesystem::exists(storageRoot+"/renamed-session-folder"));
            Click(bridge,Shell::Refresh);
            bool selectedSource=false;
            for(size_t n=0;n<shell.Browser().Entries().size();++n)if(shell.Browser().Entries()[n].name=="clipboard-source.bin") {
                CHECK(n<Shell::Rows);Click(bridge,Shell::BrowserRowBase+int(n));selectedSource=true;break;
            }
            CHECK(selectedSource);
            const auto sourceRow=Shell::Bounds(Shell::BrowserRowBase+int(shell.Browser().Selected()%Shell::Rows));
            Right(bridge,sourceRow);CHECK(shell.ContextOpen());
            const auto contextPosts=user::QueueFor(user::Win(hwnd)->threadId).posted.size();
            Click(bridge,Shell::GuestTask);CHECK(!shell.ContextOpen() && !(user::Win(hwnd)->style&user::WS_MINIMIZE));
            CHECK(user::QueueFor(user::Win(hwnd)->threadId).posted.size()==contextPosts);
            Right(bridge,sourceRow);CHECK(shell.ContextOpen());
            Event(IE::KeyDown,0,0,0x5D);Event(IE::KeyUp,0,0,0x5D);CHECK(bridge.ServiceHost()==Bridge::HostStatus::Running && !shell.ContextOpen());
            Event(IE::KeyDown,0,0,0x5D);Event(IE::KeyUp,0,0,0x5D);CHECK(bridge.ServiceHost()==Bridge::HostStatus::Running && shell.ContextOpen());
            Start(bridge);CHECK(shell.ContextOpen() && !shell.StartOpen());
            MenuClick(bridge,shell,Shell::Copy);CHECK(!shell.ContextOpen());Click(bridge,Shell::QuickDocuments);
            textReply=u"busy-copy.bin";Click(bridge,Shell::Paste);CHECK(shell.CopyRunning());
            const auto beatBeforeCopy=m.Read32(heartbeat);
            unsigned copyPolls=0;
            while(shell.CopyRunning()) {
                CHECK(++copyPolls<100);execution.RunOne();CHECK(!rt::GuestExecution::Active());
                CHECK(bridge.ServiceHost()==Bridge::HostStatus::Running);
            }
            CHECK(copyPolls>1 && m.Read32(heartbeat)!=beatBeforeCopy);
            const auto copiedPath=storageRoot+"/Documents and Settings/Player/My Documents/busy-copy.bin";
            CHECK(std::filesystem::file_size(copiedPath)==200003);
            std::ifstream copied(copiedPath,std::ios::binary);const std::string actual{std::istreambuf_iterator<char>(copied),{}};
            CHECK(actual==std::string(200003,'q'));copied.close();
            Click(bridge,Shell::Delete);Click(bridge,Shell::ConfirmDelete);CHECK(!std::filesystem::exists(copiedPath));
            textReply=u"cancel-copy.bin";Click(bridge,Shell::Paste);CHECK(shell.CopyRunning());
            Click(bridge,Shell::CancelDialog);CHECK(!shell.CopyRunning());
            CHECK(!std::filesystem::exists(storageRoot+"/Documents and Settings/Player/My Documents/cancel-copy.bin"));
            CHECK(bridge.PendingGuestInput()==0 && user::Q().input.empty() && m.Read32(keys)==0 && m.Read32(clicks)==0);
            // The actual native TXT editor uses the same F1 storage while x86
            // continues spinning. Its modal events must never reach guest USER.
            Click(bridge,Shell::FolderActions);MenuClick(bridge,shell,Shell::TextNew);
            CHECK(shell.ActiveDialog()==Shell::Dialog::TextEditor);
            textReply=u"busy public text";Click(bridge,Shell::TextEdit);
            CHECK(shell.Document().Dirty());const auto textBeat=m.Read32(heartbeat);
            for(unsigned step=0;step<16;++step)execution.RunOne();
            CHECK(m.Read32(heartbeat)!=textBeat);
            ui::OwnedSurface nativeText;nativeText.Allocate(1280,720);shell.Render(nativeText.s,u"");
            const auto& textFrame=bridge.Compose(user::S().screen.s);
            for(int y=196;y<552;++y)for(int x=396;x<1034;++x)CHECK(textFrame.Get(x,y)==nativeText.s.Get(x,y));
            Start(bridge);CHECK(!shell.StartOpen() && shell.ModalActive());
            textReply=u"C:\\Documents and Settings\\Player\\My Documents\\busy-note.txt";
            const auto notePath=storageRoot+"/Documents and Settings/Player/My Documents/busy-note.txt";
            std::filesystem::remove(notePath); // independently reset this public fixture per session
            Click(bridge,Shell::TextSaveAs);CHECK(!shell.Document().Dirty());
            std::ifstream note(notePath,std::ios::binary);CHECK(std::string(std::istreambuf_iterator<char>(note),{})=="busy public text");note.close();
            Click(bridge,Shell::CancelDialog);CHECK(!shell.ModalActive());
            Click(bridge,Shell::Delete);Click(bridge,Shell::ConfirmDelete);CHECK(!std::filesystem::exists(notePath));
            CHECK(bridge.PendingGuestInput()==0 && user::Q().input.empty() && m.Read32(keys)==0 && m.Read32(clicks)==0);
            Click(bridge,Shell::Close);
            // A guest-owned right gesture retains ownership over native chrome.
            IE right;right.type=IE::MouseDown;right.button=1;right.x=400;right.y=250;CHECK(!bridge.Filter(right));
            right.type=IE::MouseMove;right.x=20;right.y=700;CHECK(!bridge.Filter(right));
            right.type=IE::MouseUp;CHECK(!bridge.Filter(right));CHECK(!shell.ContextOpen());
            previousBeat=m.Read32(heartbeat);
            if(mode==4)m.Write32(pollMode,1);
            // Queue guest input while it is still executing pure x86 instructions.
            for(unsigned n=0;n<1000;++n) {Event(IE::MouseMove,350+int(n%50),250);CHECK(bridge.ServiceHost()==Bridge::HostStatus::Running);}
            CHECK(bridge.PendingGuestInput()==1); // coalesced only adjacent motion
            Event(IE::MouseDown,400,250);Event(IE::MouseUp,400,250);
            Event(IE::KeyDown,0,0,'K');if(mode!=4)Event(IE::KeyUp,0,0,'K');CHECK(bridge.ServiceHost()==Bridge::HostStatus::Running);
            CHECK(m.Read32(clicks)==0 && m.Read32(keys)==0 && bridge.PendingGuestInput()==(mode==4?4u:5u));
            if(mode==2) {
                for(unsigned n=0;n<Bridge::MaxGuestInput;++n)Event(n%2?IE::KeyUp:IE::KeyDown,0,0,'J');
                CHECK(bridge.ServiceHost()==Bridge::HostStatus::InputOverflow);
                CHECK(bridge.PendingGuestInput()==Bridge::MaxGuestInput && m.Read32(keys)==0);done=true;break;
            }
            if(mode==3) {Event(IE::Quit);CHECK(bridge.ServiceHost()==Bridge::HostStatus::Quit);done=true;break;}
            Click(bridge,Shell::GuestTask);
            CHECK(!(user::Win(hwnd)->style&user::WS_MINIMIZE) && m.Read32(commands)==0);
            auto& posted=user::QueueFor(user::Win(hwnd)->threadId).posted;const auto count=posted.size();CHECK(count==1);
            for(int n=0;n<20;++n)Click(bridge,Shell::GuestTask);
            CHECK(posted.size()==count); // bounded intent while the app is unresponsive
            Start(bridge);CHECK(shell.StartOpen());Start(bridge);CHECK(!shell.StartOpen());
            if(mode==1){done=true;break;} // host abort with a genuinely pending action
        }
        if(inspected && m.Read32(stage)==1 && m.Read32(heartbeat)!=previousBeat) {
            CHECK(m.Read32(commands)==0 && m.Read32(keys)==0 && !(user::Win(hwnd)->style&user::WS_MINIMIZE));
            if(mode==4) {
                if(!asyncReleased && (m.Read32(async)&1) && m.Read32(cursorX)==uint32_t(user::S().cursorX) && m.Read32(cursorY)==uint32_t(user::S().cursorY)) {
                    Event(IE::KeyUp,0,0,'K');CHECK(bridge.ServiceHost()==Bridge::HostStatus::Running);asyncReleased=true;
                }
                if(!asyncReleased || !(m.Read32(async)&2))continue;
            }
            m.Write32(resume,1);
        }
        if(m.Read32(stage)==2 && !restorePosted && m.Read32(commands)==1 && (user::Win(hwnd)->style&user::WS_MINIMIZE)) {
            CHECK(user::Win(hwnd)->style&user::WS_MINIMIZE);CHECK(m.Read32(wrong)==0);
            CHECK(bridge.ServiceHost()==Bridge::HostStatus::Running);Click(bridge,Shell::GuestTask);restorePosted=true;
        }
        if(!finishRequested && restorePosted && m.Read32(commands)==2 && m.Read32(keys)==2 && m.Read32(clicks)==2) {
            const auto* w=user::Win(hwnd);
            if(w && !(w->style&user::WS_MINIMIZE)) {
                CHECK(bridge.PendingGuestInput()==0);m.Write32(finish,1);finishRequested=true;
            }
        }
        if(result==rt::GuestExecution::Result::Stopped) {CHECK(m.Read32(stage)==3 && win32::g_guestExitCode==0);done=true;break;}
        if(result==rt::GuestExecution::Result::Idle)rt::Clock::Sleep(1);
    }
    CHECK(done);pending.clear();
}
int main(int argc,char** argv) {
    CHECK(argc==2);ui::FontCache::SetDirectory("romfs/fonts");rt::Clock::SetFastForward(true);
    char name[]="/tmp/sr-shell-busy-XXXXXX";CHECK(mkdtemp(name));
    struct Cleanup{std::string path;~Cleanup(){std::filesystem::remove_all(path);}} cleanup{name};
    storageRoot=name;std::ofstream(storageRoot+"/clipboard-source.bin",std::ios::binary)<<std::string(200003,'q');
    Shell shell;std::string error;CHECK(shell.ConfigureFilesystem(name,"","",error));
    for(int mode:{0,1,2,3,4,6,0}) {Run(shell,argv[1],mode);CHECK(!shell.GuestActive() && !rt::g_process && !user::g_platform.wait);}
    std::puts("busy shell: actual x86 spin, independent native input/output, deferred owner commands, exact input pairs, bounded overflow, quit/abort/restart PASS");
}
