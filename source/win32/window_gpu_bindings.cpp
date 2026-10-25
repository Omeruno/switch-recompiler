#include "window_gpu_bindings.h"
#include "user32.h"
#include <limits>

namespace win32::user {
namespace {
bool SameRect(const ui::Rect& a,const ui::Rect& b) {
    return a.left==b.left && a.top==b.top && a.right==b.right && a.bottom==b.bottom;
}
bool Same(const ui::WindowRasterTarget& a,const ui::WindowRasterTarget& b) {
    if(a.windowIdentity!=b.windowIdentity || a.dcIdentity!=b.dcIdentity ||
       a.layoutGeneration!=b.layoutGeneration || a.hwnd!=b.hwnd || a.hdc!=b.hdc ||
       !SameRect(a.frame,b.frame) || a.originX!=b.originX || a.originY!=b.originY ||
       a.windowOrg.x!=b.windowOrg.x || a.windowOrg.y!=b.windowOrg.y ||
       a.viewportOrg.x!=b.viewportOrg.x || a.viewportOrg.y!=b.viewportOrg.y ||
       a.wholeWindow!=b.wholeWindow || a.clip.Rects().size()!=b.clip.Rects().size())return false;
    for(size_t i=0;i<a.clip.Rects().size();++i)if(!SameRect(a.clip.Rects()[i],b.clip.Rects()[i]))return false;
    return true;
}
bool Size(const ui::Rect& r,int& w,int& h) {
    const int64_t x=int64_t(r.right)-r.left,y=int64_t(r.bottom)-r.top;
    if(x<=0 || y<=0 || x>ui::GpuGdiPrototype::MaxSide || y>ui::GpuGdiPrototype::MaxSide)return false;
    w=int(x);h=int(y);return true;
}
}
bool WindowGpuBindings::Initialize(){Shutdown();return targets_.Initialize();}
void WindowGpuBindings::Shutdown(){slots_={};targets_.Shutdown();}
void WindowGpuBindings::Prune() {
    for(auto& s:slots_)if(s.target) {
        auto* w=Win(s.hwnd);int width=0,height=0;
        if(!w || w->rasterIdentity!=s.identity || !Size(ScreenRect(w),width,height) ||
           width!=s.width || height!=s.height) {
            targets_.Destroy(s.target);s={};
        }
    }
}
bool WindowGpuBindings::Acquire(uint32_t hdc,Lease& out) {
    out={};Prune();
    ui::WindowRasterTarget r;int width=0,height=0;
    if(!targets_.GpuActive() || !ui::g_gdi.DescribeWindowTarget(hdc,r) || r.hwnd==S().desktop ||
       !Size(r.frame,width,height) || r.clip.Empty() || r.clip.Rects().size()>MaxClipRects)return false;
    for(const auto& s:slots_)if(s.identity==r.windowIdentity && s.target){out={std::move(r),s.target};return true;}
    for(auto& s:slots_)if(!s.target) {
        const auto target=targets_.Create(width,height);if(!target)return false;
        s={r.hwnd,r.windowIdentity,width,height,target};out={std::move(r),target};return true;
    }
    return false;
}
bool WindowGpuBindings::Valid(const Lease& lease) {
    if(!lease.target || lease.raster.clip.Rects().size()>MaxClipRects)return false;
    bool found=false;
    for(const auto& s:slots_)if(s.target==lease.target && s.identity==lease.raster.windowIdentity){found=true;break;}
    if(!found)return false;
    ui::WindowRasterTarget now;
    return ui::g_gdi.DescribeWindowTarget(lease.raster.hdc,now) && Same(now,lease.raster);
}
bool WindowGpuBindings::Fill(const Lease& lease,ui::Rect logical,uint32_t rgb) {
    if(!Valid(lease))return false;
    if(logical.Empty())return true;
    const auto& r=lease.raster;
    const int64_t dx=int64_t(r.originX)+r.viewportOrg.x-r.windowOrg.x;
    const int64_t dy=int64_t(r.originY)+r.viewportOrg.y-r.windowOrg.y;
    const int64_t x0=int64_t(logical.left)+dx,x1=int64_t(logical.right)+dx;
    const int64_t y0=int64_t(logical.top)+dy,y1=int64_t(logical.bottom)+dy;
    constexpr auto low=std::numeric_limits<int>::min(),high=std::numeric_limits<int>::max();
    if(x0<low || x0>high || x1<low || x1>high || y0<low || y0>high || y1<low || y1>high)return false;
    const ui::Rect screen{int(x0),int(y0),int(x1),int(y1)};
    for(const auto& clip:r.clip.Rects()) {
        const auto part=screen.Intersect(clip).Intersect(r.frame);if(part.Empty())continue;
        const ui::Rect local{int(int64_t(part.left)-r.frame.left),int(int64_t(part.top)-r.frame.top),
            int(int64_t(part.right)-r.frame.left),int(int64_t(part.bottom)-r.frame.top)};
        if(!targets_.Fill(lease.target,local,rgb))return false;
    }
    return true;
}
const ui::Surface* WindowGpuBindings::CpuRead(const Lease& lease) {
    return Valid(lease)?targets_.CpuRead(lease.target):nullptr;
}
const ui::GpuGdiPrototype::Stats* WindowGpuBindings::Counters(const Lease& lease) {
    return Valid(lease)?targets_.TargetStats(lease.target):nullptr;
}
} // namespace win32::user
