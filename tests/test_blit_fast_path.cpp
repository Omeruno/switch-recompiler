#include "source/ui/gdi.h"
#include <cassert>
#include <cstdio>
#include <cstring>

// Compare complete storage (including X bytes/padding/guards), capture ownership
// and revisions with the retained general rasterizer, not another fast copy.
static std::vector<uint8_t> Draw(bool fast,unsigned scenario) {
    constexpr int width=32,height=24;
    const int format=scenario%8;
    const int srcBpp=format==1?24:format==2?8:32;
    const int dstBpp=format==3?16:format==4?24:32;
    ui::OwnedSurface source,dest;source.Allocate(width,height,srcBpp);dest.Allocate(width,height,dstBpp);
    source.storage.resize(source.storage.size()+32,0xa5);dest.storage.resize(dest.storage.size()+32,0x5a);
    source.s.bits=source.storage.data();dest.s.bits=dest.storage.data();
    if(format==5 || format==7){source.s.bits+=source.s.pitch*(height-1);source.s.pitch=-source.s.pitch;}
    if(format==6 || format==7){dest.s.bits+=dest.s.pitch*(height-1);dest.s.pitch=-dest.s.pitch;}
    for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
        source.s.Set(x,y,(x*7219+y*3311+scenario*117)&0xffffff);
        dest.s.Set(x,y,(x*4127+y*9187+scenario*13)&0xffffff);
        if(srcBpp==32)reinterpret_cast<uint32_t*>(source.s.Row(y))[x]|=0xa7000000;
        if(dstBpp==32)reinterpret_cast<uint32_t*>(dest.s.Row(y))[x]|=0x9b000000;
    }
    std::vector<uint32_t> owners(width*height,3),pixels(width*height,0x114477);
    uint64_t pixelsRevision=0,ownerRevision=0,clock=8,epoch=1;
    ui::SurfaceWriteCapture capture;
    capture.owners=owners.data();capture.screenWidth=width;capture.screenHeight=height;
    capture.token=11;capture.pixels=pixels.data();capture.width=width;capture.height=height;
    capture.pixelVersions=&pixelsRevision;capture.pixelCols=1;capture.revision=7;
    capture.ownerVersions=&ownerRevision;capture.ownerCols=1;capture.ownerClock=&clock;capture.ownerEpoch=&epoch;
    if((scenario/8)%2 && dstBpp==32)dest.s.writeCapture=&capture;
    ui::Gdi gdi;gdi.EnableBlitFastPath(fast);gdi.SetScreen(&dest.s);
    const auto dst=gdi.CreateDc(ui::DcKind::Screen),src=gdi.CreateCompatibleDc(dst);
    const auto bitmap=gdi.CreateDibSection(source.s,0,{});assert(dst && src && bitmap);
    gdi.SelectObject(src,bitmap);
    const unsigned operation=(scenario/16)%10;
    auto* dc=gdi.Dc(dst);assert(dc);
    if(operation==1 || operation==9) {
        dc->st.hasClip=true;dc->st.clip=ui::Region({1,1,12,20});dc->st.clip.UnionRect({15,4,31,23});
    }
    if(operation==8) {dc->st.windowOrg={3,4};dc->st.viewportOrg={1,2};}
    int x=int((scenario*13)%13)-4,y=int((scenario*7)%9)-3;
    int sx=2,sy=3,w=19,h=15,sw=w,sh=h;
    if(operation==2){sx=-3;sy=-2;} // partial source -> general path
    if(operation==3){sw=11;sh=9;} // scaling -> general path
    if(operation==4){sw=-w;sx=25;}
    if(operation==5){sh=-h;sy=22;}
    bool ok;
    if(operation==6)ok=gdi.TransparentBlt(dst,x,y,w,h,src,sx,sy,sw,sh,0x114477);
    else {
        const auto input=operation==7?dst:src; // scrolling uses existing source snapshot
        const uint32_t rop=operation==9?0x660046:0xcc0020;
        ok=gdi.StretchBlt(dst,x,y,w,h,input,sx,sy,sw,sh,rop);
    }
    assert(ok);
    // Same pixels copied again must preserve exact capture revision/no-op rules.
    assert(gdi.BitBlt(dst,4,5,9,7,src,2,3,0xcc0020));
    assert(gdi.BitBlt(dst,4,5,9,7,src,2,3,0xcc0020));
    std::vector<uint8_t> result=dest.storage;
    auto append=[&](const void* p,size_t n) {const auto* b=static_cast<const uint8_t*>(p);result.insert(result.end(),b,b+n);};
    append(source.storage.data(),source.storage.size());append(owners.data(),owners.size()*4);append(pixels.data(),pixels.size()*4);
    const uint64_t metadata[]={pixelsRevision,ownerRevision,clock,epoch,gdi.ScreenChangeCounter()};append(metadata,sizeof(metadata));
    return result;
}
int main() {
    for(unsigned i=0;i<1600;++i)assert(Draw(false,i)==Draw(true,i));
    puts("PASSED: 1600 reference/fast GDI copies, clips/formats/pitch/X bytes/overlap/ROP/key/capture/revisions/guards");
}
