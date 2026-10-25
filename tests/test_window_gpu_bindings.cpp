#define GL_GLEXT_PROTOTYPES 1
#include <EGL/egl.h>
#include <GL/glcorearb.h>
#include "source/runtime/guest_session.h"
#include "source/runtime/guest_execution.h"
#include "source/runtime/clock.h"
#include "source/cpu/interpreter.h"
#include "source/win32/window_gpu_bindings.h"
#include "source/win32/user32.h"
#include <cassert>
#include <cstdio>
#include <new>
#include <vector>
namespace user=win32::user;
using Bindings=user::WindowGpuBindings;
static void Check(Bindings& b,const Bindings::Lease& l,int width,int height,const std::vector<uint32_t>& pixels,bool gpu=true) {
    const auto* image=b.CpuRead(l);assert(image && image->width==width && image->height==height);
    for(int y=0;y<height;++y)for(int x=0;x<width;++x)assert(image->Get(x,y)==pixels[size_t(y)*width+x]);
    const auto* stats=b.Counters(l);assert(stats && stats->draws && b.GpuActive()==gpu);
    assert(gpu?!stats->recoveries:stats->recoveries>0);
}
struct SessionIdentity {uint64_t window=0;uint32_t hwnd=0,hdc=0;};
static SessionIdentity Run(const char* path,Bindings& bindings,SessionIdentity previous,void (*lose)(void*)=nullptr,void* loss=nullptr) {
    rt::GuestSession session;user::EnableUi(1280,720);auto& p=session.ProcessObject();auto& m=session.Memory();std::string error;
    assert(p.LoadMainExe(path,error) && p.PrepareMainThread(p.MainThread().cpu,error));
    auto probe=p.GetProcAddress(p.MainModule(),"GpuWindowProbe");assert(probe);
    unsigned checked=0;bool stopped=false,lost=false;uint32_t firstHwnd=0,firstDc=0;uint64_t identity=0;
    Bindings::Lease old,current,other;std::vector<uint32_t> expected(100*80);
    {
        rt::GuestExecution execution(p,[&](rt::GuestThread& t) {
            win32::HostCallOutcome call;if(win32::TryHandleHostCall(t.cpu,m,call))return !call.requestExit;
            cpu::Interpreter cpu(m,t.teb,t.teb);const auto result=cpu.Step(t.cpu);
            assert(result==cpu::StepResult::Continue || result==cpu::StepResult::Halted);return true;
        },rt::GuestExecution::Startup::InitializeModules);
        for(unsigned i=0;i<500000;++i) {
            const auto result=execution.RunOne();assert(result!=rt::GuestExecution::Result::AllocationFailed);
            const auto stage=m.Read32(probe);
            if(stage!=checked && stage>=1 && stage<=10) {
                assert(stage==checked+1);checked=stage;
                const auto hwnd=m.Read32(probe+4),dc=m.Read32(probe+8),second=m.Read32(probe+12),mem=m.Read32(probe+16);
                const auto& screen=user::S().screen.s;std::vector<uint32_t> cpuBefore(size_t(screen.width)*screen.height);
                for(int y=0;y<screen.height;++y)for(int x=0;x<screen.width;++x)cpuBefore[size_t(y)*screen.width+x]=screen.Get(x,y);
                Bindings::Lease rejected;assert(!bindings.Acquire(mem,rejected) && !rejected.target);
                if(stage==1) {
                    assert(bindings.Acquire(dc,current) && bindings.Acquire(second,other));
                    assert(current.target==other.target && current.raster.dcIdentity!=other.raster.dcIdentity);
                    identity=current.raster.windowIdentity;assert(identity && identity!=previous.window);firstHwnd=hwnd;firstDc=dc;
                    if(previous.window)assert(hwnd==previous.hwnd && dc==previous.hdc);
                    assert(current.raster.frame.left==40 && current.raster.frame.top==50);
                    assert(bindings.Fill(current,{0,0,100,80},0x1234ab));std::fill(expected.begin(),expected.end(),0x1234ab);
                    Check(bindings,current,100,80,expected);old=current;
                    if(lose) {
                        assert(bindings.Fill(current,{0,0,1,1},0xcccc11));expected[0]=0xcccc11;
                        lose(loss);bindings.ContextLost();Check(bindings,current,100,80,expected,false);
                        assert(!bindings.Acquire(dc,rejected));lost=true;
                    }

                } else if(stage==2) {
                    assert(!bindings.Valid(old) && !bindings.Fill(old,{0,0,1,1},0xffffff));
                    assert(bindings.Acquire(dc,current) && current.target==old.target);
                    assert(current.raster.viewportOrg.x==3 && current.raster.viewportOrg.y==4);
                    assert(bindings.Fill(current,{-20,-20,200,200},0x44cc88));
                    for(int y=1;y<35;++y)for(int x=2;x<40;++x)if(!(x>=20 && y>=10 && y<30))expected[size_t(y)*100+x]=0x44cc88;
                    Check(bindings,current,100,80,expected);old=current;
                } else if(stage==3) {
                    assert(!bindings.Valid(old) && !bindings.CpuRead(old));assert(bindings.Acquire(second,current));
                    assert(current.target==old.target);old=current;
                } else if(stage==4) {
                    assert(!bindings.Valid(old));assert(bindings.Acquire(second,current) && current.target==old.target);
                    assert(current.raster.frame.left==70 && current.raster.frame.top==80);
                    assert(bindings.Fill(current,{0,0,100,80},0xaabbcc));std::fill(expected.begin(),expected.end(),0xaabbcc);
                    Check(bindings,current,100,80,expected);old=current;
                } else if(stage==5) {
                    assert(!bindings.Valid(old));assert(bindings.Acquire(second,current) && current.target!=old.target);
                    assert(bindings.Pixels()==120*90);expected.assign(120*90,0);
                    assert(bindings.Fill(current,{0,0,120,90},0x887733));std::fill(expected.begin(),expected.end(),0x887733);
                    Check(bindings,current,120,90,expected);old=current;
                } else if(stage==6) {
                    assert(!bindings.Valid(old) && !bindings.Acquire(second,current));assert(bindings.Pixels()==120*90);
                } else if(stage==7) {
                    assert(!bindings.Valid(old));assert(bindings.Acquire(second,current) && current.target==old.target);
                    Check(bindings,current,120,90,expected);old=current;
                } else if(stage==8) {
                    assert(!bindings.Valid(old) && !bindings.CpuRead(old));bindings.Prune();assert(!bindings.Pixels());
                    // Deliberate ABA regression: the real next CreateWindow gets
                    // the freed HWND; its old DC must not access the new window.
                    user::S().nextHandle=firstHwnd;
                } else if(stage==9) {
                    assert(hwnd==firstHwnd && !bindings.Valid(old));
                    ui::Surface* target=nullptr;int x=0,y=0;ui::Region clip;
                    assert(!ui::g_gdi.Target(second,target,x,y,clip));
                    const auto bytes=user::S().windowLayers.PixelBytes();
                    assert(!ui::g_gdi.SetPixel(second,0,0,0xffffff));
                    assert(user::S().windowLayers.PixelBytes()==bytes);
                    assert(!bindings.Acquire(second,rejected));assert(bindings.Acquire(dc,current));
                    assert(current.raster.windowIdentity!=identity && current.target!=old.target);
                    expected.assign(100*80,0);assert(bindings.Fill(current,{0,0,100,80},0x654321));
                    std::fill(expected.begin(),expected.end(),0x654321);Check(bindings,current,100,80,expected);
                    // Unsupported mapping and dimensions must never claim GPU support.
                    auto* nativeDc=ui::g_gdi.Dc(dc);assert(nativeDc);nativeDc->st.mapMode=8;
                    assert(!bindings.Acquire(dc,rejected) && !bindings.Valid(current));nativeDc->st.mapMode=1;
                    auto frame=user::Win(hwnd)->rect;user::Win(hwnd)->rect.right=frame.left+600;
                    assert(!bindings.Acquire(dc,rejected));user::Win(hwnd)->rect=frame;
                } else if(stage==10) {bindings.Prune();assert(!bindings.Pixels());}
                // Diagnostic bound-GPU drawing never pretends to update ordinary
                // GDI's canonical screen or its retained window/ownership planes.
                for(int y=0;y<screen.height;++y)for(int x=0;x<screen.width;++x)assert(screen.Get(x,y)==cpuBefore[size_t(y)*screen.width+x]);
                if(lost)break;
            }
            if(result==rt::GuestExecution::Result::Stopped){assert(p.MainThread().exitCode==0);stopped=true;break;}
            if(result==rt::GuestExecution::Result::Idle)rt::Clock::Sleep(1);
        }
    }
    assert(lost?checked==1:(stopped && checked==10));bindings.Prune();assert(!bindings.Pixels());return {identity,firstHwnd,firstDc};
}
int main(int argc,char** argv) {
    assert(argc==2);rt::Clock::SetFastForward(true);
    auto d=eglGetDisplay(EGL_DEFAULT_DISPLAY);assert(eglInitialize(d,nullptr,nullptr) && eglBindAPI(EGL_OPENGL_API));
    EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_NONE};
    EGLConfig config;EGLint n;assert(eglChooseConfig(d,attrs,&config,1,&n) && n==1);
    EGLint size[]={EGL_WIDTH,96,EGL_HEIGHT,64,EGL_NONE};auto surface=eglCreatePbufferSurface(d,config,size);assert(surface!=EGL_NO_SURFACE);
    EGLint version[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,3,EGL_CONTEXT_OPENGL_PROFILE_MASK,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,EGL_NONE};
    auto context=eglCreateContext(d,config,EGL_NO_CONTEXT,version);assert(context!=EGL_NO_CONTEXT);
    assert(eglMakeCurrent(d,surface,surface,context));
    {
        Bindings bindings;assert(bindings.Initialize());const auto identity=Run(argv[1],bindings,{});Run(argv[1],bindings,identity);
        struct Loss {EGLDisplay display;EGLContext* context;} loss{d,&context};
        Run(argv[1],bindings,{},[](void* data) {
            auto& l=*static_cast<Loss*>(data);
            assert(eglMakeCurrent(l.display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT));
            assert(eglDestroyContext(l.display,*l.context));*l.context=EGL_NO_CONTEXT;
        },&loss);
        bindings.Shutdown();
    }
    assert(eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT));
    assert(context==EGL_NO_CONTEXT && eglDestroySurface(d,surface) && eglTerminate(d));
    std::puts("GPU window bindings: PASS (20 actual-x86 lifecycle stages, identities, clips, resize, stale DC/HWND reuse, fresh sessions, actual context loss/abort cleanup; canonical CPU unchanged)");
}
