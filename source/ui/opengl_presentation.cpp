#include "opengl_presentation.h"
#include "presentation_cursor.h"
#include <algorithm>
#include <cstring>
#if defined(__SWITCH__)
#include <glad/glad.h>
#elif defined(SR_OPENGL_TEST)
#define GL_GLEXT_PROTOTYPES 1
#include <GL/glcorearb.h>
#endif

namespace ui {
#if defined(__SWITCH__) || defined(SR_OPENGL_TEST)
namespace {
const char* vertex=R"GLSL(#version 330 core
void main() {
    vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);
    gl_Position=vec4(p*2.0-1.0,0,1);
})GLSL";
const char* fragment=R"GLSL(#version 330 core
uniform sampler2D screenImage;
uniform sampler2D cursorImage;
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
    color=vec4(texelFetch(screenImage,(q*sourceSize)/imageRect.zw,0).rgb,1);
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
    glGenVertexArrays(1,&vao_);glGenTextures(1,&texture_);glGenTextures(1,&cursorTexture_);
    glBindTexture(GL_TEXTURE_2D,cursorTexture_);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,0);
    uint32_t cursor[kCursorWidth*kCursorHeight];
    for(int y=0;y<kCursorHeight;++y)for(int x=0;x<kCursorWidth;++x)cursor[y*kCursorWidth+x]=CursorRgba(x,y);
    glPixelStorei(GL_UNPACK_ALIGNMENT,4);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,kCursorWidth,kCursorHeight,0,GL_RGBA,GL_UNSIGNED_BYTE,cursor);
    if(!vao_ || !texture_ || !cursorTexture_ || glGetError()!=GL_NO_ERROR) {
        Shutdown();error_="GL resource allocation failed";return false;
    }
    totalUploaded_=uploadCalls_=0;error_="none";return true;
}
void OpenGlPresentation::Shutdown() {
    if(texture_)glDeleteTextures(1,&texture_);
    if(cursorTexture_)glDeleteTextures(1,&cursorTexture_);
    if(vao_)glDeleteVertexArrays(1,&vao_);
    if(program_)glDeleteProgram(program_);
    texture_=cursorTexture_=program_=vao_=0;Reset();
}
void OpenGlPresentation::Reset() {width_=height_=0;id_=generation_=0;versions_.Clear();uploadedBytes_=0;}
bool OpenGlPresentation::Render(const PresentationFrame& frame,const OutputBuffer& out) {
    uploadedBytes_=0;
    const auto& s=frame.surface;const auto& v=frame.viewport;
    if(!program_ || !s.Valid() || s.bpp!=32 || s.pitch<s.width*4 || s.pitch%4 ||
       uint64_t(s.width)*s.height*4>Presentation::MaxFrameBytes || !v.Valid() ||
       out.width<=0 || out.height<=0 || out.width>8192 || out.height>8192) {
        error_="invalid GPU frame";return false;
    }
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,texture_);
    glPixelStorei(GL_UNPACK_ALIGNMENT,4);glPixelStorei(GL_UNPACK_ROW_LENGTH,s.pitch/4);
    const int cols=(s.width+31)/32,rows=(s.height+31)/32;
    const size_t count=size_t(cols)*rows;
    const bool tracked=frame.tileVersions && frame.tileVersions->size()==count;
    bool full=width_!=s.width || height_!=s.height || id_!=frame.resourceId ||
              generation_!=frame.resourceGeneration || versions_.size()!=count || !tracked;
    if(width_!=s.width || height_!=s.height) {
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,s.width,s.height,0,GL_BGRA,GL_UNSIGNED_BYTE,nullptr);
        // Bind one complete mip level; sampling is texelFetch with exact integer coordinates.
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,0);
    }
    auto upload=[&](int x,int y,int w,int h) {
        glTexSubImage2D(GL_TEXTURE_2D,0,x,y,w,h,GL_BGRA,GL_UNSIGNED_BYTE,s.Row(y)+size_t(x)*4);
        uploadedBytes_+=uint64_t(w)*h*4;++uploadCalls_;
    };
    std::array<Rect,129> updates{};size_t updateCount=0;uint64_t area=0;
    if(!full)for(int ty=0;ty<rows && !full;++ty)for(int tx=0;tx<cols;) {
        if(versions_[size_t(ty)*cols+tx]==(*frame.tileVersions)[size_t(ty)*cols+tx]) {++tx;continue;}
        const int first=tx++;
        while(tx<cols && versions_[size_t(ty)*cols+tx]!=(*frame.tileVersions)[size_t(ty)*cols+tx])++tx;
        Rect r(first*32,ty*32,std::min(tx*32,s.width),std::min((ty+1)*32,s.height));
        area+=uint64_t(r.Width())*r.Height();
        if(updateCount && updates[updateCount-1].left==r.left && updates[updateCount-1].right==r.right &&
           updates[updateCount-1].bottom==r.top)updates[updateCount-1].bottom=r.bottom;
        else updates[updateCount++]=r;
        if(updateCount>128 || area>uint64_t(s.width)*s.height/2) {full=true;break;}
    }
    if(full)upload(0,0,s.width,s.height);
    else for(size_t i=0;i<updateCount;++i) {
        const auto& r=updates[i];upload(r.left,r.top,r.Width(),r.Height());
    }
    glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
    glBindFramebuffer(GL_FRAMEBUFFER,0);glViewport(0,0,out.width,out.height);
    glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glDisable(GL_SCISSOR_TEST);glDisable(GL_DITHER);
    glDisable(GL_FRAMEBUFFER_SRGB);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    glUseProgram(program_);glBindVertexArray(vao_);
    glUniform1i(screenUniform_,0);
    glUniform1i(cursorUniform_,1);
    glUniform4i(rectUniform_,v.x,v.y,v.width,v.height);
    glUniform2i(sizeUniform_,s.width,s.height);
    glUniform1i(heightUniform_,out.height);
    const int cx=std::max(0,std::min(frame.cursor.x,s.width-1)),cy=std::max(0,std::min(frame.cursor.y,s.height-1));
    glUniform3i(cursorStateUniform_,v.x+int(int64_t(cx)*v.width/s.width),
        v.y+int(int64_t(cy)*v.height/s.height),int(frame.cursor.visible));
    glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,cursorTexture_);
    glDrawArrays(GL_TRIANGLES,0,3);
    if(glGetError()!=GL_NO_ERROR) {Reset();error_="GPU upload/draw failed";return false;}
    width_=s.width;height_=s.height;id_=frame.resourceId;generation_=frame.resourceGeneration;
    if(tracked && versions_.Resize(count))std::memcpy(versions_.data(),frame.tileVersions->data(),count*sizeof(uint64_t));
    else versions_.Clear();
    totalUploaded_+=uploadedBytes_;error_="none";return true;
}
#else
bool OpenGlPresentation::Initialize() {error_="OpenGL unavailable in this build";return false;}
void OpenGlPresentation::Shutdown() {Reset();}
void OpenGlPresentation::Reset() {versions_.Clear();uploadedBytes_=0;}
bool OpenGlPresentation::Render(const PresentationFrame&,const OutputBuffer&) {return false;}
#endif
} // namespace ui
