#include "source/ui/xp_theme.h"
#include "source/runtime/guest_session.h"
#include "source/runtime/guest_execution.h"
#include "source/runtime/clock.h"
#include "source/cpu/interpreter.h"
#include "source/win32/shell_session.h"
#include "source/win32/presentation_session.h"
#include "source/ui/presentation.h"
#include "source/ui/font.h"
#include "workspace_layer_oracle.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
namespace user=win32::user;
using IE=user::InputEvent;using Shell=ui::DesktopShell;
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"workspace line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static std::vector<IE> pending;
static WorkspaceLayerOracle* oracle=nullptr;
static std::function<void()> inspection;
static void Send(IE::Type type,int x=0,int y=0,uint32_t vk=0) {
    IE e;e.type=type;e.x=x;e.y=y;e.vk=vk;pending.push_back(e);
}
static void Flush(){
    user::PollQueueEvents();user::Msg msg;
    while(user::FetchMessage(msg,0,user::WM_SYSCOMMAND,user::WM_SYSCOMMAND,true,false))
        user::SendMsg(msg.hwnd,msg.message,msg.wParam,msg.lParam);
}
static void Click(Shell& shell,int target){const auto r=shell.ControlBounds(target);const int x=(r.left+r.right)/2,y=(r.top+r.bottom)/2;Send(IE::MouseDown,x,y);Send(IE::MouseUp,x,y);Flush();}
static void Start(){Send(IE::KeyDown,0,0,0x5B);Send(IE::KeyUp,0,0,0x5B);Flush();}
static void Snapshot(const ui::Surface& s,const char* path) {
    if(!path)return;
    FILE* f=std::fopen(path,"wb");CHECK(f);std::fprintf(f,"P6\n%d %d\n255\n",s.width,s.height);
    for(int y=0;y<s.height;++y)for(int x=0;x<s.width;++x){auto p=s.Get(x,y);unsigned char rgb[]={uint8_t(p>>16),uint8_t(p>>8),uint8_t(p)};CHECK(std::fwrite(rgb,1,3,f)==3);}
    std::fclose(f);
}
static void Run(Shell& shell,const char* exe,bool abort,const char* screenshot) {
    const auto previous=user::g_platform;
    {
        rt::GuestSession guest;auto& p=guest.ProcessObject();auto& m=guest.Memory();
        user::PresentationSession platform(true,nullptr);
        user::g_platform.wait=[](int,std::vector<IE>& out){out.swap(pending);return true;};
        user::EnableUi(1280,720);
        user::ShellSession bridge(shell);CHECK(bridge.Attach("Public workspace application"));bridge.SetClock(u"14:32");
        auto checkLayers=[&](){const auto& canonical=bridge.Compose(user::S().screen.s);const auto scene=bridge.Scene();oracle->Check(canonical,scene);};
        CHECK(user::EnableWindowLayers(true)); // background writes also invalidate canonical ownership
        std::string error;CHECK(p.LoadMainExe(exe,error));CHECK(p.PrepareMainThread(p.MainThread().cpu,error));
        const auto stage=p.GetProcAddress(p.MainModule(),"WorkspaceStage"),finish=p.GetProcAddress(p.MainModule(),"WorkspaceFinish");
        const auto clicks=p.GetProcAddress(p.MainModule(),"WorkspaceClicks"),keys=p.GetProcAddress(p.MainModule(),"WorkspaceKeys");CHECK(stage&&finish&&clicks&&keys);
        bool inspected=false,finished=false;
        bool requestAbort=false;
        inspection=[&]() {
            if(!inspected && m.Read32(stage)==1) {
                inspected=true;auto* desk=user::Desktop();CHECK(desk&&desk->children.size()==2);
                const auto handles=desk->children;std::vector<ui::Rect> rects;
                for(auto h:handles)rects.push_back(user::Win(h)->rect);
                CHECK(user::S().screen.s.Get(1220,200)==ui::xp::WallpaperPixel(1280,680,1220,200));
                const auto& image=bridge.Compose(user::S().screen.s);CHECK(image.Get(1220,200)==ui::xp::WallpaperPixel(1280,680,1220,200));
                Snapshot(image,screenshot);oracle->Check(image,bridge.Scene());
                // Actual production ingestion filters both keyboard edges before USER queues.
                const auto screenChanges=ui::g_gdi.ScreenChangeCounter();
                const auto revision=user::g_platform.shellRevision(user::g_platform.shellContext);
                Start();CHECK(shell.StartOpen());CHECK(user::Q().input.empty());
                checkLayers();
                CHECK(ui::g_gdi.ScreenChangeCounter()==screenChanges);
                CHECK(user::g_platform.shellRevision(user::g_platform.shellContext)>revision);
                Start();CHECK(!shell.StartOpen());
                Click(shell,Shell::GuestTask);for(auto h:handles)CHECK(user::Win(h)->style&user::WS_MINIMIZE);
                checkLayers();
                CHECK(user::S().screen.s.Get(400,250)==ui::xp::WallpaperPixel(1280,680,400,250));
                Click(shell,Shell::GuestTask);
                for(size_t n=0;n<handles.size();++n)CHECK(user::Win(handles[n])->rect==rects[n]);
                // Maximize respects taskbar; restore remembers exact pre-maximized geometry.
                user::ShowWindowInternal(handles[0],3);CHECK(user::ScreenClientRect(user::Win(handles[0])).bottom<=680);
                user::ShowWindowInternal(handles[0],6);user::ShowWindowInternal(handles[0],9);
                CHECK(user::Win(handles[0])->style&user::WS_MAXIMIZE);
                user::ShowWindowInternal(handles[0],9);CHECK(user::Win(handles[0])->rect==rects[0]);
                // No click-through; shell explorer uses its host VFS and cannot launch another EXE.
                Start();Click(shell,Shell::MenuComputer);CHECK(shell.ActivePanel()==Shell::Panel::Explorer);
                CHECK(!shell.Enabled(Shell::MenuRun));CHECK(!shell.Enabled(Shell::Exit));
                // Luna's expanded frame must be composed in full, including
                // the caption/right/bottom areas outside the old panel box.
                ui::OwnedSurface expectedChrome;expectedChrome.Allocate(1280,720);shell.Render(expectedChrome.s,u"14:32");
                const auto& luna=bridge.Compose(user::S().screen.s);const auto panel=shell.PanelBounds();
                for(int y=panel.top;y<panel.bottom;++y)for(int x=panel.left;x<panel.right;++x)
                    CHECK(luna.Get(x,y)==expectedChrome.s.Get(x,y));
                const auto scene=bridge.Scene();
                for(auto point:{ui::Point{panel.left+1,panel.top+1},ui::Point{panel.right-2,panel.bottom-2}})
                    CHECK(scene.owners[size_t(point.y)*scene.width+point.x]==scene.layers[scene.count-1].token);
                // Cover a real guest client area that used to extend beyond
                // the old overlay; clicks must remain native-owned there.
                user::ShowWindowInternal(handles[0],3); // maximize under native Explorer
                checkLayers();
                const auto beforeClicks=m.Read32(clicks);
                Send(IE::MouseDown,panel.right-12,300);Send(IE::MouseUp,panel.right-12,300);Flush();
                CHECK(user::Q().input.empty() && m.Read32(clicks)==beforeClicks);
                user::ShowWindowInternal(handles[0],9);
                checkLayers();
                // Native geometry uses real frame/control coordinates while x86 lives.
                const auto nativeStart=shell.PanelBounds();
                Send(IE::MouseDown,nativeStart.right-2,nativeStart.bottom-2);
                Send(IE::MouseMove,nativeStart.right-244,nativeStart.bottom-110);
                Send(IE::MouseUp,nativeStart.right-244,nativeStart.bottom-110);Flush();
                CHECK(shell.PanelBounds().Width()==860 && shell.PanelBounds().Height()==560);
                const auto small=shell.PanelBounds();const auto moved=small.Offset(40,35);
                const auto frameRevision=user::g_platform.shellRevision(user::g_platform.shellContext);
                Send(IE::MouseDown,small.left+70,small.top+12);
                Send(IE::MouseMove,small.left+110,small.top+47);
                Send(IE::MouseUp,small.left+110,small.top+47);Flush();CHECK(shell.PanelBounds()==moved);
                CHECK(user::g_platform.shellRevision(user::g_platform.shellContext)>frameRevision);
                auto exactNative=[&]() {
                    shell.Render(expectedChrome.s,u"14:32");const auto& frame=bridge.Compose(user::S().screen.s);
                    const auto bounds=shell.PanelBounds(),dialog=shell.DialogBounds();
                    for(int y=0;y<720;++y)for(int x=0;x<1280;++x) {
                        const bool native=y>=680 || bounds.Contains(x,y) || dialog.Contains(x,y);
                        CHECK(frame.Get(x,y)==(native?expectedChrome.s.Get(x,y):user::S().screen.s.Get(x,y)));
                    }
                    oracle->Check(frame,bridge.Scene());
                };
                exactNative();
                Send(IE::MouseDown,moved.left+70,moved.top+12);Send(IE::MouseMove,moved.left+50,moved.top+2);
                Send(IE::KeyDown,0,0,0x1b);Send(IE::KeyUp,0,0,0x1b);Send(IE::MouseUp,moved.left+50,moved.top+2);Flush();
                CHECK(shell.PanelBounds()==moved && !shell.DraggingWindow());exactNative();
                Click(shell,Shell::Minimize);CHECK(shell.NativeMinimized());exactNative();
                IE key;key.type=IE::KeyDown;key.vk=0x41;CHECK(!bridge.Filter(key));key.type=IE::KeyUp;CHECK(!bridge.Filter(key));
                Click(shell,Shell::NativeTask);CHECK(shell.PanelBounds()==moved);exactNative();
                Click(shell,Shell::Maximize);CHECK(shell.PanelBounds()==ui::Rect(0,0,1280,680));exactNative();
                Click(shell,Shell::Maximize);CHECK(shell.PanelBounds()==moved);
                // A modal document can extend beyond its moved owner panel.
                Click(shell,Shell::Documents);Click(shell,Shell::FolderActions);CHECK(shell.ContextOpen());
                bool openedText=false;
                for(size_t n=0;n<shell.ContextCount();++n)if(shell.ContextAction(n)==Shell::TextNew) {
                    const auto item=shell.ContextBounds(Shell::ContextBase+int(n));
                    Send(IE::MouseDown,item.left+5,item.top+5);Send(IE::MouseUp,item.left+5,item.top+5);Flush();openedText=true;break;
                }
                CHECK(openedText && shell.ActiveDialog()==Shell::Dialog::TextEditor);
                const auto doc=shell.DialogBounds();
                Send(IE::MouseDown,doc.left+70,doc.top+12);Send(IE::MouseMove,90,32);
                Send(IE::MouseUp,90,32);Flush();
                CHECK(shell.DialogBounds()==ui::Rect(20,20,652,370));exactNative();
                Click(shell,Shell::DialogMaximize);exactNative();Click(shell,Shell::DialogMinimize);
                CHECK(shell.NativeMinimized() && !shell.ModalActive());exactNative();
                Click(shell,Shell::NativeTask);CHECK(shell.NativeMaximized());exactNative();
                Click(shell,Shell::DialogMaximize);CHECK(shell.DialogBounds()==ui::Rect(20,20,652,370));
                Click(shell,Shell::DialogClose);CHECK(shell.ActiveDialog()==Shell::Dialog::TextClose);
                Click(shell,Shell::TextDiscard);CHECK(shell.ActiveDialog()==Shell::Dialog::None);exactNative();
                CHECK(user::Q().input.empty() && m.Read32(clicks)==beforeClicks && m.Read32(keys)==0);
                Click(shell,Shell::Close);CHECK(shell.ActivePanel()==Shell::Panel::Desktop);
                Start();Click(shell,Shell::MenuPrograms);CHECK(shell.Count()==1);
                shell.Accept();CHECK(shell.LaunchPath().empty() && shell.PendingTextInput()==Shell::TextInput::None);
                Click(shell,Shell::Close);
                // Guest-owned drag stays guest-owned even across the taskbar/Start area.
                IE down;down.type=IE::MouseDown;down.x=400;down.y=250;CHECK(!bridge.Filter(down));
                auto move=down;move.type=IE::MouseMove;move.x=30;move.y=700;CHECK(!bridge.Filter(move));
                auto up=move;up.type=IE::MouseUp;CHECK(!bridge.Filter(up));CHECK(!shell.StartOpen());
                // Shell press dragged into guest consumes release without delivering it.
                down.x=20;down.y=700;CHECK(bridge.Filter(down));up.x=400;up.y=250;CHECK(bridge.Filter(up));CHECK(!shell.StartOpen());
                // Real app click is delivered after the shell interactions.
                Send(IE::MouseDown,400,250);Send(IE::MouseUp,400,250);Flush();
                if(abort)requestAbort=true;
            }
        };
        rt::GuestExecution execution(p,[&](rt::GuestThread& t) {
            if(inspection)inspection();
            win32::HostCallOutcome outcome;if(win32::TryHandleHostCall(t.cpu,m,outcome))return !outcome.requestExit;
            cpu::Interpreter interpreter(m,t.teb,t.teb);CHECK(interpreter.Step(t.cpu)==cpu::StepResult::Continue);return true;
        },rt::GuestExecution::Startup::InitializeModules);
        for(unsigned i=0;i<1000000;++i) {
            const auto result=execution.RunOne();CHECK(result!=rt::GuestExecution::Result::AllocationFailed);
            if(requestAbort){finished=true;break;}

            if(inspected && m.Read32(clicks)) {CHECK(m.Read32(keys)==0);m.Write32(finish,1);}
            if(result==rt::GuestExecution::Result::Stopped) {CHECK(inspected&&m.Read32(stage)==2&&m.Read32(clicks)==1&&win32::g_guestExitCode==0);finished=true;break;}
            if(result==rt::GuestExecution::Result::Idle)rt::Clock::Sleep(1);
        }
        inspection={};CHECK(finished);
    }
    CHECK(user::g_platform.filterInput==previous.filterInput && user::g_platform.shellContext==previous.shellContext);
    CHECK(!shell.GuestActive());CHECK(!rt::g_process);pending.clear();oracle->Replay();
}
int main(int argc,char** argv) {
    CHECK(argc>=2);WorkspaceLayerOracle checks;oracle=&checks;ui::FontCache::SetDirectory("romfs/fonts");
    ui::FontDesc font;font.face="Liberation Sans";font.height=-16;CHECK(!ui::FontCache::Get(font)->Metrics().fixedPitch);rt::Clock::SetFastForward(true);
    char path[]="/tmp/sr-workspace-XXXXXX";CHECK(mkdtemp(path));
    struct Cleanup{std::string path;~Cleanup(){std::filesystem::remove_all(path);}} cleanup{path};
    Shell shell;std::string error;CHECK(shell.ConfigureFilesystem(path,"","",error));
    shell.Enter();shell.ToggleStart();auto r=Shell::Bounds(Shell::MenuComputer);shell.PointerDown(r.left+5,r.top+5);shell.PointerUp(r.left+5,r.top+5);
    CHECK(shell.ActivePanel()==Shell::Panel::Explorer);
    std::filesystem::copy_file(argv[1],std::string(path)+"/Programs/probe.exe");
    ui::ExecutableCatalog catalog;catalog.ScanRoot(std::string(path)+"/Programs/",0);shell.SetCatalog(catalog);
    // Each required allocation fails transactionally before hooks/guest shell state change.
    struct Allocator final:ui::PresentationAllocator {
        int fail=0,calls=0,live=0;
        void* Allocate(size_t n) override {if(calls++==fail)return nullptr;auto* p=std::malloc(n);if(p)++live;return p;}
        void Free(void* p) override {if(p)--live;std::free(p);}
    };
    for(int n=0;n<3;++n) {
        rt::GuestSession guest;user::EnableUi(1280,720);Allocator allocator;allocator.fail=n;
        {user::ShellSession bridge(shell,&allocator);CHECK(!bridge.Attach("allocation failure"));
         CHECK(!shell.GuestActive() && !user::g_platform.filterInput && user::WorkArea().bottom==720);}
        CHECK(allocator.live==0);
    }
    {rt::GuestSession guest;user::EnableUi(1280,720);Allocator allocator;allocator.fail=3;
     {user::ShellSession bridge(shell,&allocator);CHECK(bridge.Attach("optional layer allocation"));
      CHECK(!bridge.Scene().owners && bridge.Compose(user::S().screen.s).Valid());}
     CHECK(allocator.live==0 && !shell.GuestActive());}
    {rt::GuestSession guest;user::EnableUi(1280,720);user::ShellSession bridge(shell);CHECK(bridge.Attach("missing.exe"));
     CHECK(!guest.ProcessObject().LoadMainExe(std::string(path)+"/missing.exe",error));}
    CHECK(!shell.GuestActive() && !user::g_platform.filterInput);
    Run(shell,argv[1],false,argc>2?argv[2]:nullptr);CHECK(shell.ActivePanel()==Shell::Panel::Explorer);
    Run(shell,argv[1],true,nullptr);Run(shell,argv[1],false,nullptr);
    std::puts("shell workspace: real x86 windows, desktop, task minimize/restore, input ownership, normal/abort/restart PASS");
}
