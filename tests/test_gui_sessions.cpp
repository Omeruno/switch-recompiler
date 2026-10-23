#include "source/runtime/guest_session.h"
#include "source/runtime/guest_execution.h"
#include "source/runtime/clock.h"
#include "source/cpu/interpreter.h"
#include "source/win32/user32.h"
#include "source/ui/presentation.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
#include <new>
#include <type_traits>
namespace user=win32::user;
static bool parked;
static void NativeBinding() {
    auto* baseline=&user::S();auto* screen=ui::g_gdi.Screen();
    {
        rt::GuestSession outer;auto* state=&user::S();assert(state!=baseline);
        assert(user::EnableWindowLayers(true));
        auto* capture=user::S().windowLayers.Capture(123,0,0,20,20);assert(capture);
        auto* owners=capture->owners;
        const auto brush=ui::g_gdi.CreateSolidBrush(0x123456);assert(brush);
        ui::SetSysColor(15,0xAABBCC);user::S().clipboard=u"outer";
        {
            rt::GuestSession inner;assert(&user::S()!=state);
            assert(ui::SysColor(15)==0xC8D0D4 && user::S().clipboard.empty());
            assert(ui::g_gdi.Screen()==&user::S().screen.s);
        }
        assert(&user::S()==state && ui::g_gdi.Screen()==&state->screen.s);
        assert(ui::g_gdi.Brush(brush) && ui::SysColor(15)==0xAABBCC);
        assert(user::S().clipboard==u"outer" && capture->owners==owners);
        // WindowLayers itself must never move across a nested binding exchange.
        assert(user::S().windowLayers.Capture(123,0,0,20,20)==capture);
    }
    assert(&user::S()==baseline && ui::g_gdi.Screen()==screen);
}
static void Run(const char* path,const std::string& mode,const char* probeName,void* storage,
                ui::Presentation& presentation,std::vector<uint32_t>& pixels) {
    const bool core=std::string(probeName)=="SessionUiProbe",abort=mode=="hostabort";
    auto* baseline=&user::S();
    auto* session=new(storage)rt::GuestSession;
    auto& process=session->ProcessObject();auto& memory=session->Memory();
    assert(user::S().initialized && user::S().screenW==1280 && user::S().screenH==720);
    assert(user::S().windows.size()==1 && user::S().queues.empty() && !user::S().capture);
    assert(ui::SysColor(15)==0xC8D0D4 && user::S().clipboard.empty());
    assert(user::EnableWindowLayers(true));
    std::string error;process.SetCommandLine(std::string(path)+" "+mode);
    assert(process.LoadMainExe(path,error));assert(process.PrepareMainThread(process.MainThread().cpu,error));
    const auto probe=process.GetProcAddress(process.MainModule(),probeName);assert(probe);
    bool stopped=false,snapshot=false;parked=false;
    std::vector<uint32_t> expected;
    user::g_platform.wait=[](int,std::vector<user::InputEvent>&){parked=true;rt::GuestExecution::Yield();return true;};
    {
        rt::GuestExecution execution(process,[&](rt::GuestThread& thread) {
            win32::HostCallOutcome call;
            if(win32::TryHandleHostCall(thread.cpu,memory,call))return !call.requestExit;
            cpu::Interpreter interpreter(memory,thread.teb,thread.teb);
            auto step=interpreter.Step(thread.cpu);
            assert(step==cpu::StepResult::Continue || step==cpu::StepResult::Halted);return true;
        },rt::GuestExecution::Startup::InitializeModules);
        for(unsigned n=0;n<300000;++n) {
            auto result=execution.RunOne();assert(result!=rt::GuestExecution::Result::AllocationFailed);
            if(core && memory.Read32(probe)==1 && !snapshot) {
                assert(user::S().windows.size()>1 && user::S().capture);
                assert(presentation.Submit(user::S().screen.s,user::S().windowLayers.Scene()));
                assert(presentation.Render({pixels.data(),1280,720,1280,pixels.size()}));
                expected=pixels;
                snapshot=true;user::S().clipboard=u"dirty guest session";
                if(abort)break;
                memory.Write32(probe+28,1);
            }
            if(!core && abort && parked){assert(memory.Read32(probe)==1);break;}
            if(result==rt::GuestExecution::Result::Stopped) {
                const auto code=process.MainThread().exitCode;
                if(code!=(core?0u:42u))std::fprintf(stderr,"GUI session %s exit %u\n",mode.c_str(),code);
                assert(code==(core?0u:42u));stopped=true;break;
            }
            if(result==rt::GuestExecution::Result::Idle)rt::Clock::Sleep(1);
        }
        assert(abort?(core?snapshot:parked):stopped);
    }
    assert(!process.Scheduler().HasLiveThreads());
    assert(user::S().windows.size()==1 && !user::S().capture && !user::S().foreground);
    for(const auto& item:user::S().queues)assert(!item.second.focus && !item.second.active);
    if(core)assert(snapshot);
    session->~GuestSession();assert(&user::S()==baseline && !rt::g_process);
    user::g_platform={};
    // Renderer owns its frame after DIBs, windows, layers and memory disappeared.
    if(core) {
        assert(presentation.Render({pixels.data(),1280,720,1280,pixels.size()}));
        assert(pixels==expected);
        const auto before=pixels;
        assert(presentation.Render({pixels.data(),1280,720,1280,pixels.size()}));assert(pixels==before);
    }
}
int main(int argc,char** argv) {
    assert(argc==4);cpu::g_consoleAvailable=false;rt::Clock::SetFastForward(true);
    user::EnableUi(1280,720);NativeBinding();
    std::aligned_storage_t<sizeof(rt::GuestSession),alignof(rt::GuestSession)> storage;
    {
        auto* baseline=&user::S();
        auto* failed=new(&storage)rt::GuestSession;
        std::string error;
        assert(!failed->ProcessObject().LoadMainExe(std::string(argv[1])+".missing",error));
        assert(!error.empty());ui::SetSysColor(15,0x123456);
        failed->~GuestSession();
        assert(&user::S()==baseline && !rt::g_process && ui::SysColor(15)==0xC8D0D4);
    }
    ui::SoftwarePresentation software;ui::Presentation presentation(&software);
    std::vector<uint32_t> pixels(1280*720);
    for(const auto& mode:{"normal","hostabort","normal","hostabort"})Run(argv[1],mode,"SessionUiProbe",&storage,presentation,pixels);
    for(const auto& mode:{"exit","hostabort","exit","hostabort"}) {
        Run(argv[2],mode,"DialogProbeStage",&storage,presentation,pixels);
        Run(argv[3],mode,"ColorProbeStage",&storage,presentation,pixels);
    }
    puts("PASSED: 12 sequential x86 USER/GDI/dialog/color sessions, normal/ExitProcess/parked abort, class/hook/DIB/menu/icon/cache isolation, stable nested raster ownership and renderer redraw after destruction");
}
