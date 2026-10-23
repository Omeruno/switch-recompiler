#include "source/runtime/guest_session.h"
#include "source/runtime/guest_execution.h"
#include "source/runtime/clock.h"
#include "source/cpu/interpreter.h"
#include "source/win32/host_functions.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <new>
#include <type_traits>
namespace w=win32;
static bool parked;
static uint32_t TickA(){return 10;}
static uint32_t TickB(){return 20;}
static void FreshFacade() {
    assert(!w::g_imageBase && !w::g_imageBuffer && !w::g_imageBufferSize && !w::g_resourceDirRva);
    assert(!w::g_windowManager.GetLastCreatedWindow() && !w::g_windowManager.FindClass("LegacySessionWindow"));
    assert(!w::g_messageQueue.Size() && !w::g_messageQueue.QuitRequested());
    assert(!w::g_menuLoaded && w::g_loadedMenu.topLevel.empty() && w::g_dialogItemText.empty());
    assert(!w::g_dialogActive && !w::g_aboutBoxActive && !w::g_dialogEndRequested && !w::g_aboutBoxCloseRequested);
    assert(!w::g_paintCDC && !w::g_paintView && !w::g_requestedToolId);
    assert(w::g_requestedForegroundColor==0xFFFFFFFFu && w::g_requestedBackgroundColor==0xFFFFFFFFu);
    uint32_t hwnd,id;assert(!w::g_timerRegistry.CheckDue(100000,hwnd,id));
    assert(w::g_mfcStubSlots.empty());
}
static void NativeBinding() {
    static const uint8_t image[]={1,2,3,4};
    {
        rt::GuestSession outer;FreshFacade();
        w::g_imageBuffer=image;w::g_imageBufferSize=sizeof(image);w::g_imageBase=0x123;
        w::g_requestedToolId=77;w::g_requestedForegroundColor=0x123456;w::g_paintCDC=1234;
        w::g_menuLoaded=true;w::g_dialogItemText[77]={1,2};w::g_getTickCountImpl=&TickA;
        w::g_messageQueue.Post(42,55,66,77);w::g_messageQueue.RequestQuit();
        w::g_timerRegistry.SetTimer(42,7,1,0);
        w::g_windowManager.RegisterClass("outer",{});const auto window=w::g_windowManager.CreateWindow("outer",0,0,10,10,0);
        bool existed=false;
        const auto key=w::g_registry.CreateOrOpenKey(w::HKEY_CURRENT_USER,"session-lifetime-fixture",existed);
        w::g_registry.SetValue(key,"value",4,{7});
        const auto stub=w::AllocateNamedStubSlot("KERNEL32.dll","GetSystemInfo");assert(stub);
        {
            rt::GuestSession inner;FreshFacade();assert(w::g_getTickCountImpl==&TickA);
            w::RegValue value;assert(!w::g_registry.GetValue(key,"value",value));
            bool found=false;
            const auto innerKey=w::g_registry.OpenExistingKey(w::HKEY_CURRENT_USER,"session-lifetime-fixture",found);
            assert(found && w::g_registry.GetValue(innerKey,"value",value) && value.data==std::vector<uint8_t>{7});
            w::g_registry.SetValue(innerKey,"value",4,{8});
            w::g_getTickCountImpl=&TickB;w::g_requestedBackgroundColor=0;w::EnableHostLayer(w::kUiLayer,true);
        }
        assert(w::g_imageBuffer==image && w::g_imageBufferSize==sizeof(image) && w::g_imageBase==0x123);
        assert(w::g_requestedToolId==77 && w::g_requestedForegroundColor==0x123456 && w::g_paintCDC==1234);
        assert(w::g_getTickCountImpl==&TickA && !w::IsHostLayerEnabled(w::kUiLayer));
        assert(w::g_windowManager.GetWindow(window) && w::g_menuLoaded && w::g_dialogItemText[77].size()==2);
        assert(w::g_messageQueue.Size()==1 && w::g_messageQueue.QuitRequested());
        uint32_t h,id;assert(w::g_timerRegistry.CheckDue(2,h,id) && h==42 && id==7);
        assert(w::g_mfcStubSlots.size()==1);
        w::RegValue value;assert(w::g_registry.GetValue(key,"value",value) && value.data==std::vector<uint8_t>{8});
    }
    FreshFacade();assert(!w::g_getTickCountImpl);
}
static uint32_t Run(const char* path,const std::string& mode,void* storage) {
    auto* session=new(storage)rt::GuestSession;FreshFacade();
    auto& process=session->ProcessObject();auto& memory=session->Memory();
    process.ForceHle("mfc42u.dll");process.SetCommandLine(std::string(path)+" "+mode);
    std::string error;assert(process.LoadMainExe(path,error));
    assert(w::g_imageBuffer==process.MainModule()->image.data());
    assert(process.PrepareMainThread(process.MainThread().cpu,error));
    const auto probe=process.GetProcAddress(process.MainModule(),"LegacySessionProbe");assert(probe);
    parked=false;bool stopped=false,aborted=false;
    w::g_pumpHostFrame=[](){parked=true;rt::GuestExecution::Yield();return w::HostFrameResult{};};
    {
        rt::GuestExecution execution(process,[&](rt::GuestThread& thread) {
            w::HostCallOutcome call;if(w::TryHandleHostCall(thread.cpu,memory,call))return !call.requestExit;
            cpu::Interpreter interpreter(memory,thread.teb,thread.teb);const auto step=interpreter.Step(thread.cpu);
            assert(step==cpu::StepResult::Continue || step==cpu::StepResult::Halted);return true;
        },rt::GuestExecution::Startup::InitializeModules);
        for(unsigned n=0;n<300000;++n) {
            const auto result=execution.RunOne();assert(result!=rt::GuestExecution::Result::AllocationFailed);
            const auto stage=memory.Read32(probe);
            if((mode=="abort" && stage==1) || ((mode=="dialogabort" || mode=="aboutabort") && parked)) {
                assert(stage==(mode=="dialogabort"?2u:1u));aborted=true;break;
            }
            if(result==rt::GuestExecution::Result::Stopped) {
                const auto code=process.MainThread().exitCode;
                if(code!=(mode=="dialogexit"?42u:0u))std::fprintf(stderr,"legacy %s exit %u\n",mode.c_str(),code);
                assert(code==(mode=="dialogexit"?42u:0u));stopped=true;break;
            }
            if(result==rt::GuestExecution::Result::Idle)rt::Clock::Sleep(1);
        }
        assert(aborted || stopped);
        assert(w::g_menuLoaded && w::g_loadedMenu.topLevel.size()==1);
        assert(w::g_windowManager.GetWindow(memory.Read32(probe+4)));
    }
    assert(!process.Scheduler().HasLiveThreads());
    assert(!w::g_dialogActive && !w::g_aboutBoxActive);
    // Teardown must clear even a partially pending UI command and guest addresses.
    w::g_requestedToolId=123;w::g_paintCDC=memory.Read32(probe+16);w::g_paintView=probe;
    const auto document=memory.Read32(probe+24);assert(document);
    session->~GuestSession();assert(!rt::g_process && !w::g_pumpHostFrame);FreshFacade();
    return document;
}
int main(int argc,char** argv) {
    assert(argc==2);cpu::g_consoleAvailable=true;rt::Clock::SetFastForward(true);w::EnableHostLayer(w::kUiLayer,false);
    NativeBinding();
    std::aligned_storage_t<sizeof(rt::GuestSession),alignof(rt::GuestSession)> storage;
    {
        auto* failed=new(&storage)rt::GuestSession;std::string error;
        assert(!failed->ProcessObject().LoadMainExe(std::string(argv[1])+".missing",error));
        failed->~GuestSession();FreshFacade();
    }
    uint32_t firstDocument=0;
    for(const auto& mode:{"normal","abort","normal","dialogexit","normal","dialogabort","normal","aboutabort","normal"}) {
        const auto document=Run(argv[1],mode,&storage);
        if(!firstDocument)firstDocument=document;
        assert(document==firstDocument);
    }
    puts("PASSED: nine sequential legacy x86/MFC sessions, normal/callback exit/parked dialog+about abort, image/class/queue/timer/CDC/document isolation, failed load and native binding restoration");
}
