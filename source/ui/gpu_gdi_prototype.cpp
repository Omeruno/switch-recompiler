#include "gpu_gdi_prototype.h"
#include "gdi_gl_scope.h"
#include <algorithm>
#include <cstring>
#include <utility>
#if defined(__SWITCH__)
#include <glad/glad.h>
#elif defined(SR_OPENGL_TEST)
#define GL_GLEXT_PROTOTYPES 1
#include <GL/glcorearb.h>
#endif

namespace ui {
GpuGdiPrototype::GpuGdiPrototype(PresentationAllocator* allocator)
    :allocator_(allocator),checkpoint_(allocator),scratch_(allocator) {
    for(auto& c:commands_)c.input=PresentationBuffer<uint32_t>(allocator);
}
GpuGdiPrototype::~GpuGdiPrototype() {Shutdown();}
void GpuGdiPrototype::ClearJournal() {
    for(size_t i=0;i<count_;++i)commands_[i].input.Clear();
    count_=flushed_=inputBytes_=0;
}
void GpuGdiPrototype::Shutdown() {
    DeleteDevice();ClearJournal();checkpoint_.Clear();scratch_.Clear();surface_={};mode_=Mode::Empty;
}
bool GpuGdiPrototype::Initialize(int width,int height) {
    Shutdown();stats_={};
    if(width<=0 || height<=0 || width>MaxSide || height>MaxSide)return false;
    if(!checkpoint_.Resize(size_t(width)*height) || !scratch_.Resize(size_t(width)*height)) {Shutdown();return false;}
    std::fill(checkpoint_.data(),checkpoint_.data()+checkpoint_.size(),0);
    surface_.width=width;surface_.height=height;surface_.pitch=width*4;
    surface_.bits=reinterpret_cast<uint8_t*>(checkpoint_.data());
    if(!OpenDevice()){Shutdown();return false;}
    mode_=Mode::Gpu;return true;
}
void GpuGdiPrototype::Apply(const Command& c) {
    const auto clip=c.rect.Intersect({0,0,surface_.width,surface_.height});
    for(int y=clip.top;y<clip.bottom;++y)for(int x=clip.left;x<clip.right;++x)
        surface_.Set(x,y,c.copy?c.input[size_t(y-c.rect.top)*c.rect.Width()+x-c.rect.left]:c.color);
}
void GpuGdiPrototype::Replay() {
    for(size_t i=0;i<count_;++i)Apply(commands_[i]);
    ClearJournal();
}
void GpuGdiPrototype::Recover(bool contextAlive) {
    // Last verified CPU checkpoint + all owned commands, including GPU-flushed
    // commands, survives partial batches, readback failure and lost contexts.
    Replay();if(contextAlive)DeleteDevice();else target_=upload_=fbo_=vao_=program_=batchProgram_=0;
    mode_=Mode::CpuFallback;++stats_.recoveries;
}
void GpuGdiPrototype::ContextLost() {if(mode_==Mode::Gpu)Recover(false);}
bool GpuGdiPrototype::Room(size_t bytes) {
    if(count_==MaxCommands || inputBytes_>MaxInputBytes-bytes)return CpuRead()!=nullptr;
    return true;
}
bool GpuGdiPrototype::Fill(Rect r,uint32_t rgb) {
    if(mode_==Mode::Empty)return false;
    r=r.Intersect({0,0,surface_.width,surface_.height});if(r.Empty())return true;
    Command c;c.rect=r;c.color=rgb&0xffffff;
    if(!Room(0))return false;
    if(mode_==Mode::CpuFallback)Apply(c);else commands_[count_++]=std::move(c);
    return true;
}
bool GpuGdiPrototype::Copy(Rect r,const uint32_t* pixels,size_t count) {
    if(mode_==Mode::Empty)return false;
    const int64_t w=int64_t(r.right)-r.left,h=int64_t(r.bottom)-r.top;
    if(!pixels || w<=0 || h<=0 || w>MaxSide || h>MaxSide || uint64_t(w)*uint64_t(h)!=count)return false;
    if(r.Intersect({0,0,surface_.width,surface_.height}).Empty())return true;
    Command c;c.rect=r;c.copy=true;c.input=PresentationBuffer<uint32_t>(allocator_);
    if(!c.input.Resize(count))return false;
    std::memcpy(c.input.data(),pixels,count*4); // Snapshot BEFORE checkpoint may alter borrowed CPU input.
    if(!Room(count*4))return false;
    if(mode_==Mode::CpuFallback)Apply(c);
    else {inputBytes_+=count*4;commands_[count_++]=std::move(c);}
    return true;
}
bool GpuGdiPrototype::FlushGpu() {
    if(mode_!=Mode::Gpu)return false;
    detail::GdiGlScope glScope;
    while(flushed_<count_) {
        size_t end=flushed_+1;
        if(batching_ && !commands_[flushed_].copy) {
            for(;end<count_ && !commands_[end].copy;++end) {
                bool overlap=false;
                for(size_t i=flushed_;i<end;++i)if(commands_[i].rect.Intersects(commands_[end].rect)){overlap=true;break;}
                if(overlap)break; // ordered batches; never commute overlapping writes
            }
        }
        const bool ok=end-flushed_>1?DrawFillBatch(flushed_,end-flushed_):Draw(commands_[flushed_]);
        if(!ok){Recover(true);return false;}
        flushed_=end;
    }
    return true;
}
const Surface* GpuGdiPrototype::CpuRead() {
    if(mode_==Mode::Empty)return nullptr;
    detail::GdiGlScope glScope(mode_==Mode::Gpu);
    if(mode_==Mode::Gpu) {
        if(FlushGpu()) {
            // Read into separate storage. Failed/partial read must not damage
            // the recovery checkpoint before replaying the retained journal.
            if(ReadDevice()) {
                for(size_t i=0;i<checkpoint_.size();++i)checkpoint_[i]=scratch_[i]&0xffffff;
                ClearJournal();++stats_.checkpoints;
            } else Recover(true);
        }
    }
    return &surface_;
}

#if defined(__SWITCH__) || defined(SR_OPENGL_TEST)
namespace {
GLuint Shader(GLenum kind,const char* source) {
    const auto s=glCreateShader(kind);if(!s)return 0;
    glShaderSource(s,1,&source,nullptr);glCompileShader(s);GLint ok=0;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
    if(!ok){glDeleteShader(s);return 0;}return s;
}
const char* vs=R"(#version 330 core
void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.0-1.0,0,1);})";
const char* fs=R"(#version 330 core
uniform sampler2D image;uniform int operation;uniform vec3 fillColor;uniform ivec2 origin;
out vec4 color;
void main(){color=operation==0?vec4(fillColor,1):vec4(texelFetch(image,ivec2(gl_FragCoord.xy)-origin,0).rgb,1);})";
const char* batchVs=R"(#version 330 core
uniform ivec4 rectangles[64];uniform vec3 colors[64];uniform ivec2 targetSize;
flat out vec3 paint;
void main(){int v=gl_VertexID;vec2 corner=vec2((v==1||v==4||v==5)?1:0,(v==2||v==3||v==5)?1:0);
ivec4 r=rectangles[gl_InstanceID];vec2 p=vec2(r.xy)+corner*vec2(r.zw);
gl_Position=vec4(p/vec2(targetSize)*2.0-1.0,0,1);paint=colors[gl_InstanceID];})";
const char* batchFs=R"(#version 330 core
flat in vec3 paint;out vec4 color;void main(){color=vec4(paint,1);})";
GLuint Program(const char* vertex,const char* fragment) {
    const auto a=Shader(GL_VERTEX_SHADER,vertex),b=Shader(GL_FRAGMENT_SHADER,fragment);
    if(!a || !b){if(a)glDeleteShader(a);if(b)glDeleteShader(b);return 0;}
    const auto p=glCreateProgram();glAttachShader(p,a);glAttachShader(p,b);glLinkProgram(p);
    glDeleteShader(a);glDeleteShader(b);GLint ok=0;glGetProgramiv(p,GL_LINK_STATUS,&ok);
    if(!ok){glDeleteProgram(p);return 0;}return p;
}
}
bool GpuGdiPrototype::OpenDevice() {
    detail::GdiGlScope glScope;
    static_assert(MaxCommands==64,"batch shader uniform capacity must match command limit");
    if(glGetError()!=GL_NO_ERROR)return false;
    glActiveTexture(GL_TEXTURE0);
    program_=Program(vs,fs);batchProgram_=Program(batchVs,batchFs);if(!program_ || !batchProgram_)return false;
    locations_.operation=glGetUniformLocation(program_,"operation");locations_.image=glGetUniformLocation(program_,"image");
    locations_.origin=glGetUniformLocation(program_,"origin");locations_.fill=glGetUniformLocation(program_,"fillColor");
    locations_.rects=glGetUniformLocation(batchProgram_,"rectangles[0]");locations_.colors=glGetUniformLocation(batchProgram_,"colors[0]");
    locations_.size=glGetUniformLocation(batchProgram_,"targetSize");
    if(locations_.operation<0 || locations_.image<0 || locations_.origin<0 || locations_.fill<0 ||
       locations_.rects<0 || locations_.colors<0 || locations_.size<0)return false;
    glGenVertexArrays(1,&vao_);glGenFramebuffers(1,&fbo_);glGenTextures(1,&target_);glGenTextures(1,&upload_);
    if(!vao_ || !fbo_ || !target_ || !upload_)return false;
    glBindTexture(GL_TEXTURE_2D,target_);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,surface_.width,surface_.height,0,GL_BGRA,GL_UNSIGNED_BYTE,nullptr);
    glBindFramebuffer(GL_FRAMEBUFFER,fbo_);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,target_,0);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)return false;
    glDisable(GL_SCISSOR_TEST);glDisable(GL_DITHER);glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glDisable(GL_STENCIL_TEST);
    glDisable(GL_FRAMEBUFFER_SRGB);glDisable(GL_CULL_FACE);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    // Keep sampler input distinct from the attached render target even for a
    // fill branch: no texture feedback loop is permitted by this prototype.
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,upload_);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    const uint32_t black=0;glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_BGRA,GL_UNSIGNED_BYTE,&black);
    return glGetError()==GL_NO_ERROR;
}
void GpuGdiPrototype::DeleteDevice() {
    detail::GdiGlScope glScope(target_ || upload_ || fbo_ || vao_ || program_ || batchProgram_);
    if(target_)glDeleteTextures(1,&target_);
    if(upload_)glDeleteTextures(1,&upload_);
    if(fbo_)glDeleteFramebuffers(1,&fbo_);
    if(vao_)glDeleteVertexArrays(1,&vao_);
    if(program_)glDeleteProgram(program_);
    if(batchProgram_)glDeleteProgram(batchProgram_);
    target_=upload_=fbo_=vao_=program_=batchProgram_=0;
}
bool GpuGdiPrototype::Draw(const Command& c) {
    detail::GdiGlScope glScope;
    if(glGetError()!=GL_NO_ERROR)return false;
    glBindFramebuffer(GL_FRAMEBUFFER,fbo_);glViewport(0,0,surface_.width,surface_.height);
    glUseProgram(program_);glBindVertexArray(vao_);glEnable(GL_SCISSOR_TEST);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,upload_);
    const auto r=c.rect.Intersect({0,0,surface_.width,surface_.height});glScissor(r.left,r.top,r.Width(),r.Height());
    glUniform1i(locations_.operation,c.copy?1:0);
    if(c.copy) {
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,upload_);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);glPixelStorei(GL_UNPACK_ALIGNMENT,4);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
        glPixelStorei(GL_UNPACK_SKIP_ROWS,0);glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,c.rect.Width(),c.rect.Height(),0,GL_BGRA,GL_UNSIGNED_BYTE,c.input.data());
        glUniform1i(locations_.image,0);glUniform2i(locations_.origin,c.rect.left,c.rect.top);++stats_.uploads;
    } else glUniform3f(locations_.fill,((c.color>>16)&255)/255.f,((c.color>>8)&255)/255.f,(c.color&255)/255.f);
    glDrawArrays(GL_TRIANGLES,0,3);++stats_.draws;++stats_.submittedCommands;return glGetError()==GL_NO_ERROR;
}
bool GpuGdiPrototype::DrawFillBatch(size_t first,size_t count) {
    detail::GdiGlScope glScope;
    if(glGetError()!=GL_NO_ERROR)return false;
    std::array<GLint,MaxCommands*4> rectangles{};std::array<GLfloat,MaxCommands*3> colors{};
    for(size_t i=0;i<count;++i) {
        const auto& c=commands_[first+i];const auto& r=c.rect;
        rectangles[i*4]=r.left;rectangles[i*4+1]=r.top;rectangles[i*4+2]=r.Width();rectangles[i*4+3]=r.Height();
        colors[i*3]=((c.color>>16)&255)/255.f;colors[i*3+1]=((c.color>>8)&255)/255.f;colors[i*3+2]=(c.color&255)/255.f;
    }
    glBindFramebuffer(GL_FRAMEBUFFER,fbo_);glViewport(0,0,surface_.width,surface_.height);
    glUseProgram(batchProgram_);glBindVertexArray(vao_);glDisable(GL_SCISSOR_TEST);
    glUniform4iv(locations_.rects,GLsizei(count),rectangles.data());glUniform3fv(locations_.colors,GLsizei(count),colors.data());
    glUniform2i(locations_.size,surface_.width,surface_.height);
    glDrawArraysInstanced(GL_TRIANGLES,0,6,GLsizei(count));++stats_.draws;++stats_.batches;
    stats_.submittedCommands+=count;stats_.batchedCommands+=count;return glGetError()==GL_NO_ERROR;
}
bool GpuGdiPrototype::ReadDevice() {
    detail::GdiGlScope glScope;
    if(glGetError()!=GL_NO_ERROR)return false;
    glBindFramebuffer(GL_READ_FRAMEBUFFER,fbo_);glPixelStorei(GL_PACK_ALIGNMENT,4);glPixelStorei(GL_PACK_ROW_LENGTH,0);
    glBindBuffer(GL_PIXEL_PACK_BUFFER,0);glPixelStorei(GL_PACK_SKIP_ROWS,0);glPixelStorei(GL_PACK_SKIP_PIXELS,0);
    glReadPixels(0,0,surface_.width,surface_.height,GL_BGRA,GL_UNSIGNED_BYTE,scratch_.data());++stats_.readbacks;
    return glGetError()==GL_NO_ERROR;
}
#else
bool GpuGdiPrototype::OpenDevice(){return false;}
void GpuGdiPrototype::DeleteDevice(){}
bool GpuGdiPrototype::Draw(const Command&){return false;}
bool GpuGdiPrototype::DrawFillBatch(size_t,size_t){return false;}
bool GpuGdiPrototype::ReadDevice(){return false;}
#endif
} // namespace ui
