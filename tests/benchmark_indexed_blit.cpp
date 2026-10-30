#include "source/ui/gdi.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>
using Clock=std::chrono::steady_clock;
struct Result{uint64_t ns=0,hash=0;};
static Result Run(bool fast,int width,int height,bool bottomUp){
    ui::OwnedSurface src,dst;src.Allocate(width,height,8);dst.Allocate(width,height,32);
    src.s.palette.resize(256);for(unsigned n=0;n<256;++n)src.s.palette[n]=(n*749339u)&0xffffff;
    for(int y=0;y<height;++y)for(int x=0;x<width;++x)src.s.Row(y)[x]=uint8_t(x*17+y*31);
    if(bottomUp){src.s.bits=src.s.Row(height-1);src.s.pitch=-src.s.pitch;}
    ui::Gdi g;g.EnableBlitFastPath(fast);g.SetScreen(&dst.s);const auto dc=g.CreateDc(ui::DcKind::Screen);assert(dc);
    Result result;const auto start=Clock::now();
    for(unsigned n=0;n<48;++n){src.s.palette[3]=(n*719231u)&0xffffff;assert(g.BlitFromSurface(dc,0,0,width,height,src.s,0,0,width,height,0xcc0020));}
    result.ns=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count());
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){
        const auto index=uint8_t(x*17+(bottomUp?height-1-y:y)*31);
        const auto expected=index==3?(47*719231u)&0xffffff:(index*749339u)&0xffffff;
        assert(dst.s.Get(x,y)==expected);result.hash=(result.hash^expected)*1099511628211ull;
    }
    return result;
}
int main(){
    for(const auto size:{ui::Point{32,24},ui::Point{640,480}})for(bool bottomUp:{false,true}){
        std::vector<uint64_t> reference,fast;uint64_t hash=0;
        for(unsigned n=0;n<7;++n){Result a,b;if(n&1){b=Run(true,size.x,size.y,bottomUp);a=Run(false,size.x,size.y,bottomUp);}else{a=Run(false,size.x,size.y,bottomUp);b=Run(true,size.x,size.y,bottomUp);}
            assert(a.hash==b.hash);hash=a.hash;reference.push_back(a.ns);fast.push_back(b.ns);}
        std::sort(reference.begin(),reference.end());std::sort(fast.begin(),fast.end());
        printf("Indexed blit measurement %dx%d bottom_up=%u samples=7 calls=48 reference_ns=%llu fast_ns=%llu hash=%016llx\n",size.x,size.y,unsigned(bottomUp),(unsigned long long)reference[3],(unsigned long long)fast[3],(unsigned long long)hash);
    }
    puts("Indexed blit measurement PASS (independent changing-palette pixels,paired median wall observations;no hardware FPS claim or timing threshold)");
}
