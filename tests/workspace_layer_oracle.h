#pragma once
#ifdef SR_OPENGL_TEST
#define GL_GLEXT_PROTOTYPES 1
#include <EGL/egl.h>
#include <GL/glcorearb.h>
#include "source/ui/opengl_presentation.h"
#endif
#include "source/ui/presentation.h"
#include <vector>
#include <cstdio>
#include <cstdlib>
#define LAYER_CHECK(x) do{if(!(x)){std::fprintf(stderr,"workspace layers %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
class WorkspaceLayerOracle {
    ui::SoftwarePresentation software_;
    ui::Presentation presentation_{&software_};
    std::vector<uint32_t> expected_,output_=std::vector<uint32_t>(1287*720,0x12345678);
#ifdef SR_OPENGL_TEST
    EGLDisplay display_=EGL_NO_DISPLAY;EGLSurface surface_=EGL_NO_SURFACE;EGLContext context_=EGL_NO_CONTEXT;
    ui::OpenGlPresentation gl_;
    ui::Presentation gpu_{&gl_};
    std::vector<uint32_t> pixels_=std::vector<uint32_t>(1280*720);
#endif
public:
    WorkspaceLayerOracle() {
#ifdef SR_OPENGL_TEST
        display_=eglGetDisplay(EGL_DEFAULT_DISPLAY);LAYER_CHECK(display_!=EGL_NO_DISPLAY && eglInitialize(display_,nullptr,nullptr) && eglBindAPI(EGL_OPENGL_API));
        EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
        EGLConfig config;EGLint count;LAYER_CHECK(eglChooseConfig(display_,attrs,&config,1,&count) && count==1);
        EGLint dims[]={EGL_WIDTH,1280,EGL_HEIGHT,720,EGL_NONE};surface_=eglCreatePbufferSurface(display_,config,dims);LAYER_CHECK(surface_!=EGL_NO_SURFACE);
        EGLint context[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,3,EGL_CONTEXT_OPENGL_PROFILE_MASK,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,EGL_NONE};
        context_=eglCreateContext(display_,config,EGL_NO_CONTEXT,context);LAYER_CHECK(context_!=EGL_NO_CONTEXT);
        LAYER_CHECK(eglMakeCurrent(display_,surface_,surface_,context_) && gl_.Initialize());
#endif
    }
    ~WorkspaceLayerOracle() {
#ifdef SR_OPENGL_TEST
        gpu_.Detach();gl_.Shutdown();LAYER_CHECK(eglMakeCurrent(display_,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT));
        LAYER_CHECK(eglDestroyContext(display_,context_) && eglDestroySurface(display_,surface_) && eglTerminate(display_));
#endif
    }
    void Check(const ui::Surface& canonical,const ui::WindowSceneView& scene) {
        LAYER_CHECK(scene.owners && scene.count>0);LAYER_CHECK(presentation_.Submit(canonical,scene));
        LAYER_CHECK(presentation_.LayerCount()>0);
        expected_.resize(1280*720);
        for(int y=0;y<720;++y)for(int x=0;x<1280;++x){auto rgb=canonical.Get(x,y);expected_[size_t(y)*1280+x]=0xff000000u|((rgb&255)<<16)|(rgb&0xff00)|((rgb>>16)&255);}
#ifdef SR_OPENGL_TEST
        LAYER_CHECK(gpu_.Submit(canonical,scene));
#endif
        Replay();
    }
    void Replay() {
        if(expected_.empty())return;
        LAYER_CHECK(presentation_.Render({output_.data(),1280,720,1287,output_.size(),true,1}));
        for(int y=0;y<720;++y)for(size_t x=0;x<1287;++x)LAYER_CHECK(output_[size_t(y)*1287+x]==(x<1280?expected_[size_t(y)*1280+x]:0x12345678));
#ifdef SR_OPENGL_TEST
        LAYER_CHECK(gpu_.Render({nullptr,1280,720,0,0}));glReadPixels(0,0,1280,720,GL_RGBA,GL_UNSIGNED_BYTE,pixels_.data());LAYER_CHECK(glGetError()==GL_NO_ERROR);
        for(int y=0;y<720;++y)for(int x=0;x<1280;++x)LAYER_CHECK(pixels_[size_t(719-y)*1280+x]==expected_[size_t(y)*1280+x]);
        LAYER_CHECK(gl_.LayerDraws()>0);
#endif
    }
};
