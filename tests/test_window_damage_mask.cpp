#include "source/ui/gdi.h"
#include <cassert>
#include <cstdio>
#include <vector>
namespace {
ui::SurfaceWriteCapture capture;
bool failCapture=false;unsigned captures=0;
const ui::Rect frame{3,4,14,13};
bool Provider(uint32_t hwnd,bool,ui::WindowDcInfo& out) {
    if(hwnd!=1 || (out.expectedWindowIdentity && out.expectedWindowIdentity!=42))return false;
    out.windowIdentity=42;out.frame=frame;out.originX=frame.left;out.originY=frame.top;
    out.visible=ui::Region(frame);out.visible.SubtractRect({7,7,10,10});
    if(out.captureWrites){++captures;if(failCapture)return false;out.capture=&capture;}
    return true;
}
}
int main() {
    ui::Gdi g;ui::OwnedSurface screen,image;screen.Allocate(32,24);image.Allocate(11,9);
    std::vector<uint32_t> owners(32*24,55),retained(11*9,0x112233);
    for(int y=0;y<24;++y)for(int x=0;x<32;++x)screen.s.Set(x,y,0x112233);
    for(int y=0;y<9;++y)for(int x=0;x<11;++x)image.s.Set(x,y,(unsigned(y*11+x)*0x13579u)&0xffffff);
    capture.owners=owners.data();capture.screenWidth=32;capture.screenHeight=24;capture.token=77;
    capture.pixels=retained.data();capture.left=3;capture.top=4;capture.width=11;capture.height=9;
    assert(g.SetScreen(&screen.s) && g.SetWindowDcProvider(Provider));
    const auto dc=g.CreateDc(ui::DcKind::Window,1);ui::WindowRasterTarget stamp;assert(g.DescribeWindowTarget(dc,stamp));
    std::vector<uint8_t> mask((11*9+7)/8,0);const std::vector<size_t> dirty{0,10+8*11,3+2*11,7+5*11};
    for(auto n:dirty)mask[n/8]|=uint8_t(1u<<(n%8));
    const auto before=g.ScreenChangeCounter();size_t pixels=999;
    auto unchanged=[&](){assert(g.ScreenChangeCounter()==before && pixels==0 && captures==0);for(auto o:owners)assert(o==55);for(int y=0;y<24;++y)for(int x=0;x<32;++x)assert(screen.s.Get(x,y)==0x112233);};
    assert(!g.CommitWindowMask(stamp,image.s,nullptr,mask.size(),pixels));unchanged();
    assert(!g.CommitWindowMask(stamp,image.s,mask.data(),mask.size()-1,pixels));unchanged();
    auto badImage=image.s;badImage.pitch+=1;assert(!g.CommitWindowMask(stamp,badImage,mask.data(),mask.size(),pixels));unchanged();
    badImage=image.s;badImage.bits+=1;assert(!g.CommitWindowMask(stamp,badImage,mask.data(),mask.size(),pixels));unchanged();
    auto malformed=mask;malformed.back()|=0x80;assert(!g.CommitWindowMask(stamp,image.s,malformed.data(),malformed.size(),pixels));unchanged();
    malformed=mask;const size_t hole=4+3*11;malformed[hole/8]|=uint8_t(1u<<(hole%8));
    assert(!g.CommitWindowMask(stamp,image.s,malformed.data(),malformed.size(),pixels));unchanged();
    auto wrong=stamp;wrong.windowIdentity=43;assert(!g.CommitWindowMask(wrong,image.s,mask.data(),mask.size(),pixels));unchanged();
    failCapture=true;assert(!g.CommitWindowMask(stamp,image.s,mask.data(),mask.size(),pixels));assert(captures==1 && !pixels && g.ScreenChangeCounter()==before);captures=0;failCapture=false;unchanged();
    assert(g.CommitWindowMask(stamp,image.s,mask.data(),mask.size(),pixels));assert(pixels==dirty.size() && captures==1 && g.ScreenChangeCounter()==before+1);
    for(int y=0;y<24;++y)for(int x=0;x<32;++x) {
        bool changed=false;for(auto n:dirty)if(x==int(n%11)+3 && y==int(n/11)+4)changed=true;
        assert(screen.s.Get(x,y)==(changed?image.s.Get(x-3,y-4):0x112233));
        assert(owners[size_t(y)*32+x]==(changed?77u:55u));
    }
    for(size_t n=0;n<retained.size();++n) {
        bool changed=false;for(auto d:dirty)if(n==d)changed=true;
        assert(retained[n]==(changed?image.s.Get(int(n%11),int(n/11)):0x112233));
    }
    g.InvalidateLayout();const auto revision=g.ScreenChangeCounter();assert(!g.CommitWindowMask(stamp,image.s,mask.data(),mask.size(),pixels) && !pixels && g.ScreenChangeCounter()==revision);
    puts("Exact window damage mask PASS (padding/visibility/identity/layout/late failure atomic reject; only dirty pixels and capture owners change)");
}
