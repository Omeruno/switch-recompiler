#pragma once
#include <array>

#if defined(__SWITCH__)
#include <glad/glad.h>
#elif defined(SR_OPENGL_TEST)
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif
#include <GL/glcorearb.h>
#endif

namespace ui::detail {
// Internal GL 3.3 handoff on one current context/native thread. Nested target
// calls share one snapshot; no allocations, glFinish or error consumption.
// Caller must not change contexts or start queries/transform feedback inside it.
class GdiGlScope {
public:
#if defined(__SWITCH__) || defined(SR_OPENGL_TEST)
    explicit GdiGlScope(bool enabled=true):enabled_(enabled) {
        if(!enabled_ || depth_++)return;
        glGetIntegerv(GL_CURRENT_PROGRAM,&program_);glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao_);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw_);glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read_);
        glGetIntegerv(GL_VIEWPORT,viewport_.data());glGetIntegerv(GL_SCISSOR_BOX,scissor_.data());
        glGetIntegerv(GL_POLYGON_MODE,polygon_.data());glGetBooleanv(GL_COLOR_WRITEMASK,mask_.data());
        glGetFloatv(GL_COLOR_CLEAR_VALUE,clear_.data());
        glGetIntegerv(GL_ACTIVE_TEXTURE,&active_);glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture_);glGetIntegeri_v(GL_SAMPLER_BINDING,0,&sampler_);
        glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&packBuffer_);
        glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&unpackBuffer_);
        for(size_t i=0;i<stores_.size();++i)glGetIntegerv(StoreNames[i],&stores_[i]);
        for(size_t i=0;i<caps_.size();++i){caps_[i]=glIsEnabled(CapNames[i]);glDisable(CapNames[i]);}
        // GL 3.3 guarantees these eight clip-distance enables. Additional
        // implementation-specific distances must be disabled by the caller.
        for(size_t i=0;i<clips_.size();++i){clips_[i]=glIsEnabled(GL_CLIP_DISTANCE0+GLenum(i));glDisable(GL_CLIP_DISTANCE0+GLenum(i));}
        glBindSampler(0,0);glBindBuffer(GL_PIXEL_PACK_BUFFER,0);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
        for(auto name:StoreNames)glPixelStorei(name,(name==GL_PACK_ALIGNMENT || name==GL_UNPACK_ALIGNMENT)?4:0);
        glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    }
    ~GdiGlScope() {
        if(!enabled_ || --depth_)return;
        glUseProgram(GLuint(program_));glBindVertexArray(GLuint(vao_));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,GLuint(draw_));glBindFramebuffer(GL_READ_FRAMEBUFFER,GLuint(read_));
        glViewport(viewport_[0],viewport_[1],viewport_[2],viewport_[3]);
        glScissor(scissor_[0],scissor_[1],scissor_[2],scissor_[3]);
        glPolygonMode(GL_FRONT_AND_BACK,GLenum(polygon_[0]));
        glColorMask(mask_[0],mask_[1],mask_[2],mask_[3]);glClearColor(clear_[0],clear_[1],clear_[2],clear_[3]);
        for(size_t i=0;i<caps_.size();++i)Set(CapNames[i],caps_[i]);
        for(size_t i=0;i<clips_.size();++i)Set(GL_CLIP_DISTANCE0+GLenum(i),clips_[i]);
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,GLuint(texture_));glBindSampler(0,GLuint(sampler_));
        glActiveTexture(GLenum(active_));
        glBindBuffer(GL_PIXEL_PACK_BUFFER,GLuint(packBuffer_));glBindBuffer(GL_PIXEL_UNPACK_BUFFER,GLuint(unpackBuffer_));
        for(size_t i=0;i<stores_.size();++i)glPixelStorei(StoreNames[i],stores_[i]);
    }
#else
    explicit GdiGlScope(bool=true) {}
#endif
    GdiGlScope(const GdiGlScope&)=delete;
    GdiGlScope& operator=(const GdiGlScope&)=delete;
private:
#if defined(__SWITCH__) || defined(SR_OPENGL_TEST)
    static void Set(GLenum cap,GLboolean enabled){if(enabled)glEnable(cap);else glDisable(cap);}
    inline static thread_local unsigned depth_=0;
    inline static constexpr std::array<GLenum,13> CapNames={GL_BLEND,GL_DEPTH_TEST,GL_STENCIL_TEST,
        GL_CULL_FACE,GL_SCISSOR_TEST,GL_DITHER,GL_FRAMEBUFFER_SRGB,GL_RASTERIZER_DISCARD,
        GL_COLOR_LOGIC_OP,GL_SAMPLE_ALPHA_TO_COVERAGE,GL_SAMPLE_ALPHA_TO_ONE,GL_SAMPLE_COVERAGE,GL_SAMPLE_MASK};
    inline static constexpr std::array<GLenum,12> StoreNames={GL_PACK_ALIGNMENT,GL_PACK_ROW_LENGTH,
        GL_PACK_SKIP_PIXELS,GL_PACK_SKIP_ROWS,GL_PACK_SWAP_BYTES,GL_PACK_LSB_FIRST,
        GL_UNPACK_ALIGNMENT,GL_UNPACK_ROW_LENGTH,GL_UNPACK_SKIP_PIXELS,GL_UNPACK_SKIP_ROWS,
        GL_UNPACK_SWAP_BYTES,GL_UNPACK_LSB_FIRST};
    bool enabled_;
    GLint program_=0,vao_=0,draw_=0,read_=0,active_=0,texture_=0,sampler_=0,packBuffer_=0,unpackBuffer_=0;
    std::array<GLint,4> viewport_{},scissor_{};
    std::array<GLint,2> polygon_{};
    std::array<GLboolean,4> mask_{};
    std::array<GLfloat,4> clear_{};
    std::array<GLboolean,CapNames.size()> caps_{};
    std::array<GLboolean,8> clips_{};
    std::array<GLint,StoreNames.size()> stores_{};
#endif
};
} // namespace ui::detail
