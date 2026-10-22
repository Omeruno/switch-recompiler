#ifdef SR_OPENGL_TEST
#define GL_GLEXT_PROTOTYPES 1
#include <EGL/egl.h>
#include <GL/glcorearb.h>
#include "source/ui/opengl_presentation.h"
#endif
#include "source/runtime/guest_execution.h"
#include "source/runtime/clock.h"
#include "source/cpu/interpreter.h"
#include "source/win32/user32.h"
#include "source/ui/presentation.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

namespace user=win32::user;
#ifdef SR_OPENGL_TEST
// Same production GL renderer, with an offscreen host context only.
class GpuCheck {
    EGLDisplay display_=EGL_NO_DISPLAY;
    EGLSurface surface_=EGL_NO_SURFACE;
    EGLContext context_=EGL_NO_CONTEXT;
    ui::OpenGlPresentation backend_;
    ui::Presentation presentation_{&backend_};
    std::vector<uint32_t> pixels_=std::vector<uint32_t>(1280*720);
public:
    GpuCheck() {
        display_=eglGetDisplay(EGL_DEFAULT_DISPLAY);assert(display_!=EGL_NO_DISPLAY);
        assert(eglInitialize(display_,nullptr,nullptr) && eglBindAPI(EGL_OPENGL_API));
        EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_BIT,
            EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
        EGLConfig config;EGLint count=0;assert(eglChooseConfig(display_,attrs,&config,1,&count) && count==1);
        EGLint dimensions[]={EGL_WIDTH,1280,EGL_HEIGHT,720,EGL_NONE};
        surface_=eglCreatePbufferSurface(display_,config,dimensions);assert(surface_!=EGL_NO_SURFACE);
        EGLint contextAttrs[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,3,
            EGL_CONTEXT_OPENGL_PROFILE_MASK,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,EGL_NONE};
        context_=eglCreateContext(display_,config,EGL_NO_CONTEXT,contextAttrs);assert(context_!=EGL_NO_CONTEXT);
        assert(eglMakeCurrent(display_,surface_,surface_,context_) && backend_.Initialize());
    }
    ~GpuCheck() {
        presentation_.Detach();backend_.Shutdown();
        assert(eglMakeCurrent(display_,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT));
        assert(eglDestroySurface(display_,surface_) && eglDestroyContext(display_,context_) && eglTerminate(display_));
    }
    void Check(const ui::Surface& screen,const std::vector<uint32_t>& expected,size_t stride,unsigned stage) {
        assert(presentation_.Submit(screen) && presentation_.Render({nullptr,1280,720,0,0}));
        glReadPixels(0,0,1280,720,GL_RGBA,GL_UNSIGNED_BYTE,pixels_.data());assert(glGetError()==GL_NO_ERROR);
        for(int y=0;y<720;++y)for(int x=0;x<1280;++x) {
            if(pixels_[size_t(719-y)*1280+x]!=expected[size_t(y)*stride+x]) {
                std::fprintf(stderr,"Composition stage %u actual GL (%d,%d) mismatch\n",stage,x,y);assert(false);
            }
        }
        // Simulate a backend recreation in the middle of the live window scene.
        if(stage==14)presentation_.Attach(&backend_);
    }
};
static GpuCheck* gpuCheck;
#endif
// Independent geometry oracle: opaque rectangles, back to front. It does not
// query USER32's window tree, clipping, invalid regions or reported pixels.
static uint32_t Expected(unsigned stage,int x,int y) {
    auto inside=[&](int l,int t,int w,int h){return x>=l && y>=t && x<l+w && y<t+h;};
    uint32_t rgb=0x182030;
    if(stage>=22) {
        const bool inLower=inside(40,50,300,220),inUpper=inside(120,140,100,70);
        if(inLower)rgb=0x1E3CD2;
        if(stage>=25 && stage<28 && inLower && inside(50,60,100,100))rgb=0xFADC14;
        if(stage==25 && inLower && inside(100,110,100,100))rgb=0xB428BE;
        if(stage==26 && inLower && inside(190,200,100,100))rgb=0xB428BE;
        if(inUpper && (stage==22 || stage==24 || !inLower))rgb=0x1EBE3C;
        return rgb;
    }
    auto lower=[&]() {
        if(stage<2 || stage>=20 || !inside(40,50,300,220))return;
        rgb=stage<4?0xD21E28:0x1E3CD2;
        if(stage==11 && inside(290,230,150,100))rgb=0xFADC14;
        if(stage==12 && inside(20,40,150,100))rgb=0xFADC14;
        if(stage>=18 && inside(60,70,32,24))rgb=(x&1)?0xFF00FF:0x00FFFF;
        if(stage==19 && x==60 && y==70)rgb=0xFFFF00;
    };
    auto upper=[&]() {
        if(stage<3 || stage==7 || stage>=21)return;
        int l=stage<5?160:120,t=stage<5?100:140;
        int w=stage<6?280:100,h=stage<6?220:70;
        if(inside(l,t,w,h))rgb=0x1EBE3C;
    };
    if(stage==9){upper();lower();}else{lower();upper();}
    if(stage==14 && inside(60,70,80,60))rgb=0xF5F5F5;
    if(stage==16 && inside(30,40,200,150))rgb^=0xFFFFFF;
    return rgb;
}
static void Check(unsigned stage,const ui::Surface& screen,ui::Presentation& presentation) {
    for(int y=0;y<screen.height;++y)for(int x=0;x<screen.width;++x) {
        const uint32_t expected=Expected(stage,x,y),actual=screen.Get(x,y);
        if(actual!=expected) {
            std::fprintf(stderr,"Composition stage %u CPU (%d,%d): %06x != %06x\n",stage,x,y,actual,expected);
            assert(false);
        }
    }
    assert(presentation.Submit(screen));
    // Three retained outputs exercise independent history and poisoned padding.
    constexpr size_t stride=1287;
    static std::vector<uint32_t> output[3]={std::vector<uint32_t>(stride*720,0x12345678),
        std::vector<uint32_t>(stride*720,0x12345678),std::vector<uint32_t>(stride*720,0x12345678)};
    auto& pixels=output[stage%3];
    assert(presentation.Render({pixels.data(),1280,720,stride,pixels.size(),true,1}));
    auto viewport=ui::FitViewport(screen.width,screen.height,1280,720);
    for(int y=0;y<720;++y)for(size_t x=0;x<stride;++x) {
        uint32_t expected=0xFF000000;
        if(x>=1280)expected=0x12345678;
        else if(viewport.Contains(int(x),y)) {
            int sx=int(x-viewport.x)*screen.width/viewport.width;
            int sy=(y-viewport.y)*screen.height/viewport.height;
            auto rgb=Expected(stage,sx,sy);expected=0xFF000000|ui::RgbToColorref(rgb);
        }
        if(pixels[size_t(y)*stride+x]!=expected) {
            std::fprintf(stderr,"Composition stage %u output (%zu,%d) mismatch\n",stage,x,y);assert(false);
        }
    }
#ifdef SR_OPENGL_TEST
    gpuCheck->Check(screen,pixels,stride,stage);
#endif
}
int main(int argc,char** argv) {
    assert(argc==2 || (argc==3 && std::strcmp(argv[2],"--classic")==0));
    int width=argc==2?1280:800,height=argc==2?720:600;
    rt::Clock::SetFastForward(true);user::EnableUi(width,height);
#ifdef SR_OPENGL_TEST
    GpuCheck gpu;gpuCheck=&gpu;
#endif
    cpu::PagedGuestMemory memory;rt::Process process(memory);rt::g_process=&process;
    std::string error;assert(process.LoadMainExe(argv[1],error));
    assert(process.PrepareMainThread(process.MainThread().cpu,error));
    const auto stageAddress=process.GetProcAddress(process.MainModule(),"CompositionStage");assert(stageAddress);
    ui::SoftwarePresentation software;ui::Presentation presentation(&software);
    unsigned checked=0;bool stopped=false;
    {
        rt::GuestExecution execution(process,[&](rt::GuestThread& thread) {
            win32::HostCallOutcome call;
            if(win32::TryHandleHostCall(thread.cpu,memory,call))return !call.requestExit;
            cpu::Interpreter interpreter(memory,thread.teb,thread.teb);
            auto result=interpreter.Step(thread.cpu);
            assert(result==cpu::StepResult::Continue || result==cpu::StepResult::Halted);return true;
        },rt::GuestExecution::Startup::InitializeModules);
        for(unsigned n=0;n<2000000;++n) {
            auto result=execution.RunOne();assert(result!=rt::GuestExecution::Result::AllocationFailed);
            const auto stage=memory.Read32(stageAddress);
            if(stage!=checked && stage<=28) {
                assert(stage==checked+1);Check(stage,user::S().screen.s,presentation);checked=stage;
            }
            if(result==rt::GuestExecution::Result::Stopped){stopped=true;break;}
            if(result==rt::GuestExecution::Result::Idle)rt::Clock::Sleep(1);
        }
    }
    assert(stopped && !win32::g_guestExitCode && checked==28);
    for(const auto& entry:user::S().windows)assert(entry.first==user::S().desktop);
    presentation.Detach();rt::g_process=nullptr;
    std::printf("PASSED: 28 real x86 composition scenes %dx%d; independent full-frame CPU/output oracle, triple output history/padding, screen ROP/readback and raw DIB\n",width,height);
#ifdef SR_OPENGL_TEST
    std::puts("PASSED: every composition scene matches actual OpenGL/EGL output, including backend reset while windows remain alive");
#endif
}
