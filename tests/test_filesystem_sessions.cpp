#include "source/runtime/guest_session.h"
#include "source/runtime/guest_execution.h"
#include "source/runtime/files.h"
#include "source/runtime/clock.h"
#include "source/ui/desktop_shell.h"
#include "source/cpu/interpreter.h"
#include "source/win32/host_functions.h"
#include "source/win32/user32.h"
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <iterator>
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"F1 sessions line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static void Run(const fs::path& exe,const fs::path& system,const fs::path& sd,const fs::path& rom,const char* mode) {
    rt::GuestSession session;auto& p=session.ProcessObject();auto& memory=session.Memory();std::string error;
    CHECK(rt::FileSystem().ConfigurePersistent(system.string(),sd.string(),rom.string(),error));
    p.SetCommandLine(exe.string()+" "+mode);CHECK(p.LoadMainExe(exe.string(),error));
    CHECK(p.PrepareMainThread(p.MainThread().cpu,error));
    const auto probe=p.GetProcAddress(p.MainModule(),"FilesystemProbe");CHECK(probe);
    bool finished=false;
    {
        rt::GuestExecution execution(p,[&](rt::GuestThread& t) {
            win32::HostCallOutcome out;if(win32::TryHandleHostCall(t.cpu,memory,out))return !out.requestExit;
            cpu::Interpreter interpreter(memory,t.teb,t.teb);
            const auto result=interpreter.Step(t.cpu);
            CHECK(result==cpu::StepResult::Continue || result==cpu::StepResult::Halted);return true;
        },rt::GuestExecution::Startup::InitializeModules);
        for(unsigned i=0;i<500000;++i) {
            const auto result=execution.RunOne();CHECK(result!=rt::GuestExecution::Result::AllocationFailed);
            if(std::string(mode)=="abort" && memory.Read32(probe)==1){finished=true;break;}
            if(result==rt::GuestExecution::Result::Stopped){CHECK(win32::g_guestExitCode==0);finished=true;break;}
            if(result==rt::GuestExecution::Result::Idle)rt::Clock::Sleep(1);
        }
        CHECK(finished && memory.Read32(probe)==1);
    }
    CHECK(!p.Scheduler().HasLiveThreads());
}
int main(int argc,char**argv) {
    CHECK(argc==3);rt::Clock::SetFastForward(true);cpu::g_consoleAvailable=true;
    namespace user=win32::user;
    user::EnableUi(800,600);
    user::g_platform.wait=[](int,std::vector<user::InputEvent>& events) {
        static uint32_t parent=0;static unsigned clicks=0;
        for(const auto& pair:user::S().windows) {
            auto* window=pair.second.get();
            if((window->id!=1001 && window->id!=1002) || !(window->style&user::WS_CHILD))continue;
            auto* owner=user::Win(window->parent);if(!owner)continue;
            if(parent!=owner->hwnd){parent=owner->hwnd;clicks=0;}
            const bool readOnly=owner->text.find(u"R:\\")!=std::u16string::npos;
            const auto wanted=readOnly && clicks?1002u:1001u;
            if(window->id!=wanted)continue;
            ++clicks;
            user::InputEvent move;move.type=user::InputEvent::MouseMove;
            move.x=user::ClientOriginX(window)+4;move.y=user::ClientOriginY(window)+4;
            auto down=move;down.type=user::InputEvent::MouseDown;
            auto up=move;up.type=user::InputEvent::MouseUp;
            events.push_back(move);events.push_back(down);events.push_back(up);return true;
        }
        return false;
    };
    char name[]="/tmp/sr-filesystem-XXXXXX";const char* temp=mkdtemp(name);CHECK(temp);
    const fs::path base=temp,sd=base/"sd",rom=base/"rom",system=sd/"switch-recompiler/drive_c";
    struct Cleanup{fs::path path;~Cleanup(){fs::remove_all(path);}} cleanup{base};
    fs::create_directories(sd/"Apps/One");fs::create_directories(sd/"Apps/Two");fs::create_directory(rom);
    std::ofstream(rom/"locked.txt")<<"locked";
    for(const auto& app:{sd/"Apps/One",sd/"Apps/Two"}) {
        fs::copy_file(argv[1],app/"filesystem_mounts.exe");fs::copy_file(argv[2],app/"TeStDlL.DLL");
    }
    ui::DesktopShell shell;std::string shellError;
    CHECK(shell.ConfigureFilesystem(system.string(),sd.string(),rom.string(),shellError));
    auto click=[&](int target) {
        const auto r=ui::DesktopShell::Bounds(target);const int x=(r.left+r.right)/2,y=(r.top+r.bottom)/2;
        shell.PointerDown(x,y);shell.PointerUp(x,y);
    };
    ui::ExecutableCatalog catalog;catalog.ScanRoot((sd/"Apps/One").string()+"/",0);
    shell.SetCatalog(catalog);shell.Enter();shell.Accept();shell.Accept();
    CHECK(shell.LaunchRequest().guestPath=="S:\\Apps\\One\\filesystem_mounts.exe");
    Run(shell.LaunchPath(),system,sd,rom,"writer");
    const auto document=system/"Documents and Settings/Player/My Documents/文件.txt";
    {std::ifstream f(document);std::string bytes{std::istreambuf_iterator<char>(f),{}};CHECK(bytes=="shared bytes");}
    {rt::GuestSession failed;std::string error;
        CHECK(rt::FileSystem().ConfigurePersistent(system.string(),sd.string(),rom.string(),error));
        CHECK(!failed.ProcessObject().LoadMainExe((sd/"missing.exe").string(),error));CHECK(!error.empty());}
    auto runTwo=[&]() {
        shell.Enter();shell.ToggleStart();click(ui::DesktopShell::MenuRun);
        CHECK(shell.PendingTextInput()==ui::DesktopShell::TextInput::Run);
        shell.CompleteTextInput(true,"S:\\Apps\\Two\\filesystem_mounts.exe");
        CHECK(shell.LaunchPath()==(sd/"Apps/Two/filesystem_mounts.exe").string());
        Run(shell.LaunchPath(),system,sd,rom,"reader");
    };
    runTwo();
    shell.Enter();click(ui::DesktopShell::Close);click(ui::DesktopShell::Computer);
    click(ui::DesktopShell::Address);shell.CompleteTextInput(true,"S:\\Apps\\One");
    size_t row=0;const auto& entries=shell.Browser().Entries();
    while(row<entries.size() && entries[row].name!="filesystem_mounts.exe")++row;
    CHECK(row<entries.size());click(ui::DesktopShell::BrowserRowBase+int(row));shell.Accept();
    CHECK(shell.LaunchPath()==(sd/"Apps/One/filesystem_mounts.exe").string());
    Run(shell.LaunchPath(),system,sd,rom,"abort");
    shell.Enter();CHECK(shell.ActivePanel()==ui::DesktopShell::Panel::Explorer && shell.Browser().Path()=="S:\\Apps\\One");
    runTwo();
    CHECK(!fs::exists(system/"moved.txt") && !fs::exists(rom/"copy.txt"));
    std::string locked;std::ifstream(rom/"locked.txt")>>locked;CHECK(locked=="locked");
    CHECK(!rt::g_process);
    puts("PASSED: four fresh x86 mounted filesystem sessions via Start/Run/Explorer, real host Unicode bytes, distinct EXE directories, sibling DLLs, failed load and host-abort restart");
}
