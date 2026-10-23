#include "window_layers.h"
#include <algorithm>
#include <cstring>
#include <limits>

namespace ui {
void WindowLayers::Drop(Slot& slot) {
    if(!slot.key)return;
    const int width=screenCapture_.screenWidth,height=screenCapture_.screenHeight;
    const int x0=int(std::max<int64_t>(0,std::min<int64_t>(width,slot.left)));
    const int y0=int(std::max<int64_t>(0,std::min<int64_t>(height,slot.top)));
    const int x1=int(std::max<int64_t>(0,std::min<int64_t>(width,int64_t(slot.left)+slot.surface.width)));
    const int y1=int(std::max<int64_t>(0,std::min<int64_t>(height,int64_t(slot.top)+slot.surface.height)));
    const auto revision=screenCapture_.NextOwnerRevision();
    for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x)if(owners_[size_t(y)*width+x]==slot.token) {
        owners_[size_t(y)*width+x]=0;ownerVersions_[size_t(y/32)*screenCapture_.ownerCols+x/32]=revision;
    }
    pixelBytes_-=slot.pixels.size()*4;
    slot=Slot{};
}
void WindowLayers::Clear() {
    // Release pixels before the owner map, without scanning it once per slot.
    for(auto& slot:slots_)slot=Slot{};
    owners_.Clear();ownerVersions_.Clear();screenCapture_={};pixelBytes_=0;clock_=ownerClock_=0;nextToken_=1;++generation_;++ownerEpoch_;
}
bool WindowLayers::Enable(int width,int height) {
    Clear();
    if(width<=0 || height<=0 || width>8192 || height>8192 || uint64_t(width)*height*4>MaxPixelBytes)return false;
    if(!owners_.Resize(size_t(width)*height) || !ownerVersions_.Resize(size_t((width+31)/32)*((height+31)/32))){Clear();return false;}
    std::memset(owners_.data(),0,owners_.size()*4);
    std::memset(ownerVersions_.data(),0,ownerVersions_.size()*8);
    screenCapture_.owners=owners_.data();screenCapture_.screenWidth=width;screenCapture_.screenHeight=height;
    screenCapture_.ownerVersions=ownerVersions_.data();screenCapture_.ownerCols=(width+31)/32;
    screenCapture_.ownerClock=&ownerClock_;screenCapture_.ownerEpoch=&ownerEpoch_;
    return true;
}
const SurfaceWriteCapture* WindowLayers::Capture(uint32_t key,int left,int top,int width,int height) {
    if(!Enabled() || !key || width<=0 || height<=0 || width>8192 || height>8192 ||
       uint64_t(width)*height*4>MaxPixelBytes)return ScreenCapture();
    Slot* selected=nullptr;
    for(auto& slot:slots_)if(slot.key==key){selected=&slot;break;}
    if(selected && selected->left==left && selected->top==top && selected->surface.width==width && selected->surface.height==height) {
        selected->touched=++clock_;
        if(!++selected->writeSerial){selected->writeSerial=1;selected->generation=++generation_;std::memset(selected->versions.data(),0,selected->versions.size()*8);}
        selected->capture.revision=selected->writeSerial;return &selected->capture;
    }
    if(selected)Drop(*selected);
    if(!selected)for(auto& slot:slots_)if(!slot.key){selected=&slot;break;}
    if(!selected) {
        selected=&*std::min_element(slots_.begin(),slots_.end(),[](const Slot& a,const Slot& b){return a.touched<b.touched;});
        Drop(*selected);
    }
    const size_t bytes=size_t(width)*height*4;
    while(pixelBytes_>MaxPixelBytes-bytes) {
        Slot* oldest=nullptr;
        for(auto& slot:slots_)if(slot.key && (!oldest || slot.touched<oldest->touched))oldest=&slot;
        if(!oldest)return ScreenCapture();
        Drop(*oldest);
    }
    if(nextToken_==std::numeric_limits<uint32_t>::max()) {
        // Never let a wrapped token match any old ownership/cache entry.
        for(auto& slot:slots_)Drop(slot);
        nextToken_=1;++generation_;
    }
    PresentationBuffer<uint32_t> pixels(allocator_);PresentationBuffer<uint64_t> versions(allocator_);
    if(!pixels.Resize(size_t(width)*height) || !versions.Resize(size_t((width+31)/32)*((height+31)/32)))return ScreenCapture();
    selected->pixels.Swap(pixels);selected->versions.Swap(versions);
    std::memset(selected->pixels.data(),0,bytes);
    std::memset(selected->versions.data(),0,selected->versions.size()*8);
    selected->key=key;selected->token=nextToken_++;selected->generation=++generation_;selected->touched=++clock_;
    selected->left=left;selected->top=top;
    selected->surface.width=width;selected->surface.height=height;selected->surface.pitch=width*4;
    selected->surface.bits=reinterpret_cast<uint8_t*>(selected->pixels.data());
    selected->capture=screenCapture_;selected->capture.token=selected->token;
    selected->capture.pixels=selected->pixels.data();selected->capture.left=left;selected->capture.top=top;
    selected->capture.width=width;selected->capture.height=height;
    selected->writeSerial=1;selected->capture.revision=1;selected->capture.pixelCols=(width+31)/32;
    selected->capture.pixelVersions=selected->versions.data();
    pixelBytes_+=bytes;return &selected->capture;
}
void WindowLayers::Forget(uint32_t key) {for(auto& slot:slots_)if(slot.key==key){Drop(slot);break;}}
WindowSceneView WindowLayers::Scene() {
    size_t count=0;
    for(auto& slot:slots_)if(slot.key)views_[count++]={&slot.surface,slot.token,slot.left,slot.top,slot.generation,&slot.versions};
    return {owners_.data(),screenCapture_.screenWidth,screenCapture_.screenHeight,views_.data(),count,true,&ownerVersions_,ownerEpoch_};
}
} // namespace ui
