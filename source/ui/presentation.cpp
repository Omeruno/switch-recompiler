#include "presentation.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>

namespace ui {
namespace {
using Steady=std::chrono::steady_clock;
uint64_t Elapsed(Steady::time_point start) {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(Steady::now()-start).count());
}
bool Dimensions(int w,int h) {return w>0 && h>0 && w<=8192 && h<=8192;}
bool ValidSource(const Surface& s) {
    if(!s.bits || !Dimensions(s.width,s.height))return false;
    if(s.bpp!=1 && s.bpp!=4 && s.bpp!=8 && s.bpp!=16 && s.bpp!=24 && s.bpp!=32)return false;
    const int64_t pitch=s.pitch;
    return (pitch<0?-pitch:pitch)>=Surface::DibPitch(s.width,s.bpp);
}
uint32_t Rgba(uint32_t rgb) {return 0xFF000000u|((rgb>>16)&255)|(rgb&0xFF00)|((rgb&255)<<16);}
}
bool Viewport::Contains(int px,int py) const {
    return Valid() && px>=x && py>=y && int64_t(px)<int64_t(x)+width && int64_t(py)<int64_t(y)+height;
}
bool Viewport::ToSource(int px,int py,int sw,int sh,float& sx,float& sy) const {
    if(!Valid() || !Dimensions(sw,sh))return false;
    sx=std::max(0.0f,std::min(static_cast<float>((int64_t(px)-x)*sw)/width,static_cast<float>(sw-1)));
    sy=std::max(0.0f,std::min(static_cast<float>((int64_t(py)-y)*sh)/height,static_cast<float>(sh-1)));
    return true;
}
Viewport FitViewport(int sw,int sh,int ow,int oh) {
    if(!Dimensions(sw,sh) || !Dimensions(ow,oh))return {};
    int w=ow,h=oh;
    if(int64_t(sw)*oh<=int64_t(ow)*sh)w=std::max(1,int(int64_t(sw)*oh/sh));
    else h=std::max(1,int(int64_t(sh)*ow/sw));
    return {(ow-w)/2,(oh-h)/2,w,h};
}
bool OutputBuffer::Valid() const {
    if(!pixels || !Dimensions(width,height) || stridePixels<static_cast<size_t>(width))return false;
    const size_t rows=static_cast<size_t>(height-1);
    if(rows && stridePixels>(std::numeric_limits<size_t>::max()-size_t(width))/rows)return false;
    return capacityPixels>=rows*stridePixels+static_cast<size_t>(width);
}
bool SoftwarePresentation::Render(const PresentationFrame& frame,const OutputBuffer& out) {
    const Surface& s=frame.surface;const Viewport& v=frame.viewport;
    if(!ValidSource(s) || !out.Valid() || !v.Valid() || v.x<0 || v.y<0 ||
       int64_t(v.x)+v.width>out.width || int64_t(v.y)+v.height>out.height)return false;
    if(sourceWidth_!=s.width || outputWidth_!=v.width) {
        columns_.resize(static_cast<size_t>(v.width));
        for(int x=0;x<v.width;++x)columns_[x]=int(int64_t(x)*s.width/v.width);
        sourceWidth_=s.width;outputWidth_=v.width;
    }
    for(int y=0;y<out.height;++y) {
        uint32_t* row=out.pixels+size_t(y)*out.stridePixels;
        if(y<v.y || y>=v.y+v.height) {std::fill(row,row+out.width,0xFF000000u);continue;}
        std::fill(row,row+v.x,0xFF000000u);
        std::fill(row+v.x+v.width,row+out.width,0xFF000000u);
        const int sy=int(int64_t(y-v.y)*s.height/v.height);
        if(s.bpp==32) {
            const uint8_t* src=s.Row(sy);
            // Native-size desktop: contiguous reads, no column-map gather.
            // memcpy also permits unaligned guest DIB rows and negative pitch.
            if(v.width==s.width) {
                for(int x=0;x<v.width;++x) {uint32_t rgb;std::memcpy(&rgb,src+size_t(x)*4,4);row[v.x+x]=Rgba(rgb);}
            } else for(int x=0;x<v.width;++x) {uint32_t rgb;std::memcpy(&rgb,src+size_t(columns_[x])*4,4);row[v.x+x]=Rgba(rgb);}
        } else for(int x=0;x<v.width;++x)row[v.x+x]=Rgba(s.Get(columns_[x],sy));
    }
    // Kept pixel-for-pixel identical to the previous Switch cursor.
    static const char* const arrow[]={"X","XX","X.X","X..X","X...X","X....X","X.....X","X......X","X.......X","X........X",
        "X.....XXXXX","X..X..X","X.X X..X","XX  X..X","X    X..X","     X..X","      X..X","      X..X","       XX"};
    if(frame.cursor.visible) {
        const int cx=std::max(0,std::min(frame.cursor.x,s.width-1));
        const int cy=std::max(0,std::min(frame.cursor.y,s.height-1));
        const int px=v.x+int(int64_t(cx)*v.width/s.width),py=v.y+int(int64_t(cy)*v.height/s.height);
        for(int y=0;y<19;++y)for(int x=0;arrow[y][x];++x) {
            const char ch=arrow[y][x];if(ch==' ' || px+x>=out.width || py+y>=out.height)continue;
            out.pixels[size_t(py+y)*out.stridePixels+px+x]=ch=='X'?0xFF000000u:0xFFFFFFFFu;
        }
    }
    return true;
}
void Presentation::ClearFrame() {
    snapshot_.s=Surface{};std::vector<uint8_t>().swap(snapshot_.storage);serial_=lastRenderedSerial_=0;
}
void Presentation::Detach() {ClearFrame();backend_=nullptr;}
bool Presentation::Submit(const Surface& s) {
    if(!ValidSource(s) || uint64_t(s.width)*s.height*4>MaxFrameBytes) {
        ++stats_.rejected;ClearFrame();return false;
    }
    const auto start=Steady::now();
    if(snapshot_.s.width!=s.width || snapshot_.s.height!=s.height)snapshot_.Allocate(s.width,s.height);
    for(int y=0;y<s.height;++y) {
        if(s.bpp==32)std::memcpy(snapshot_.s.Row(y),s.Row(y),size_t(s.width)*4);
        else for(int x=0;x<s.width;++x)snapshot_.s.Set(x,y,s.Get(x,y));
    }
    ++serial_;++stats_.submitted;stats_.snapshotBytes+=snapshot_.storage.size();stats_.snapshotNs+=Elapsed(start);
    return true;
}
bool Presentation::Render(const OutputBuffer& out,PresentationCursor cursor) {
    if(!backend_ || !snapshot_.s.Valid() || !out.Valid()) {++stats_.rejected;return false;}
    const auto start=Steady::now();
    PresentationFrame frame{snapshot_.s,FitViewport(snapshot_.s.width,snapshot_.s.height,out.width,out.height),cursor,serial_};
    const bool ok=backend_->Render(frame,out);stats_.renderNs+=Elapsed(start);
    if(!ok) {++stats_.rejected;return false;}
    ++stats_.rendered;if(lastRenderedSerial_==serial_)++stats_.redraws;
    lastRenderedSerial_=serial_;stats_.outputBytes+=uint64_t(out.width)*out.height*4;return true;
}
} // namespace ui
