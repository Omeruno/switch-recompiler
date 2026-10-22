#include "switch_gpu_output.h"
#ifdef __SWITCH__
#include <switch.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <glad/glad.h>
namespace ui {
bool SwitchGpuOutput::Initialize() {
    Shutdown();
    display_=eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if(display_==EGL_NO_DISPLAY || !eglInitialize(display_,nullptr,nullptr)) {
        display_=nullptr;error_="EGL display initialization failed";return false;
    }
    EGLConfig config=nullptr;EGLint count=0;
    const EGLint attrs[]={EGL_RENDERABLE_TYPE,EGL_OPENGL_BIT,EGL_SURFACE_TYPE,EGL_WINDOW_BIT,
        EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
    if(!eglBindAPI(EGL_OPENGL_API) || !eglChooseConfig(display_,attrs,&config,1,&count) || count!=1) {
        Shutdown();error_="EGL configuration failed";return false;
    }
    surface_=eglCreateWindowSurface(display_,config,nwindowGetDefault(),nullptr);
    if(surface_==EGL_NO_SURFACE) {Shutdown();error_="EGL window creation failed";return false;}
    const EGLint ctxAttrs[]={EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
        EGL_CONTEXT_MAJOR_VERSION_KHR,3,EGL_CONTEXT_MINOR_VERSION_KHR,3,EGL_NONE};
    context_=eglCreateContext(display_,config,EGL_NO_CONTEXT,ctxAttrs);
    if(context_==EGL_NO_CONTEXT || !MakeCurrent()) {Shutdown();error_="EGL context creation failed";return false;}
    if(!gladLoadGL()) {Shutdown();error_="OpenGL entry points unavailable";return false;}
    if(!eglSwapInterval(display_,1) || !eglMakeCurrent(display_,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT)) {
        Shutdown();error_="EGL context transfer failed";return false;
    }
    error_="none";return true;
}
bool SwitchGpuOutput::MakeCurrent() {
    if(display_ && surface_ && context_ && eglMakeCurrent(display_,surface_,surface_,context_))return true;
    error_="EGL make-current failed";return false;
}
bool SwitchGpuOutput::Swap() {
    if(display_ && surface_ && eglSwapBuffers(display_,surface_))return true;
    error_="EGL swap failed";return false;
}
void SwitchGpuOutput::Shutdown() {
    if(display_) {
        eglMakeCurrent(display_,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
        if(context_)eglDestroyContext(display_,context_);
        if(surface_)eglDestroySurface(display_,surface_);
        eglTerminate(display_);
    }
    display_=surface_=context_=nullptr;
}
} // namespace ui
#else
namespace ui {
bool SwitchGpuOutput::Initialize() {error_="Switch EGL unavailable";return false;}
bool SwitchGpuOutput::MakeCurrent() {return false;}
bool SwitchGpuOutput::Swap() {return false;}
void SwitchGpuOutput::Shutdown() {}
}
#endif
