#define GL_GLEXT_PROTOTYPES 1
#include <EGL/egl.h>
#include <GL/glcorearb.h>
#include "source/ui/gpu_gdi_targets.h"
#include "source/ui/opengl_presentation.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <vector>

namespace {
constexpr int W=96,H=64;
const GLenum caps[]={GL_BLEND,GL_DEPTH_TEST,GL_STENCIL_TEST,GL_CULL_FACE,GL_SCISSOR_TEST,
    GL_DITHER,GL_FRAMEBUFFER_SRGB,GL_RASTERIZER_DISCARD,GL_COLOR_LOGIC_OP,
    GL_SAMPLE_ALPHA_TO_COVERAGE,GL_SAMPLE_ALPHA_TO_ONE,GL_SAMPLE_COVERAGE,GL_SAMPLE_MASK};
const GLenum stores[]={GL_PACK_ALIGNMENT,GL_PACK_ROW_LENGTH,GL_PACK_SKIP_PIXELS,GL_PACK_SKIP_ROWS,
    GL_PACK_SWAP_BYTES,GL_PACK_LSB_FIRST,GL_UNPACK_ALIGNMENT,GL_UNPACK_ROW_LENGTH,
    GL_UNPACK_SKIP_PIXELS,GL_UNPACK_SKIP_ROWS,GL_UNPACK_SWAP_BYTES,GL_UNPACK_LSB_FIRST};
struct State {
    std::vector<GLint> ints;
    std::vector<GLfloat> floats;
    bool operator==(const State& b) const {return ints==b.ints && floats==b.floats;}
};
State Snapshot() {
    State s;
    for(GLenum name:{GL_CURRENT_PROGRAM,GL_VERTEX_ARRAY_BINDING,GL_DRAW_FRAMEBUFFER_BINDING,
        GL_READ_FRAMEBUFFER_BINDING,GL_ACTIVE_TEXTURE,GL_PIXEL_PACK_BUFFER_BINDING,GL_PIXEL_UNPACK_BUFFER_BINDING}) {
        GLint v=0;glGetIntegerv(name,&v);s.ints.push_back(v);
    }
    for(auto name:stores){GLint v=0;glGetIntegerv(name,&v);s.ints.push_back(v);}
    for(auto name:{GL_VIEWPORT,GL_SCISSOR_BOX,GL_COLOR_WRITEMASK}) {
        GLint v[4];glGetIntegerv(name,v);s.ints.insert(s.ints.end(),v,v+4);
    }
    GLint polygon[2];glGetIntegerv(GL_POLYGON_MODE,polygon);s.ints.insert(s.ints.end(),polygon,polygon+2);
    for(auto cap:caps)s.ints.push_back(glIsEnabled(cap));
    for(int i=0;i<8;++i)s.ints.push_back(glIsEnabled(GL_CLIP_DISTANCE0+GLenum(i)));
    GLint active;glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
    for(GLenum unit:{GL_TEXTURE0,GL_TEXTURE1,GL_TEXTURE2,GL_TEXTURE3}) {
        glActiveTexture(unit);GLint v;glGetIntegerv(GL_TEXTURE_BINDING_2D,&v);s.ints.push_back(v);
        glGetIntegeri_v(GL_SAMPLER_BINDING,unit-GL_TEXTURE0,&v);s.ints.push_back(v);
    }
    glActiveTexture(GLenum(active));GLfloat color[4];glGetFloatv(GL_COLOR_CLEAR_VALUE,color);
    s.floats.insert(s.floats.end(),color,color+4);return s;
}
void Clean() {
    glUseProgram(0);glBindVertexArray(0);glBindFramebuffer(GL_FRAMEBUFFER,0);
    for(auto cap:caps)glDisable(cap);
    for(int i=0;i<8;++i)glDisable(GL_CLIP_DISTANCE0+GLenum(i));
    glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    glBindBuffer(GL_PIXEL_PACK_BUFFER,0);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
    for(auto name:stores)glPixelStorei(name,(name==GL_PACK_ALIGNMENT || name==GL_UNPACK_ALIGNMENT)?4:0);
    for(int i=0;i<4;++i){glActiveTexture(GL_TEXTURE0+GLenum(i));glBindTexture(GL_TEXTURE_2D,0);glBindSampler(GLuint(i),0);}
    glActiveTexture(GL_TEXTURE0);assert(glGetError()==GL_NO_ERROR);
}
GLuint Program() {
    const char* vs="#version 330 core\nvoid main(){gl_Position=vec4(0,0,0,1);}";
    const char* fs="#version 330 core\nout vec4 color;void main(){color=vec4(1);}";
    GLuint shaders[2]={glCreateShader(GL_VERTEX_SHADER),glCreateShader(GL_FRAGMENT_SHADER)};
    const char* text[2]={vs,fs};GLuint p=glCreateProgram();
    for(int i=0;i<2;++i){glShaderSource(shaders[i],1,&text[i],nullptr);glCompileShader(shaders[i]);glAttachShader(p,shaders[i]);}
    glLinkProgram(p);GLint linked=0;glGetProgramiv(p,GL_LINK_STATUS,&linked);assert(linked);
    for(auto shader:shaders)glDeleteShader(shader);
    return p;
}
struct Caller {
    GLuint textures[4]{},buffers[2]{},fbos[2]{},vao=0,sampler=0,program=0;
    Caller() {
        Clean();program=Program();glGenVertexArrays(1,&vao);glGenTextures(4,textures);
        for(auto t:textures){glBindTexture(GL_TEXTURE_2D,t);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,13,11,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);}
        glGenFramebuffers(2,fbos);
        for(int i=0;i<2;++i){glBindFramebuffer(GL_FRAMEBUFFER,fbos[i]);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,textures[i],0);assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);}
        glGenBuffers(2,buffers);
        for(auto b:buffers){glBindBuffer(GL_PIXEL_PACK_BUFFER,b);glBufferData(GL_PIXEL_PACK_BUFFER,256,nullptr,GL_STREAM_READ);}
        glGenSamplers(1,&sampler);glSamplerParameteri(sampler,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
        Clean();
    }
    void Hostile() const {
        Clean();glUseProgram(program);glBindVertexArray(vao);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,fbos[0]);glBindFramebuffer(GL_READ_FRAMEBUFFER,fbos[1]);
        glViewport(7,9,31,27);glScissor(2,3,5,7);glClearColor(.25f,.5f,.75f,.125f);
        glColorMask(GL_FALSE,GL_TRUE,GL_FALSE,GL_TRUE);glPolygonMode(GL_FRONT_AND_BACK,GL_LINE);
        for(auto cap:caps)glEnable(cap);
        for(int i=0;i<8;i+=2)glEnable(GL_CLIP_DISTANCE0+GLenum(i));
        glCullFace(GL_FRONT_AND_BACK);glBlendFunc(GL_ZERO,GL_ZERO);glLogicOp(GL_XOR);
        glSampleCoverage(0,GL_FALSE);glSampleMaski(0,0);
        for(int i=0;i<4;++i){glActiveTexture(GL_TEXTURE0+GLenum(i));glBindTexture(GL_TEXTURE_2D,textures[i]);glBindSampler(GLuint(i),sampler);}
        glActiveTexture(GL_TEXTURE3);glBindBuffer(GL_PIXEL_PACK_BUFFER,buffers[0]);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,buffers[1]);
        for(auto name:stores)glPixelStorei(name,(name==GL_PACK_ALIGNMENT || name==GL_UNPACK_ALIGNMENT)?8:
            (name==GL_PACK_ROW_LENGTH || name==GL_UNPACK_ROW_LENGTH)?41:
            (name==GL_PACK_SWAP_BYTES || name==GL_UNPACK_SWAP_BYTES || name==GL_PACK_LSB_FIRST || name==GL_UNPACK_LSB_FIRST)?1:2);
        assert(glGetError()==GL_NO_ERROR);
    }
    ~Caller() {
        Clean();glDeleteProgram(program);glDeleteVertexArrays(1,&vao);glDeleteSamplers(1,&sampler);
        glDeleteFramebuffers(2,fbos);glDeleteBuffers(2,buffers);glDeleteTextures(4,textures);
    }
};
void Same(const State& expected) {assert(Snapshot()==expected);assert(glGetError()==GL_NO_ERROR);}
std::vector<uint32_t> ReadRgb() {
    Clean();std::vector<uint32_t> raw(W*H),rgb(raw.size());glReadPixels(0,0,W,H,GL_RGBA,GL_UNSIGNED_BYTE,raw.data());
    assert(glGetError()==GL_NO_ERROR);
    for(int y=0;y<H;++y)for(int x=0;x<W;++x){auto p=raw[size_t(H-1-y)*W+x];rgb[size_t(y)*W+x]=((p&255)<<16)|(p&0xff00)|((p>>16)&255);}
    return rgb;
}
void CheckImage(const ui::Surface& s,const std::vector<uint32_t>& expected) {
    assert(s.width==W && s.height==H);
    for(int y=0;y<H;++y)for(int x=0;x<W;++x)assert(s.Get(x,y)==expected[size_t(y)*W+x]);
}
struct ResidentSink:ui::ResidentFrameConsumer {
    ui::OpenGlPresentation& presenter;const ui::PresentationFrame& frame;
    uint32_t tokens[2]={11,22};bool gpu=true;
    ResidentSink(ui::OpenGlPresentation& p,const ui::PresentationFrame& f):presenter(p),frame(f) {}
    bool Consume(const ui::ResidentFrameView& view) override {
        assert(view.count==2);gpu=view.images[0].texture!=0;
        if(gpu)return presenter.RenderResident(frame,{nullptr,W,H,0,0},view,tokens,2);
        // Fallback supplies real recovered pixels, never a stale canonical copy.
        std::array<ui::PresentationLayerFrame,3> layers;
        std::copy(frame.scene->layers,frame.scene->layers+3,layers.begin());
        for(size_t i=0;i<2;++i){assert(view.images[i].cpu);layers[i].surface=view.images[i].cpu;}
        auto scene=*frame.scene;scene.layers=layers.data();auto recovered=frame;recovered.scene=&scene;
        return presenter.Render(recovered,{nullptr,W,H,0,0});
    }
};
void TestResident() {
    Clean();ui::GpuGdiTargets targets;assert(targets.Initialize());
    ui::OpenGlPresentation presenter;assert(presenter.Initialize());
    const auto a=targets.Create(23,17),b=targets.Create(19,13);assert(a && b);
    assert(targets.Fill(a,{0,0,23,17},0x1279ad) && targets.Fill(b,{0,0,19,13},0xa94317));
    ui::GpuGdiTargets::Layer input[]={{a,{0,0,23,17}},{b,{0,0,19,13}}};
    ui::GpuGdiTargets::Frame resident;assert(targets.PrepareFrame(input,2,W,H,resident));
    ui::OwnedSurface base,overlay,refA,refB,canonical;base.Allocate(W,H);overlay.Allocate(W,H);canonical.Allocate(W,H);
    refA.Allocate(23,17);refB.Allocate(19,13);
    for(int y=0;y<H;++y)for(int x=0;x<W;++x) {
        base.s.Set(x,y,(x*17311+y*387)&0xffffff);overlay.s.Set(x,y,(x*831+y*77413)&0xffffff);
    }
    for(int y=0;y<17;++y)for(int x=0;x<23;++x)refA.s.Set(x,y,0x1279ad);
    for(int y=0;y<13;++y)for(int x=0;x<19;++x)refB.s.Set(x,y,0xa94317);
    std::vector<uint32_t> owners(W*H),expected(W*H);
    ui::PresentationBuffer<uint64_t> versions;assert(versions.Resize(6));std::fill(versions.data(),versions.data()+6,1);
    ui::PresentationLayerFrame layers[]={{nullptr,11,-3,7,1,1,nullptr},{nullptr,22,9,12,1,1,nullptr},{&overlay.s,33,0,0,1,1,&versions}};
    ui::PresentationSceneFrame scene{owners.data(),W,H,1,1,&versions,layers,3};
    ui::PresentationFrame frame{base.s,{3,2,W-6,H-4},{5,9,true},1,&base,&versions,777,1,&scene};
    ResidentSink sink(presenter,frame);
    ui::SoftwarePresentation software;
    for(int iteration=0;iteration<12;++iteration) {
        if(iteration==6) {
            assert(targets.Fill(a,{2,3,9,8},0x37bc91));
            for(int y=3;y<8;++y)for(int x=2;x<9;++x)refA.s.Set(x,y,0x37bc91);
        }
        std::fill(owners.begin(),owners.end(),0);
        layers[0].left=-3+iteration;layers[1].top=12+iteration;
        for(int y=0;y<H;++y)for(int x=0;x<W;++x) {
            auto& token=owners[size_t(y)*W+x];
            if(x>=layers[0].left && x<layers[0].left+23 && y>=7 && y<24)token=11;
            if(x>=9 && x<28 && y>=layers[1].top && y<layers[1].top+13)token=22;
            if(y>=H-9 || (x>=17 && x<34 && y>=11 && y<29))token=33; // native chrome / overlapping panel
            if((x+y)%19==0)token=0; // shaped holes must select base
        }
        ++scene.generation;frame.cursor.x=5+iteration;
        // Independent CPU ownership oracle; SoftwarePresentation then handles
        // viewport scaling/letterbox/cursor on this complete reference image.
        for(int y=0;y<H;++y)for(int x=0;x<W;++x) {
            const auto token=owners[size_t(y)*W+x];uint32_t rgb=base.s.Get(x,y);
            if(token==11)rgb=refA.s.Get(x-layers[0].left,y-7);
            if(token==22)rgb=refB.s.Get(x-9,y-layers[1].top);
            if(token==33)rgb=overlay.s.Get(x,y);
            canonical.s.Set(x,y,rgb);
        }
        ui::PresentationFrame referenceFrame{canonical.s,frame.viewport,frame.cursor};
        assert(software.Render(referenceFrame,{expected.data(),W,H,W,expected.size()}));
        assert(targets.ConsumeFrame(resident,sink) && sink.gpu);
        const auto actual=ReadRgb();
        for(size_t i=0;i<actual.size();++i) {
            const auto p=expected[i];const auto rgb=((p&255)<<16)|(p&0xff00)|((p>>16)&255);
            if(actual[i]!=rgb){std::fprintf(stderr,"resident frame=%d xy=%zu,%zu actual=%06x expected=%06x\n",iteration,i%W,i/W,actual[i],rgb);assert(false);}
        }
        assert(targets.TargetStats(a)->readbacks==0 && targets.TargetStats(b)->readbacks==0);
        assert(targets.TargetStats(a)->uploads==0 && targets.TargetStats(b)->uploads==0);
        assert(presenter.Timings().layerUploadBytes==uint64_t(W)*H*4); // CPU overlay only, once
    }
    // Invalid native token mapping leaves the previous output untouched and GPU active.
    assert(presenter.Timings().screenUploadBytes==uint64_t(W)*H*4);
    const auto before=ReadRgb();sink.tokens[1]=999;
    assert(!targets.ConsumeFrame(resident,sink) && ReadRgb()==before);
    sink.tokens[1]=11;assert(!targets.ConsumeFrame(resident,sink));
    assert(targets.GpuActive() && ReadRgb()==before);sink.tokens[1]=22;
    // Same resource frame survives retirement; the renderer never owns/deletes the borrowed names.
    assert(targets.Destroy(a) && targets.Destroy(b));assert(targets.ConsumeFrame(resident,sink));
    presenter.Shutdown();assert(targets.Present(resident)); // borrowed textures survive presenter teardown
    assert(presenter.Initialize());
    // A renderer-only GL error rejects the partial output. The native caller
    // explicitly switches all resources to CPU recovery after Consume returns.
    glEnable(0xdead);assert(!targets.ConsumeFrame(resident,sink));
    targets.RecoverCpu();assert(!targets.GpuActive());
    assert(targets.ConsumeFrame(resident,sink) && !sink.gpu);
    const auto fallback=ReadRgb();
    for(size_t i=0;i<fallback.size();++i) {
        const auto p=expected[i];assert(fallback[i]==(((p&255)<<16)|(p&0xff00)|((p>>16)&255)));
    }
    // Recovered surfaces have no revision maps: they must be uploaded honestly.
    assert(presenter.Timings().layerUploadBytes>=uint64_t(23*17+19*13)*4);
    presenter.Shutdown();targets.Shutdown();assert(!targets.ConsumeFrame(resident,sink));
    puts("Resident production layers: PASS (GPU textures, ownership holes/overlap, CPU chrome, cursor, no target readback/upload, retirement and recovery)");
}
void TestShared() {
    Caller caller;caller.Hostile();const auto hostile=Snapshot();
    std::vector<uint32_t> pixels(W*H);
    for(int y=0;y<H;++y)for(int x=0;x<W;++x)pixels[size_t(y)*W+x]=(x*91771u+y*37997u)&0xffffff;
    {
        ui::GpuGdiPrototype image;assert(image.Initialize(W,H));Same(hostile);
        assert(image.Copy({0,0,W,H},pixels.data(),pixels.size()));Same(hostile);
        for(int i=0;i<8;++i)assert(image.Fill({i*4,0,i*4+4,8},0x123400u+unsigned(i)));
        auto expected=pixels;
        for(int y=0;y<8;++y)for(int x=0;x<32;++x)expected[size_t(y)*W+x]=0x123400u+unsigned(x/4);
        assert(image.FlushGpu());Same(hostile);assert(image.Counters().batches==1);
        auto* cpu=image.CpuRead();Same(hostile);assert(cpu);CheckImage(*cpu,expected);
        // Capacity checkpoint inside Copy/Fill must preserve caller state too.
        for(int i=0;i<65;++i)assert(image.Fill({0,0,1,1},unsigned(i)));
        Same(hostile);expected[0]=64;CheckImage(*image.CpuRead(),expected);Same(hostile);
        image.Shutdown();Same(hostile);
        glEnable(0xdead);assert(!image.Initialize(W,H));Same(hostile);
    }
    {
        ui::GpuGdiTargets targets;assert(targets.Initialize());Same(hostile);
        const auto h=targets.Create(W,H);assert(h);Same(hostile);
        assert(targets.Copy(h,{0,0,W,H},pixels.data(),pixels.size()));
        ui::GpuGdiTargets::Layer layer{h,{0,0,W,H}};
        assert(targets.Present(&layer,1,W,H));Same(hostile);assert(ReadRgb()==pixels);
        // Use the real production backend between GDI submissions, retaining
        // its texture/sampler units and pixel-store state across every handoff.
        Clean();ui::OpenGlPresentation presenter;assert(presenter.Initialize());
        ui::OwnedSurface source;source.Allocate(W,H);
        for(int y=0;y<H;++y)for(int x=0;x<W;++x)source.s.Set(x,y,(x*12345u+y*91873u)&0xffffff);
        std::vector<uint32_t> expected(W*H);
        for(int y=0;y<H;++y)for(int x=0;x<W;++x)expected[size_t(y)*W+x]=source.s.Get(x,y);
        for(int frame=0;frame<12;++frame) {
            assert(presenter.Render({source.s,{0,0,W,H},{},uint64_t(frame+1)},{nullptr,W,H,0,0}));
            const auto production=Snapshot();const auto reads=targets.TargetStats(h)->readbacks;
            assert(targets.Present(&layer,1,W,H));Same(production);
            assert(targets.TargetStats(h)->readbacks==reads);assert(ReadRgb()==pixels);
            assert(presenter.Render({source.s,{0,0,W,H},{},uint64_t(frame+1)},{nullptr,W,H,0,0}));
            assert(ReadRgb()==expected);
        }
        presenter.Shutdown();caller.Hostile();
        const auto other=targets.Create(W,H);assert(other);Same(hostile);
        assert(targets.CopyTargetViaCpu(other,{0,0,W,H},h,{0,0,W,H}));Same(hostile);
        assert(targets.CpuRead(other));Same(hostile);
        assert(targets.Destroy(other));Same(hostile);
        auto invalid=layer;invalid.target=0;assert(!targets.Present(&invalid,1,W,H));Same(hostile);
        assert(targets.Fill(h,{0,0,1,1},0xabc123));pixels[0]=0xabc123;
        glEnable(0xdead);assert(!targets.Present(&layer,1,W,H));Same(hostile);
        assert(!targets.GpuActive());auto* cpu=targets.CpuRead(h);assert(cpu);CheckImage(*cpu,pixels);Same(hostile);
        assert(targets.Fill(h,{1,0,2,1},0xdef456));pixels[1]=0xdef456;
        assert(targets.ComposeCpu(&layer,1,W,H));Same(hostile);
        assert(targets.Destroy(h));Same(hostile);targets.Shutdown();Same(hostile);
    }
}
} // namespace
int main() {
    auto d=eglGetDisplay(EGL_DEFAULT_DISPLAY);assert(d!=EGL_NO_DISPLAY && eglInitialize(d,nullptr,nullptr) && eglBindAPI(EGL_OPENGL_API));
    EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_BIT,
        EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
    EGLConfig config;EGLint n;assert(eglChooseConfig(d,attrs,&config,1,&n) && n==1);
    EGLint size[]={EGL_WIDTH,W,EGL_HEIGHT,H,EGL_NONE};auto surface=eglCreatePbufferSurface(d,config,size);assert(surface!=EGL_NO_SURFACE);
    EGLint version[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,3,EGL_CONTEXT_OPENGL_PROFILE_MASK,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,EGL_NONE};
    auto context=eglCreateContext(d,config,EGL_NO_CONTEXT,version);assert(context!=EGL_NO_CONTEXT);
    assert(eglMakeCurrent(d,surface,surface,context));TestShared();TestResident();
    assert(eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT));
    assert(eglDestroyContext(d,context) && eglDestroySurface(d,surface) && eglTerminate(d));
    std::puts("GPU GDI shared context: PASS (hostile state, nested barriers, production interleave, failure cleanup; no presentation readbacks)");
}
