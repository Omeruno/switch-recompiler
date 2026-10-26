#include "gpu_gdi_targets.h"
#include "gdi_gl_scope.h"
#include <algorithm>
#include <atomic>
#include <limits>
#include <new>
#if defined(__SWITCH__)
#include <glad/glad.h>
#elif defined(SR_OPENGL_TEST)
#define GL_GLEXT_PROTOTYPES 1
#include <GL/glcorearb.h>
#endif

namespace ui {
namespace {
uint64_t Generation() {
    static std::atomic<uint64_t> next{1};
    auto n=next.load();
    for(;;) {
        if(n>(std::numeric_limits<uint64_t>::max()>>8))return 0;
        if(next.compare_exchange_weak(n,n+1))return n;
    }
}
}
GpuGdiTargets::GpuGdiTargets(PresentationAllocator* allocator):allocator_(allocator),output_(allocator) {}
GpuGdiTargets::~GpuGdiTargets(){Shutdown();}
void GpuGdiTargets::Shutdown() {
    for(auto& s:slots_){s.image.reset();s.handle=0;s.pixels=0;s.pin.reset();s.retired=false;}
    DeletePresenter();frameOwner_=0;pixels_=0;output_.Clear();outputSurface_={};initialized_=gpuActive_=false;
}
bool GpuGdiTargets::Initialize() {
    Shutdown();stats_={};if(!OpenPresenter()){DeletePresenter();return false;}
    frameOwner_=Generation();if(!frameOwner_){Shutdown();return false;}
    initialized_=gpuActive_=true;return true;
}
GpuGdiPrototype* GpuGdiTargets::Retained(Handle h) {
    const auto i=h&255;if(!i || i>MaxTargets)return nullptr;
    auto& s=slots_[size_t(i-1)];return s.handle==h?s.image.get():nullptr;
}
const GpuGdiPrototype* GpuGdiTargets::Retained(Handle h) const {
    const auto i=h&255;if(!i || i>MaxTargets)return nullptr;
    const auto& s=slots_[size_t(i-1)];return s.handle==h?s.image.get():nullptr;
}
GpuGdiPrototype* GpuGdiTargets::Find(Handle h) {
    auto* p=Retained(h);return p && !slots_[size_t((h&255)-1)].retired?p:nullptr;
}
const GpuGdiPrototype* GpuGdiTargets::Find(Handle h) const {
    auto* p=Retained(h);return p && !slots_[size_t((h&255)-1)].retired?p:nullptr;
}
void GpuGdiTargets::CollectRetired() {
    for(auto& s:slots_)if(s.retired && s.pin.use_count()==1) {
        pixels_-=s.pixels;s.image.reset();s.pin.reset();s.handle=0;s.pixels=0;s.retired=false;
    }
}
bool GpuGdiTargets::PrepareFrame(const Layer* layers,size_t count,int width,int height,Frame& out) {
    if(!Validate(layers,count,width,height))return false;
    // Allocate nothing and issue no GL commands. Preserve out on rejection.
    Frame next;next.owner_=frameOwner_;next.count_=count;next.width_=width;next.height_=height;
    for(size_t i=0;i<count;++i) {
        next.layers_[i]=layers[i];next.pins_[i]=slots_[size_t((layers[i].target&255)-1)].pin;
    }
    out=std::move(next);return true;
}
bool GpuGdiTargets::ValidateFrame(const Frame& frame) const {
    if(!initialized_ || frame.owner_!=frameOwner_ || frame.width_<=0 || frame.height_<=0)return false;
    for(size_t i=0;i<frame.count_;++i) {
        const auto h=frame.layers_[i].target;
        if(!Retained(h) || frame.pins_[i]!=slots_[size_t((h&255)-1)].pin)return false;
    }
    return true;
}
bool GpuGdiTargets::Present(const Frame& frame) {
    if(!gpuActive_ || !ValidateFrame(frame))return false;
    return PresentImpl(frame.layers_.data(),frame.count_,frame.width_,frame.height_);
}
const Surface* GpuGdiTargets::ComposeCpu(const Frame& frame) {
    if(!ValidateFrame(frame))return nullptr;
    return ComposeCpuImpl(frame.layers_.data(),frame.count_,frame.width_,frame.height_);
}
GpuGdiTargets::Handle GpuGdiTargets::Create(int width,int height) {
    CollectRetired();
    if(!gpuActive_ || width<=0 || height<=0 || width>GpuGdiPrototype::MaxSide || height>GpuGdiPrototype::MaxSide)return 0;
    const auto size=size_t(width)*height;if(size>MaxPixels-pixels_)return 0;
    for(size_t i=0;i<slots_.size();++i)if(!slots_[i].image) {
        const auto generation=Generation();if(!generation)return 0;
        auto image=std::unique_ptr<GpuGdiPrototype>(new(std::nothrow) GpuGdiPrototype(allocator_));
        if(!image || !image->Initialize(width,height))return 0;
        auto& s=slots_[i];s.handle=(generation<<8)|(i+1);s.pixels=size;s.image=std::move(image);s.pin=std::make_shared<uint8_t>(0);s.retired=false;pixels_+=size;return s.handle;
    }
    return 0;
}
bool GpuGdiTargets::Destroy(Handle h) {
    if(!Find(h))return false;
    auto& s=slots_[size_t((h&255)-1)];s.retired=true;CollectRetired();return true;
}
bool GpuGdiTargets::Fill(Handle h,Rect r,uint32_t color) {
    auto* p=Find(h);if(!p)return false;const bool accepted=p->Fill(r,color);
    if(gpuActive_ && p->Backend()==GpuGdiPrototype::Mode::CpuFallback)Fallback();
    return accepted;
}
bool GpuGdiTargets::Copy(Handle h,Rect r,const uint32_t* data,size_t count) {
    auto* p=Find(h);if(!p)return false;const bool accepted=p->Copy(r,data,count);
    if(gpuActive_ && p->Backend()==GpuGdiPrototype::Mode::CpuFallback)Fallback();
    return accepted;
}
bool GpuGdiTargets::ImportCpu(Handle h,const uint32_t* rgb,size_t count) {
    auto* p=Find(h);if(!p)return false;const bool accepted=p->ImportCpu(rgb,count);
    if(gpuActive_ && p->Backend()==GpuGdiPrototype::Mode::CpuFallback)Fallback();
    return accepted;
}
const Surface* GpuGdiTargets::CpuRead(Handle h) {
    auto* p=Find(h);if(!p)return nullptr;const auto* view=p->CpuRead();
    if(gpuActive_ && p->Backend()==GpuGdiPrototype::Mode::CpuFallback)Fallback();
    return view;
}
const GpuGdiPrototype::Stats* GpuGdiTargets::TargetStats(Handle h) const {const auto* p=Find(h);return p?&p->Counters():nullptr;}
bool GpuGdiTargets::CopyTargetViaCpu(Handle dst,Rect at,Handle src,Rect from) {
    auto* d=Find(dst);auto* s=Find(src);if(!d || !s)return false;
    const int64_t w=int64_t(from.right)-from.left,h=int64_t(from.bottom)-from.top;
    if(w<=0 || h<=0 || w>GpuGdiPrototype::MaxSide || h>GpuGdiPrototype::MaxSide ||
       int64_t(at.right)-at.left!=w || int64_t(at.bottom)-at.top!=h || from.left<0 || from.top<0 ||
       from.right>s->surface_.width || from.bottom>s->surface_.height)return false;
    PresentationBuffer<uint32_t> snapshot(allocator_);if(!snapshot.Resize(size_t(w)*size_t(h)))return false;
    const auto* input=CpuRead(src);if(!input)return false;
    for(int y=0;y<h;++y)for(int x=0;x<w;++x)snapshot[size_t(y)*size_t(w)+x]=input->Get(from.left+x,from.top+y);
    return d->Copy(at,snapshot.data(),snapshot.size());
}
bool GpuGdiTargets::Validate(const Layer* layers,size_t count,int width,int height) const {
    if(!initialized_ || count>MaxLayers || (count && !layers) || width<=0 || height<=0 || width>MaxOutputWidth || height>MaxOutputHeight)return false;
    for(size_t i=0;i<count;++i) {
        const auto& r=layers[i].bounds;
        const int64_t w=int64_t(r.right)-r.left,h=int64_t(r.bottom)-r.top;
        if(!Find(layers[i].target) || w<=0 || h<=0 || w>MaxOutputWidth || h>MaxOutputHeight ||
           r.left<-MaxOutputWidth || r.top<-MaxOutputHeight || r.right>2*MaxOutputWidth || r.bottom>2*MaxOutputHeight)return false;
    }
    return true;
}
void GpuGdiTargets::Fallback() {
    for(auto& s:slots_)if(s.image && s.image->Backend()==GpuGdiPrototype::Mode::Gpu)s.image->Recover(true);
    DeletePresenter();gpuActive_=false;++stats_.failures;
}
void GpuGdiTargets::ContextLost() {
    if(!initialized_)return;
    for(auto& s:slots_)if(s.image)s.image->ContextLost();
    program_=vao_=0;gpuActive_=false;
}
bool GpuGdiTargets::Present(const Layer* layers,size_t count,int width,int height) {
    if(!gpuActive_ || !Validate(layers,count,width,height))return false;
    return PresentImpl(layers,count,width,height);
}
bool GpuGdiTargets::PresentImpl(const Layer* layers,size_t count,int width,int height) {
    detail::GdiGlScope glScope;
    for(size_t i=0;i<count;++i)if(!Retained(layers[i].target)->FlushGpu()){Fallback();return false;}
    if(!DrawFrame(layers,count,width,height)){Fallback();return false;}
    ++stats_.frames;return true;
}
const Surface* GpuGdiTargets::ComposeCpu(const Layer* layers,size_t count,int width,int height) {
    if(!Validate(layers,count,width,height))return nullptr;
    return ComposeCpuImpl(layers,count,width,height);
}
const Surface* GpuGdiTargets::ComposeCpuImpl(const Layer* layers,size_t count,int width,int height) {
    if(!output_.Resize(size_t(width)*height))return nullptr;
    std::array<const Surface*,MaxLayers> inputs{};
    for(size_t i=0;i<count;++i){
        auto* image=Retained(layers[i].target);inputs[i]=image->CpuRead();
        if(gpuActive_ && image->Backend()==GpuGdiPrototype::Mode::CpuFallback)Fallback();
        if(!inputs[i])return nullptr;
    }
    std::fill(output_.data(),output_.data()+output_.size(),0);
    outputSurface_.width=width;outputSurface_.height=height;outputSurface_.pitch=width*4;outputSurface_.bits=reinterpret_cast<uint8_t*>(output_.data());
    for(size_t i=0;i<count;++i) {
        const auto& r=layers[i].bounds;const auto clip=r.Intersect({0,0,width,height});const auto& source=*inputs[i];
        for(int y=clip.top;y<clip.bottom;++y)for(int x=clip.left;x<clip.right;++x)
            outputSurface_.Set(x,y,source.Get((x-r.left)*source.width/r.Width(),(y-r.top)*source.height/r.Height()));
    }
    return &outputSurface_;
}

#if defined(__SWITCH__) || defined(SR_OPENGL_TEST)
namespace {
GLuint CompilePresenter(GLenum type,const char* text) {
    const auto s=glCreateShader(type);if(!s)return 0;
    glShaderSource(s,1,&text,nullptr);glCompileShader(s);GLint ok=0;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
    if(!ok){glDeleteShader(s);return 0;}return s;
}
const char* vertex=R"(#version 330 core
void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.0-1.0,0,1);})";
const char* actualFragment=R"(#version 330 core
uniform sampler2D image;uniform ivec4 bounds;uniform ivec2 sourceSize;uniform int outputHeight;
out vec4 color;
void main(){ivec2 p=ivec2(gl_FragCoord.xy);p.y=outputHeight-1-p.y;
ivec2 q=((p-bounds.xy)*sourceSize)/bounds.zw;color=vec4(texelFetch(image,q,0).rgb,1);})";
}
bool GpuGdiTargets::OpenPresenter() {
    detail::GdiGlScope glScope;
    if(glGetError()!=GL_NO_ERROR)return false;
    const auto v=CompilePresenter(GL_VERTEX_SHADER,vertex),f=CompilePresenter(GL_FRAGMENT_SHADER,actualFragment);
    if(!v || !f){if(v)glDeleteShader(v);if(f)glDeleteShader(f);return false;}
    program_=glCreateProgram();glAttachShader(program_,v);glAttachShader(program_,f);glLinkProgram(program_);
    glDeleteShader(v);glDeleteShader(f);GLint ok=0;glGetProgramiv(program_,GL_LINK_STATUS,&ok);if(!ok)return false;
    glGenVertexArrays(1,&vao_);return vao_ && glGetError()==GL_NO_ERROR;
}
void GpuGdiTargets::DeletePresenter() {
    detail::GdiGlScope glScope(program_ || vao_);
    if(program_)glDeleteProgram(program_);
    if(vao_)glDeleteVertexArrays(1,&vao_);
    program_=vao_=0;
}
bool GpuGdiTargets::DrawFrame(const Layer* layers,size_t count,int width,int height) {
    detail::GdiGlScope glScope;
    if(glGetError()!=GL_NO_ERROR)return false;
    glBindFramebuffer(GL_FRAMEBUFFER,0);glViewport(0,0,width,height);glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DITHER);glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glDisable(GL_STENCIL_TEST);glDisable(GL_CULL_FACE);glDisable(GL_FRAMEBUFFER_SRGB);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(program_);glBindVertexArray(vao_);glActiveTexture(GL_TEXTURE0);glEnable(GL_SCISSOR_TEST);
    glUniform1i(glGetUniformLocation(program_,"image"),0);glUniform1i(glGetUniformLocation(program_,"outputHeight"),height);
    for(size_t i=0;i<count;++i) {
        const auto* p=Retained(layers[i].target);const auto& r=layers[i].bounds;const auto clip=r.Intersect({0,0,width,height});if(clip.Empty())continue;
        glBindTexture(GL_TEXTURE_2D,p->target_);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glUniform2i(glGetUniformLocation(program_,"sourceSize"),p->surface_.width,p->surface_.height);
        glUniform4i(glGetUniformLocation(program_,"bounds"),r.left,r.top,r.Width(),r.Height());
        glScissor(clip.left,height-clip.bottom,clip.Width(),clip.Height());glDrawArrays(GL_TRIANGLES,0,3);++stats_.drawAttempts;
    }
    return glGetError()==GL_NO_ERROR;
}
#else
bool GpuGdiTargets::OpenPresenter(){return false;}
void GpuGdiTargets::DeletePresenter(){}
bool GpuGdiTargets::DrawFrame(const Layer*,size_t,int,int){return false;}
#endif
} // namespace ui
