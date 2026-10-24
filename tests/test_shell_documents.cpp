#include "source/ui/desktop_shell.h"
#include "source/runtime/guest_session.h"
#include "source/runtime/guest_execution.h"
#include "source/runtime/clock.h"
#include "source/cpu/interpreter.h"
#include "source/win32/host_functions.h"
#include "source/win32/crt_globals.h"
#include "source/win32/hle_util.h"
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <cstdlib>
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"F3b documents line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static void Run(const ui::ShellLaunchRequest& request,const fs::path& system,const fs::path& sd,const fs::path& rom) {
    CHECK(!request.nativePath.empty() && !request.commandLine.empty());
    rt::GuestSession session;auto& proc=session.ProcessObject();auto& memory=session.Memory();std::string error;
    CHECK(rt::FileSystem().ConfigurePersistent(system.string(),sd.string(),rom.string(),error));
    proc.SetCommandLine(request.commandLine);CHECK(proc.LoadMainExe(request.nativePath,error));
    CHECK(proc.PrepareMainThread(proc.MainThread().cpu,error));
    // The legacy fallback's data imports must match the same full bounded argv.
    const auto base=win32::g_crtGlobalsBase;
    CHECK(win32::hle::ReadAnsi(memory,memory.Read32(base+win32::CRT_OFFSET_CMDLINE_PTR))==request.commandLine);
    CHECK(memory.Read32(base+win32::CRT_OFFSET_ARGC)==request.arguments.size()+1);
    const auto legacy=memory.Read32(base+win32::CRT_OFFSET_ARGV_PTR);
    for(size_t i=0;i<request.arguments.size();++i)
        CHECK(win32::hle::ReadAnsi(memory,memory.Read32(legacy+uint32_t((i+1)*4)))==request.arguments[i]);
    const auto probe=proc.GetProcAddress(proc.MainModule(),"DocumentProbe");CHECK(probe);
    bool finished=false;
    {
        rt::GuestExecution execution(proc,[&](rt::GuestThread& thread) {
            win32::HostCallOutcome out;
            if(win32::TryHandleHostCall(thread.cpu,memory,out))return !out.requestExit;
            cpu::Interpreter interpreter(memory,thread.teb,thread.teb);
            const auto result=interpreter.Step(thread.cpu);
            CHECK(result==cpu::StepResult::Continue || result==cpu::StepResult::Halted);return true;
        },rt::GuestExecution::Startup::InitializeModules);
        for(unsigned i=0;i<700000;++i) {
            const auto result=execution.RunOne();CHECK(result!=rt::GuestExecution::Result::AllocationFailed);
            if(result==rt::GuestExecution::Result::Stopped){CHECK(win32::g_guestExitCode==0);finished=true;break;}
            if(result==rt::GuestExecution::Result::Idle)rt::Clock::Sleep(1);
        }
        CHECK(finished && memory.Read32(probe)==1);
    }
    CHECK(!proc.Scheduler().HasLiveThreads());
}
int main(int argc,char** argv) {
    CHECK(argc==2);rt::Clock::SetFastForward(true);cpu::g_consoleAvailable=true;
    char pattern[]="/tmp/sr-document-XXXXXX";CHECK(mkdtemp(pattern));
    const fs::path base=pattern,sd=base/"sd",rom=base/"rom",system=sd/"switch-recompiler/drive_c";
    struct Cleanup {fs::path path;~Cleanup(){fs::remove_all(path);}} cleanup{base};
    fs::create_directories(sd/"Apps/Public Application");fs::create_directories(rom);
    fs::copy_file(argv[1],sd/"Apps/Public Application/document reader.exe");
    ui::DesktopShell shell;std::string error;CHECK(shell.ConfigureFilesystem(system.string(),sd.string(),rom.string(),error));
    const auto document=system/"Documents and Settings/Player/My Documents/文档 😀 with spaces.txt";
    // Create real document bytes through the production native document model.
    ui::TextDocument text;text.New();CHECK(text.Edit(0,"initial draft"));
    CHECK(text.Save(shell.Browser().Filesystem(),"C:\\Documents and Settings\\Player\\My Documents\\文档 😀 with spaces.txt"));
    CHECK(text.Edit(0,"public document payload") && text.Save(shell.Browser().Filesystem()));
    const std::string doc="C:\\Documents and Settings\\Player\\My Documents\\文档 😀 with spaces.txt";
    const std::string app="S:\\Apps\\Public Application\\document reader.exe";
    auto click=[&](int target) {
        const auto r=ui::DesktopShell::Bounds(target);const int x=(r.left+r.right)/2,y=(r.top+r.bottom)/2;
        shell.PointerDown(x,y);shell.PointerUp(x,y);
    };
    shell.Enter();shell.ToggleStart();click(ui::DesktopShell::MenuRun);
    std::string line;CHECK(rt::BuildCommandLine({app,"verify","space value","","quote\"value","slash\\",doc},line));
    shell.CompleteTextInput(true,line);CHECK(shell.LaunchRequest().arguments.size()==6);
    Run(shell.LaunchRequest(),system,sd,rom);
    shell.Enter();click(ui::DesktopShell::Documents);
    size_t index=0;const auto& list=shell.Browser().Entries();
    while(index<list.size() && list[index].name!="文档 😀 with spaces.txt")++index;
    CHECK(index<list.size());
    click(ui::DesktopShell::BrowserRowBase+int(index));shell.Accept();
    CHECK(shell.PendingTextInput()==ui::DesktopShell::TextInput::OpenWith && shell.LaunchPath().empty());
    shell.CompleteTextInput(true,app);CHECK(shell.LaunchRequest().arguments==std::vector<std::string>{doc});
    Run(shell.LaunchRequest(),system,sd,rom);
    shell.Enter();CHECK(shell.ActivePanel()==ui::DesktopShell::Panel::Explorer);
    shell.Accept();CHECK(shell.PendingTextInput()==ui::DesktopShell::TextInput::None);
    Run(shell.LaunchRequest(),system,sd,rom); // Remembered association after guest teardown.
    shell.Enter();shell.ToggleStart();click(ui::DesktopShell::MenuRun);shell.CompleteTextInput(true,rt::QuoteCommandArgument(doc));
    Run(shell.LaunchRequest(),system,sd,rom); // Same document association through Run.
    {
        ui::DesktopShell restarted;CHECK(restarted.ConfigureFilesystem(system.string(),sd.string(),rom.string(),error));
        CHECK(restarted.Browser().ApplicationFor(doc)==app);restarted.Enter();restarted.ToggleStart();
        const auto run=ui::DesktopShell::Bounds(ui::DesktopShell::MenuRun);
        restarted.PointerDown(run.left+10,run.top+10);restarted.PointerUp(run.left+10,run.top+10);
        restarted.CompleteTextInput(true,rt::QuoteCommandArgument(doc));
        CHECK(restarted.PendingTextInput()==ui::DesktopShell::TextInput::None && restarted.LaunchRequest().arguments==std::vector<std::string>{doc});
        Run(restarted.LaunchRequest(),system,sd,rom); // Persisted association in a fresh native shell.
    }
    CHECK(!rt::g_process);std::string data;std::ifstream f(document);std::getline(f,data);CHECK(data=="public document payload");
    puts("PASSED: five real-x86 Run/chooser/association/reopen/fresh-shell sessions; full A/W command line, CRT argv, LocalFree and actual Unicode document bytes");
}
