#define GL_GLEXT_PROTOTYPES 1
#include <EGL/egl.h>
#include <GL/glcorearb.h>
#include "source/ui/gpu_gdi_prototype.h"
#include "source/ui/performance_metrics.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
int main(int argc,char** argv) {
    assert(argc==3);const std::string mode=argv[1],work=argv[2];
    assert((mode=="reference" || mode=="batched") && (work=="tiles" || work=="overlap" || work=="mixed"));
    auto d=eglGetDisplay(EGL_DEFAULT_DISPLAY);assert(d!=EGL_NO_DISPLAY && eglInitialize(d,nullptr,nullptr) && eglBindAPI(EGL_OPENGL_API));
    const EGLint cfg[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
    EGLConfig config;EGLint count;assert(eglChooseConfig(d,cfg,&config,1,&count) && count==1);
    const EGLint pbuf[]={EGL_WIDTH,8,EGL_HEIGHT,8,EGL_NONE};auto surface=eglCreatePbufferSurface(d,config,pbuf);assert(surface!=EGL_NO_SURFACE);
    const EGLint ctx[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,3,EGL_CONTEXT_OPENGL_PROFILE_MASK,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,EGL_NONE};
    auto context=eglCreateContext(d,config,EGL_NO_CONTEXT,ctx);assert(context!=EGL_NO_CONTEXT && eglMakeCurrent(d,surface,surface,context));
    uint64_t enqueueNs=0,submitNs=0,readbackNs=0,hash=14695981039346656037ull;
    ui::GpuGdiPrototype::Stats stats;
    {
        ui::GpuGdiPrototype gpu;assert(gpu.Initialize(128,128));gpu.EnableBatching(mode=="batched");
        std::vector<uint32_t> expected(128*128),input(16*16);
        ui::GpuGdiPrototype::Stats warmup;
        for(int iteration=-4;iteration<32;++iteration) {
            const unsigned frame=iteration<0?unsigned(iteration+4):unsigned(iteration);
            if(iteration==0)warmup=gpu.Counters();
            std::array<ui::Rect,64> rects;std::array<uint32_t,64> colors;
            for(unsigned i=0;i<64;++i) {
                rects[i]=work=="overlap"?ui::Rect{0,0,128,128}:ui::Rect{int(i%8)*16,int(i/8)*16,int(i%8+1)*16,int(i/8+1)*16};
                colors[i]=(frame*77591+i*19271)&0xffffff;
                if(work=="mixed" && i==32)for(size_t n=0;n<input.size();++n)input[n]=0xa7000000|((frame*727+n*7187)&0xffffff);
                const auto& r=rects[i];
                for(int y=r.top;y<r.bottom;++y)for(int x=r.left;x<r.right;++x)
                    expected[size_t(y)*128+x]=(work=="mixed" && i==32)?input[size_t(y-r.top)*16+x-r.left]&0xffffff:colors[i];
            }
            auto start=ui::PerformanceNowNs();
            for(unsigned i=0;i<64;++i) {
                if(work=="mixed" && i==32){assert(gpu.Copy(rects[i],input.data(),input.size()));std::fill(input.begin(),input.end(),0xff00ff);}
                else assert(gpu.Fill(rects[i],colors[i]));
            }
            const auto enqueue=ui::PerformanceNowNs()-start;start=ui::PerformanceNowNs();assert(gpu.FlushGpu());const auto submit=ui::PerformanceNowNs()-start;
            start=ui::PerformanceNowNs();const auto* actual=gpu.CpuRead();const auto readback=ui::PerformanceNowNs()-start;
            if(iteration>=0){enqueueNs+=enqueue;submitNs+=submit;readbackNs+=readback;}
            assert(actual && gpu.Backend()==ui::GpuGdiPrototype::Mode::Gpu);
            for(int y=0;y<128;++y)for(int x=0;x<128;++x) {
                const auto pixel=actual->Get(x,y);assert(pixel==expected[size_t(y)*128+x]);if(iteration>=0){hash^=pixel;hash*=1099511628211ull;}
            }
        }
        stats=gpu.Counters();
#define DELTA(field) stats.field-=warmup.field
        DELTA(draws);DELTA(uploads);DELTA(readbacks);DELTA(recoveries);DELTA(checkpoints);
        DELTA(submittedCommands);DELTA(batches);DELTA(batchedCommands);
#undef DELTA
        assert(stats.readbacks==32 && stats.submittedCommands==2048 && !stats.recoveries);
    }
    assert(eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT) && eglDestroyContext(d,context) && eglDestroySurface(d,surface) && eglTerminate(d));
    std::printf("{\"status\":\"PASS\",\"mode\":\"%s\",\"workload\":\"%s\",\"frames\":32,\"hash\":\"%016llx\","
        "\"enqueue_ns\":%llu,\"submit_ns\":%llu,\"readback_ns\":%llu,\"draws\":%llu,\"commands\":%llu,\"batches\":%llu,\"batched_commands\":%llu,\"uploads\":%llu,\"readbacks\":%llu}\n",
        mode.c_str(),work.c_str(),(unsigned long long)hash,(unsigned long long)enqueueNs,(unsigned long long)submitNs,(unsigned long long)readbackNs,
        (unsigned long long)stats.draws,(unsigned long long)stats.submittedCommands,(unsigned long long)stats.batches,
        (unsigned long long)stats.batchedCommands,(unsigned long long)stats.uploads,(unsigned long long)stats.readbacks);
}
