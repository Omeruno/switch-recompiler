#include "opengl_presentation.h"
#include "presentation_cursor.h"
#include <algorithm>
#include <cstring>
#include <chrono>
#if defined(__SWITCH__)
#include <glad/glad.h>
#elif defined(SR_OPENGL_TEST)
#define GL_GLEXT_PROTOTYPES 1
#include <GL/glcorearb.h>
#endif

namespace ui {
OpenGlPresentation::OpenGlPresentation(PresentationAllocator* allocator) {
    screen_.versions=PresentationBuffer<uint64_t>(allocator);
    ownership_.versions=PresentationBuffer<uint64_t>(allocator);
    for(auto& layer:layers_)layer.versions=PresentationBuffer<uint64_t>(allocator);
}
void OpenGlPresentation::ResetTexture(Texture& texture) {
    texture.width=texture.height=0;texture.id=texture.generation=0;texture.versions.Clear();
}
void OpenGlPresentation::Reset() {
    ResetTexture(screen_);ResetTexture(ownership_);for(auto& layer:layers_)ResetTexture(layer);
    uploadedBytes_=0;sceneEpoch_=frameId_=frameGeneration_=0;
}
void OpenGlPresentation::ResetStats() {
    uploadedBytes_=totalUploaded_=uploadCalls_=layerDraws_=0;timings_={};
    // Every query has ended before Render returns. Discard old pending samples
    // without reading/waiting, and reuse the bounded query objects in order.
    for(auto& timer:timers_)timer.pending=false;
}
size_t OpenGlPresentation::CacheBytes() const {
    size_t bytes=screen_.versions.size()*8+ownership_.versions.size()*8;
    for(const auto& layer:layers_)bytes+=layer.versions.size()*8;
    return bytes;
}
size_t OpenGlPresentation::TextureBytes() const {
    size_t bytes=screen_.allocatedBytes+ownership_.allocatedBytes;
    for(const auto& layer:layers_)bytes+=layer.allocatedBytes;
    return bytes+(cursorTexture_?kCursorWidth*kCursorHeight*4:0);
}
#if defined(__SWITCH__) || defined(SR_OPENGL_TEST)
namespace {
using Clock=std::chrono::steady_clock;
uint64_t Elapsed(Clock::time_point start) {
    return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count());
}
const char* vertex=R"GLSL(#version 330 core
void main() {
    vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);
    gl_Position=vec4(p*2.0-1.0,0,1);
})GLSL";
const char* fragment=R"GLSL(#version 330 core
uniform sampler2D screenImage;
uniform sampler2D cursorImage;
uniform usampler2D ownershipImage;
uniform int compositionMode;
uniform ivec4 layerRect;
uniform uint layerToken;
uniform ivec4 imageRect;
uniform ivec2 sourceSize;
uniform ivec3 cursorState;
uniform int outputHeight;
out vec4 color;
void main() {
    ivec2 p=ivec2(gl_FragCoord.xy);p.y=outputHeight-1-p.y;
    ivec2 c=p-cursorState.xy;
    if(cursorState.z!=0 && c.x>=0 && c.y>=0 && c.x<11 && c.y<19) {
        vec4 arrow=texelFetch(cursorImage,c,0);
        if(arrow.a>0.5) {color=arrow;return;}
    }
    ivec2 q=p-imageRect.xy;
    if(q.x<0 || q.y<0 || q.x>=imageRect.z || q.y>=imageRect.w) {color=vec4(0,0,0,1);return;}
    ivec2 source=(q*sourceSize)/imageRect.zw;
    if(compositionMode!=0) {
        uint owner=texelFetch(ownershipImage,source,0).r;
        if(compositionMode==1 && owner!=0u) {color=vec4(0,0,0,1);return;}
        if(compositionMode==2) {
            if(owner!=layerToken)discard;
            source-=layerRect.xy;
            if(any(lessThan(source,ivec2(0))) || any(greaterThanEqual(source,layerRect.zw)))discard;
        }
    }
    color=vec4(texelFetch(screenImage,source,0).rgb,1);
})GLSL";
GLuint Compile(GLenum type,const char* text) {
    GLuint shader=glCreateShader(type);if(!shader)return 0;
    glShaderSource(shader,1,&text,nullptr);glCompileShader(shader);
    GLint ok=0;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
    if(!ok) {glDeleteShader(shader);return 0;}return shader;
}
}
bool OpenGlPresentation::Initialize() {
    Shutdown();
    const GLuint vs=Compile(GL_VERTEX_SHADER,vertex),fs=Compile(GL_FRAGMENT_SHADER,fragment);
    if(!vs || !fs) {if(vs)glDeleteShader(vs);if(fs)glDeleteShader(fs);error_="shader compilation failed";return false;}
    program_=glCreateProgram();glAttachShader(program_,vs);glAttachShader(program_,fs);glLinkProgram(program_);
    glDeleteShader(vs);glDeleteShader(fs);
    GLint ok=0;glGetProgramiv(program_,GL_LINK_STATUS,&ok);
    if(!ok) {Shutdown();error_="shader linking failed";return false;}
    screenUniform_=glGetUniformLocation(program_,"screenImage");
    cursorUniform_=glGetUniformLocation(program_,"cursorImage");
    rectUniform_=glGetUniformLocation(program_,"imageRect");
    sizeUniform_=glGetUniformLocation(program_,"sourceSize");
    heightUniform_=glGetUniformLocation(program_,"outputHeight");
    cursorStateUniform_=glGetUniformLocation(program_,"cursorState");
    ownershipUniform_=glGetUniformLocation(program_,"ownershipImage");
    modeUniform_=glGetUniformLocation(program_,"compositionMode");
    layerRectUniform_=glGetUniformLocation(program_,"layerRect");
    layerTokenUniform_=glGetUniformLocation(program_,"layerToken");
    glGenVertexArrays(1,&vao_);glGenTextures(1,&cursorTexture_);
    glBindTexture(GL_TEXTURE_2D,cursorTexture_);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,0);
    uint32_t cursor[kCursorWidth*kCursorHeight];
    for(int y=0;y<kCursorHeight;++y)for(int x=0;x<kCursorWidth;++x)cursor[y*kCursorWidth+x]=CursorRgba(x,y);
    glPixelStorei(GL_UNPACK_ALIGNMENT,4);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,kCursorWidth,kCursorHeight,0,GL_RGBA,GL_UNSIGNED_BYTE,cursor);
    if(!vao_ || !cursorTexture_ || glGetError()!=GL_NO_ERROR) {
        Shutdown();error_="GL resource allocation failed";return false;
    }
    // GL 3.3 timer queries are optional diagnostics. A bounded ring avoids
    // glFinish and blocking reads even when the GPU falls behind the producer.
    GLint bits=0;glGetQueryiv(GL_TIME_ELAPSED,GL_QUERY_COUNTER_BITS,&bits);
    timerAvailable_=bits>0;
    if(timerAvailable_)for(auto& timer:timers_)glGenQueries(1,&timer.handle);
    if(glGetError()!=GL_NO_ERROR) {
        for(auto& timer:timers_) {if(timer.handle)glDeleteQueries(1,&timer.handle);timer={};}
        timerAvailable_=false;
    }
    timings_={};totalUploaded_=uploadCalls_=layerDraws_=0;error_="none";return true;
}
void OpenGlPresentation::PollTimings() {
    for(auto& timer:timers_)if(timer.pending) {
        GLint ready=0;glGetQueryObjectiv(timer.handle,GL_QUERY_RESULT_AVAILABLE,&ready);
        if(!ready)continue;
        GLuint64 ns=0;glGetQueryObjectui64v(timer.handle,GL_QUERY_RESULT,&ns);
        ++timings_.gpuSamples;timings_.gpuNs+=ns;
        timings_.maxGpuNs=std::max<uint64_t>(timings_.maxGpuNs,ns);timer.pending=false;
    }
}
void OpenGlPresentation::Shutdown() {
    for(auto& timer:timers_) {if(timer.handle)glDeleteQueries(1,&timer.handle);timer={};}
    timerAvailable_=false;
    auto destroy=[](Texture& texture){if(texture.handle)glDeleteTextures(1,&texture.handle);texture.handle=0;texture.allocatedBytes=0;};
    destroy(screen_);destroy(ownership_);for(auto& layer:layers_)destroy(layer);
    if(cursorTexture_)glDeleteTextures(1,&cursorTexture_);
    if(vao_)glDeleteVertexArrays(1,&vao_);
    if(program_) {
        // A bound program marked for deletion survives until the next use.
        // Do not hand that zombie binding to the shared-context GDI scope:
        // switching away would delete it before the scope could restore it.
        GLint current=0;glGetIntegerv(GL_CURRENT_PROGRAM,&current);
        if(GLuint(current)==program_)glUseProgram(0);
        glDeleteProgram(program_);
    }
    cursorTexture_=program_=vao_=0;Reset();
}
bool OpenGlPresentation::Upload(Texture& texture,const Surface& s,uint64_t id,uint64_t generation,
    const PresentationBuffer<uint64_t>* versions,bool integer) {
    if(!texture.handle)glGenTextures(1,&texture.handle);
    if(!texture.handle)return false;
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,texture.handle);
    glPixelStorei(GL_UNPACK_ALIGNMENT,4);glPixelStorei(GL_UNPACK_ROW_LENGTH,s.pitch/4);
    const int cols=(s.width+31)/32,rows=(s.height+31)/32;const size_t count=size_t(cols)*rows;
    const bool tracked=versions && versions->size()==count;
    bool full=texture.width!=s.width || texture.height!=s.height || texture.id!=id ||
        texture.generation!=generation || texture.versions.size()!=count || !tracked;
    if(texture.width!=s.width || texture.height!=s.height) {
        glTexImage2D(GL_TEXTURE_2D,0,integer?GL_R32UI:GL_RGBA8,s.width,s.height,0,
            integer?GL_RED_INTEGER:GL_BGRA,integer?GL_UNSIGNED_INT:GL_UNSIGNED_BYTE,nullptr);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,0);
        texture.allocatedBytes=size_t(s.width)*s.height*4;
    }
    auto upload=[&](int x,int y,int w,int h) {
        glTexSubImage2D(GL_TEXTURE_2D,0,x,y,w,h,integer?GL_RED_INTEGER:GL_BGRA,
            integer?GL_UNSIGNED_INT:GL_UNSIGNED_BYTE,s.Row(y)+size_t(x)*4);
        uploadedBytes_+=uint64_t(w)*h*4;++uploadCalls_;
    };
    std::array<Rect,129> updates{};size_t updateCount=0;uint64_t area=0;
    if(!full)for(int ty=0;ty<rows && !full;++ty)for(int tx=0;tx<cols;) {
        if(texture.versions[size_t(ty)*cols+tx]==(*versions)[size_t(ty)*cols+tx]){++tx;continue;}
        const int first=tx++;
        while(tx<cols && texture.versions[size_t(ty)*cols+tx]!=(*versions)[size_t(ty)*cols+tx])++tx;
        Rect r(first*32,ty*32,std::min(tx*32,s.width),std::min((ty+1)*32,s.height));
        area+=uint64_t(r.Width())*r.Height();
        if(updateCount && updates[updateCount-1].left==r.left && updates[updateCount-1].right==r.right && updates[updateCount-1].bottom==r.top)
            updates[updateCount-1].bottom=r.bottom;
        else updates[updateCount++]=r;
        if(updateCount>128 || area>uint64_t(s.width)*s.height/2){full=true;break;}
    }
    if(full)upload(0,0,s.width,s.height);
    else for(size_t n=0;n<updateCount;++n){const auto& r=updates[n];upload(r.left,r.top,r.Width(),r.Height());}
    glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
    texture.width=s.width;texture.height=s.height;texture.id=id;texture.generation=generation;
    if(tracked && texture.versions.Resize(count))std::memcpy(texture.versions.data(),versions->data(),count*8);
    else texture.versions.Clear();
    return true;
}
bool OpenGlPresentation::Render(const PresentationFrame& frame,const OutputBuffer& out) {
    return RenderImpl(frame,out,nullptr,nullptr,0);
}
bool OpenGlPresentation::RenderResident(const PresentationFrame& frame,const OutputBuffer& out,
    const ResidentFrameView& resident,const uint32_t* tokens,size_t count) {
    return RenderImpl(frame,out,&resident,tokens,count);
}
bool OpenGlPresentation::RenderImpl(const PresentationFrame& frame,const OutputBuffer& out,
    const ResidentFrameView* resident,const uint32_t* tokens,size_t count) {
    uploadedBytes_=0;const auto& s=frame.surface;const auto& v=frame.viewport;
    if(!program_ || !s.Valid() || s.width>8192 || s.height>8192 || s.bpp!=32 || s.pitch<int64_t(s.width)*4 || s.pitch%4 ||
       uint64_t(s.width)*s.height*4>Presentation::MaxFrameBytes || !v.Valid() ||
       out.width<=0 || out.height<=0 || out.width>8192 || out.height>8192 || v.x<0 || v.y<0 ||
       int64_t(v.x)+v.width>out.width || int64_t(v.y)+v.height>out.height) {
        error_="invalid GPU frame";return false;
    }
    const auto* scene=frame.scene;
    std::array<const ResidentImageView*,WindowLayers::MaxLayers> direct{};
    if(resident) {
        if(!scene || count!=resident->count || count>4 ||
           (count && (!tokens || !resident->images))) {error_="invalid resident frame";return false;}
        size_t residentPixels=0;
        for(size_t i=0;i<count;++i) {
            const auto& image=resident->images[i];
            if(!tokens[i] || !image.texture || image.cpu || image.width<=0 || image.height<=0 ||
               image.width>512 || image.height>512) {error_="invalid resident image";return false;}
            const auto pixels=size_t(image.width)*image.height;
            if(pixels>512u*512u-residentPixels){error_="resident pixel budget exceeded";return false;}
            residentPixels+=pixels;
            for(size_t j=0;j<i;++j)if(tokens[i]==tokens[j]){error_="duplicate resident token";return false;}
        }
    }
    if(scene) {
        if(!scene->owners || scene->width!=s.width || scene->height!=s.height || scene->count>layers_.size() ||
           (scene->count && !scene->layers)){error_="invalid GPU scene";return false;}
        size_t bytes=0,matched=0;
        for(size_t n=0;n<scene->count;++n) {
            const auto& layer=scene->layers[n];const auto* image=layer.surface;
            if(resident)for(size_t i=0;i<count;++i)if(tokens[i]==layer.token){direct[n]=&resident->images[i];++matched;}
            if((!direct[n] && (!image || !image->Valid() || image->bpp!=32 || image->width>8192 || image->height>8192 ||
               image->pitch<int64_t(image->width)*4 || image->pitch%4)) || !layer.token ||
               layer.left < -16384 || layer.top < -16384 || layer.left>16384 || layer.top>16384) {
                error_="invalid GPU window layer";return false;
            }
            const uint64_t size=direct[n]?uint64_t(direct[n]->width)*direct[n]->height*4:uint64_t(image->width)*image->height*4;
            if(size>WindowLayers::MaxPixelBytes-bytes){error_="GPU layer budget exceeded";return false;}
            bytes+=size_t(size);
            for(size_t i=0;i<n;++i)if(scene->layers[i].token==layer.token){error_="duplicate GPU layer token";return false;}
        }
        if(matched!=count){error_="resident token missing from scene";return false;}
    }
    if(frameId_!=frame.resourceId || frameGeneration_!=frame.resourceGeneration) {
        ResetTexture(ownership_);for(auto& layer:layers_)ResetTexture(layer);
    }
    const bool tracked=frame.owner && frame.serial && frame.resourceId;
    PollTimings();
    TimerSlot* active=nullptr;
    if(timerAvailable_) {
        for(auto& timer:timers_)if(!timer.pending && timer.handle){active=&timer;break;}
        if(active)glBeginQuery(GL_TIME_ELAPSED,active->handle);
        else ++timings_.gpuSkipped;
    }
    const auto uploadStart=Clock::now();
    bool ok=Upload(screen_,s,frame.resourceId,frame.resourceGeneration,tracked?frame.tileVersions:nullptr);
    timings_.screenUploadBytes+=uploadedBytes_;
    if(scene && ok) {
        if(sceneEpoch_!=scene->generation)ResetTexture(ownership_);
        Surface owners;owners.width=s.width;owners.height=s.height;owners.pitch=s.width*4;
        owners.bits=reinterpret_cast<uint8_t*>(const_cast<uint32_t*>(scene->owners));
        const auto beforeOwners=uploadedBytes_;
        ok=Upload(ownership_,owners,frame.resourceId,scene->generation,scene->versions,true);
        timings_.ownershipUploadBytes+=uploadedBytes_-beforeOwners;
        const auto beforeLayers=uploadedBytes_;
        for(size_t n=0;n<scene->count && ok;++n) {
            const auto& layer=scene->layers[n];
            if(direct[n]) {
                // Retire a previous CPU cache; never delete the borrowed texture.
                if(layers_[n].handle)glDeleteTextures(1,&layers_[n].handle);
                layers_[n].handle=0;layers_[n].allocatedBytes=0;ResetTexture(layers_[n]);
            } else ok=Upload(layers_[n],*layer.surface,layer.token,layer.generation,layer.versions);
        }
        timings_.layerUploadBytes+=uploadedBytes_-beforeLayers;
    }
    for(size_t n=scene?scene->count:0;n<layers_.size();++n) {
        if(layers_[n].handle)glDeleteTextures(1,&layers_[n].handle);
        layers_[n].handle=0;layers_[n].allocatedBytes=0;ResetTexture(layers_[n]);
    }
    if(!scene && ownership_.handle){glDeleteTextures(1,&ownership_.handle);ownership_.handle=0;ownership_.allocatedBytes=0;ResetTexture(ownership_);}
    const auto uploadNs=Elapsed(uploadStart);
    timings_.peakTextureBytes=std::max(timings_.peakTextureBytes,TextureBytes());
    if(!ok){if(active){glEndQuery(GL_TIME_ELAPSED);active->pending=true;}Reset();error_="GPU scene allocation failed";return false;}
    const auto drawStart=Clock::now();
    glBindFramebuffer(GL_FRAMEBUFFER,0);glViewport(0,0,out.width,out.height);
    glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glDisable(GL_SCISSOR_TEST);glDisable(GL_DITHER);
    glDisable(GL_FRAMEBUFFER_SRGB);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    glUseProgram(program_);glBindVertexArray(vao_);
    glUniform1i(screenUniform_,0);
    glUniform1i(cursorUniform_,1);glUniform1i(ownershipUniform_,2);
    glUniform1i(modeUniform_,scene?1:0);
    glUniform4i(rectUniform_,v.x,v.y,v.width,v.height);
    glUniform2i(sizeUniform_,s.width,s.height);
    glUniform1i(heightUniform_,out.height);
    const int cx=std::max(0,std::min(frame.cursor.x,s.width-1)),cy=std::max(0,std::min(frame.cursor.y,s.height-1));
    glUniform3i(cursorStateUniform_,v.x+int(int64_t(cx)*v.width/s.width),
        v.y+int(int64_t(cy)*v.height/s.height),int(frame.cursor.visible));
    glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,cursorTexture_);
    glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_2D,scene?ownership_.handle:0);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,screen_.handle);
    glDrawArrays(GL_TRIANGLES,0,3);
    if(scene) {
        glUniform1i(modeUniform_,2);glEnable(GL_SCISSOR_TEST);
        auto edge=[](int64_t value,int source,int output){value=std::max<int64_t>(0,std::min<int64_t>(value,source));return int((value*output+source-1)/source);};
        for(size_t n=0;n<scene->count;++n) {
            const auto& layer=scene->layers[n];
            const int width=direct[n]?direct[n]->width:layer.surface->width;
            const int height=direct[n]?direct[n]->height:layer.surface->height;
            const int left=v.x+edge(layer.left,s.width,v.width),top=v.y+edge(layer.top,s.height,v.height);
            const int right=v.x+edge(int64_t(layer.left)+width,s.width,v.width);
            const int bottom=v.y+edge(int64_t(layer.top)+height,s.height,v.height);
            if(left>=right || top>=bottom)continue;
            glScissor(left,out.height-bottom,right-left,bottom-top);
            glBindTexture(GL_TEXTURE_2D,direct[n]?direct[n]->texture:layers_[n].handle);
            glUniform4i(layerRectUniform_,layer.left,layer.top,width,height);
            glUniform1ui(layerTokenUniform_,layer.token);glDrawArrays(GL_TRIANGLES,0,3);++layerDraws_;
        }
        glDisable(GL_SCISSOR_TEST);
    }
    if(active){glEndQuery(GL_TIME_ELAPSED);active->pending=true;}
    const auto drawNs=Elapsed(drawStart);
    if(glGetError()!=GL_NO_ERROR) {Reset();error_="GPU upload/draw failed";return false;}
    ++timings_.frames;timings_.uploadCpuNs+=uploadNs;timings_.drawCpuNs+=drawNs;
    timings_.maxUploadCpuNs=std::max(timings_.maxUploadCpuNs,uploadNs);
    timings_.maxDrawCpuNs=std::max(timings_.maxDrawCpuNs,drawNs);
    frameId_=frame.resourceId;frameGeneration_=frame.resourceGeneration;sceneEpoch_=scene?scene->generation:0;
    totalUploaded_+=uploadedBytes_;error_="none";return true;
}
#else
void OpenGlPresentation::PollTimings() {}
bool OpenGlPresentation::Initialize() {error_="OpenGL unavailable in this build";return false;}
void OpenGlPresentation::Shutdown() {Reset();}
bool OpenGlPresentation::Render(const PresentationFrame&,const OutputBuffer&) {return false;}
bool OpenGlPresentation::RenderResident(const PresentationFrame&,const OutputBuffer&,const ResidentFrameView&,const uint32_t*,size_t) {return false;}
bool OpenGlPresentation::RenderImpl(const PresentationFrame&,const OutputBuffer&,const ResidentFrameView*,const uint32_t*,size_t) {return false;}
#endif
} // namespace ui
