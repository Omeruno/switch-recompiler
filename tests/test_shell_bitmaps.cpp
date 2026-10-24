#include "source/ui/desktop_shell.h"
#include "source/runtime/guest_session.h"
#include "source/runtime/guest_execution.h"
#include "source/runtime/clock.h"
#include "source/cpu/interpreter.h"
#include "source/win32/shell_session.h"
#include "source/win32/presentation_session.h"
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <cstdlib>
namespace fs=std::filesystem;using Shell=ui::DesktopShell;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"shell bitmaps %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static void Click(Shell& shell,int target){auto r=Shell::Bounds(target);shell.PointerDown(r.left+5,r.top+5);shell.PointerUp(r.left+5,r.top+5);}
static void Run(Shell& shell,const fs::path& root,ui::ShellLaunchRequest request) {
    rt::GuestSession session;auto& p=session.ProcessObject();auto& m=session.Memory();std::string error;
    CHECK(rt::FileSystem().ConfigurePersistent(root.string(),"","",error));
    win32::user::PresentationSession platform(true,nullptr);win32::user::EnableUi(1280,720);
    win32::user::ShellSession bridge(shell);CHECK(bridge.Attach("Public GDI bitmap application"));
    p.SetCommandLine(request.commandLine);CHECK(p.LoadMainExe(request.nativePath,error));if(!request.workingDirectory.empty())CHECK(rt::FileSystem().SetCurrentDirectory(request.workingDirectory));CHECK(p.PrepareMainThread(p.MainThread().cpu,error));
    const auto probe=p.GetProcAddress(p.MainModule(),"BitmapProbe");CHECK(probe);bool finished=false;
    {
        rt::GuestExecution execution(p,[&](rt::GuestThread& t) {
            win32::HostCallOutcome out;if(win32::TryHandleHostCall(t.cpu,m,out))return !out.requestExit;
            cpu::Interpreter interpreter(m,t.teb,t.teb);const auto step=interpreter.Step(t.cpu);
            CHECK(step==cpu::StepResult::Continue || step==cpu::StepResult::Halted);return true;
        },rt::GuestExecution::Startup::InitializeModules);
        for(unsigned i=0;i<700000;++i) {
            const auto result=execution.RunOne();CHECK(result!=rt::GuestExecution::Result::AllocationFailed);
            if(result==rt::GuestExecution::Result::Stopped){CHECK(win32::g_guestExitCode==0 && m.Read32(probe)==1);finished=true;break;}
            if(result==rt::GuestExecution::Result::Idle)rt::Clock::Sleep(1);
        }
        CHECK(finished);
    }
}
int main(int argc,char** argv) {
    CHECK(argc==2);rt::Clock::SetFastForward(true);cpu::g_consoleAvailable=true;
    char pattern[]="/tmp/sr-shell-bmp-XXXXXX";CHECK(mkdtemp(pattern));const fs::path root=pattern;
    struct Cleanup{fs::path p;~Cleanup(){fs::remove_all(p);}} cleanup{root};
    Shell shell;std::string error;CHECK(shell.ConfigureFilesystem(root.string(),"","",error));fs::copy_file(argv[1],root/"Programs/public-bitmap.exe");
    const std::string document="C:\\Documents and Settings\\Player\\My Documents\\рисунок 😀.bmp";
    std::string command;CHECK(rt::BuildCommandLine({"C:\\Programs\\public-bitmap.exe","write",document},command));
    shell.Enter();shell.ToggleStart();Click(shell,Shell::MenuRun);shell.CompleteTextInput(true,command);
    const auto request=shell.LaunchRequest();Run(shell,root,request);CHECK(!rt::g_process && !shell.GuestActive());
    shell.Enter();Click(shell,Shell::Documents);
    for(size_t i=0;i<shell.Browser().Entries().size();++i)if(shell.Browser().Entries()[i].name=="рисунок 😀.bmp")Click(shell,Shell::BrowserRowBase+int(i));
    Click(shell,Shell::FileActions);CHECK(shell.ContextOpen());bool opened=false;
    for(size_t i=0;i<shell.ContextCount();++i)if(shell.ContextAction(i)==Shell::ImageOpen) {
        auto r=shell.ContextBounds(Shell::ContextBase+int(i));shell.PointerDown(r.left+5,r.top+5);shell.PointerUp(r.left+5,r.top+5);opened=true;break;
    }
    CHECK(opened && shell.ActiveDialog()==Shell::Dialog::ImageViewer && shell.Image().Width()==3 && shell.Image().Height()==2);
    const uint32_t expected[]={0xff0000,0x00ff00,0x0000ff,0xffffff,0,0xffff00};
    for(int y=0;y<2;++y)for(int x=0;x<3;++x)CHECK(shell.Image().Pixel(x,y)==expected[y*3+x]);
    shell.Back(); // fresh shell and real x86 process read the same persisted bytes
    ui::ShellLink link;link.target="C:\\Programs\\public-bitmap.exe";link.workingDirectory="C:\\TEMP";link.name="Public bitmap";
    CHECK(rt::BuildCommandLine({"read-cwd",document},link.arguments));
    CHECK(link.Save(shell.Browser().Filesystem(),"C:\\Documents and Settings\\Player\\Desktop\\Bitmap.lnk"));
    CHECK(link.Save(shell.Browser().Filesystem(),"C:\\Documents and Settings\\Player\\Start Menu\\Programs\\Bitmap.lnk"));
    Shell fresh;CHECK(fresh.ConfigureFilesystem(root.string(),"","",error));fresh.Enter();Click(fresh,Shell::DesktopLinkBase);
    Run(fresh,root,fresh.LaunchRequest());fresh.Enter();fresh.ToggleStart();Click(fresh,Shell::MenuPrograms);fresh.Accept();
    Run(fresh,root,fresh.LaunchRequest());fresh.Enter();fresh.ToggleStart();Click(fresh,Shell::MenuRun);
    CHECK(rt::BuildCommandLine({"C:\\Programs\\public-bitmap.exe","read",document},command));fresh.CompleteTextInput(true,command);
    Run(fresh,root,fresh.LaunchRequest());CHECK(!rt::g_process && !fresh.GuestActive());
    CHECK(fs::file_size(root/"Documents and Settings/Player/My Documents/рисунок 😀.bmp")==78);
    puts("PASSED: actual x86 GDI -> Unicode BMP save -> native Explorer/pixels -> fresh shell and x86 reopen");
}
