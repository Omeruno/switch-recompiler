#include "bitmap_document.h"
#include <cerrno>
#include <sys/stat.h>
namespace ui {
namespace {
uint16_t U16(const uint8_t* p){return uint16_t(p[0])|(uint16_t(p[1])<<8);}
uint32_t U32(const uint8_t* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
uint32_t RGB(const uint8_t* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16);}
}
bool BitmapDocument::Fail(Error e,int native){error_=e;ioError_=native;if(native)errno=native;return false;}
const char16_t* BitmapDocument::ErrorText(Error e) {
    switch(e) {
    case Error::None:return u"";
    case Error::Io:return u"Изображение не прочитано. Проверьте путь и доступ.";
    case Error::TooLarge:return u"Предел BMP: 16 МиБ, 4 Мп, сторона до 8192.";
    case Error::Unsupported:return u"Нужен BMP INFOHEADER: RGB 1/4/8/24/32 бит, без сжатия.";
    case Error::Invalid:return u"Повреждённый BMP: заголовок, палитра или пиксели.";
    }
    return u"Ошибка изображения.";
}
bool BitmapDocument::Open(const rt::Vfs& vfs,const std::string& path,rt::FileCopyIo* injected) {
    size_t budget=16384;const auto host=vfs.ToHost(path,false,&budget);if(host.empty())return Fail(Error::Io,errno);
    struct stat st{};if(stat(host.c_str(),&st)!=0)return Fail(Error::Io,errno);
    if(!S_ISREG(st.st_mode))return Fail(Error::Io,EACCES);
    if(st.st_size<0 || uint64_t(st.st_size)>MaxBytes)return Fail(Error::TooLarge,EFBIG);
    rt::FileCopyIo native;auto& io=injected?*injected:native;
    const int fd=io.OpenRead(host);if(fd<0)return Fail(Error::Io,errno);
    std::vector<uint8_t> bytes;bytes.reserve(size_t(st.st_size));uint8_t buffer[8192];unsigned interrupts=0;int error=0;
    for(;;) {
        const auto n=io.Read(fd,buffer,sizeof(buffer));
        if(n<0){if(errno==EINTR && ++interrupts<=16)continue;error=errno;break;}
        if(!n)break;
        if(bytes.size()+size_t(n)>MaxBytes){error=EFBIG;break;}
        bytes.insert(bytes.end(),buffer,buffer+size_t(n));
    }
    if(io.Close(fd)!=0 && !error)error=errno;
    if(error)return Fail(error==EFBIG?Error::TooLarge:Error::Io,error);
    if(!Decode(bytes))return false;
    path_=vfs.FullGuestPath(path);return true;
}
bool BitmapDocument::Decode(const std::vector<uint8_t>& bytes) {
    if(bytes.size()>MaxBytes)return Fail(Error::TooLarge);
    if(bytes.size()<54 || bytes[0]!='B' || bytes[1]!='M')return Fail(Error::Invalid);
    const auto* p=bytes.data();const auto offset=U32(p+10),header=U32(p+14);
    if(header!=40)return Fail(Error::Unsupported);
    if(U16(p+6) || U16(p+8) || U16(p+26)!=1 || U32(p+2)!=bytes.size())return Fail(Error::Invalid);
    const int64_t width=int32_t(U32(p+18)),signedHeight=int32_t(U32(p+22));
    if(width<=0 || !signedHeight)return Fail(Error::Invalid);
    const int64_t height=signedHeight<0?-signedHeight:signedHeight;
    if(width>MaxDimension || height>MaxDimension || uint64_t(width)*uint64_t(height)>MaxPixels)return Fail(Error::TooLarge);
    const unsigned bpp=U16(p+28);const uint32_t compression=U32(p+30),used=U32(p+46);
    if(compression || (bpp!=1 && bpp!=4 && bpp!=8 && bpp!=24 && bpp!=32))return Fail(Error::Unsupported);
    const uint64_t pitch=((uint64_t(width)*bpp+31)/32)*4,extent=pitch*uint64_t(height);
    const uint32_t colors=bpp<=8?(used?used:1u<<bpp):0;
    if((bpp<=8 && colors>(1u<<bpp)) || (bpp>8 && used))return Fail(Error::Invalid);
    const uint64_t minimum=54+uint64_t(colors)*4;
    if(offset<minimum || uint64_t(offset)+extent>bytes.size() || (U32(p+34) && U32(p+34)!=extent))return Fail(Error::Invalid);
    // All dimensions, strides and encoded ranges are checked before allocating.
    std::vector<uint32_t> pixels(size_t(width)*size_t(height));
    for(int y=0;y<height;++y) {
        const auto* row=p+offset+size_t(signedHeight<0?y:height-1-y)*size_t(pitch);
        for(int x=0;x<width;++x) {
            uint32_t rgb;
            if(bpp>=24)rgb=RGB(row+size_t(x)*(bpp/8)); // BI_RGB's fourth byte is reserved, not alpha
            else {
                unsigned index=bpp==8?row[x]:bpp==4?(row[x/2]>>((x%2)?0:4))&15:(row[x/8]>>(7-x%8))&1;
                if(index>=colors)return Fail(Error::Invalid);
                rgb=RGB(p+54+index*4);
            }
            pixels[size_t(y)*size_t(width)+x]=rgb;
        }
    }
    pixels_=std::move(pixels);width_=int(width);height_=int(height);path_.clear();error_=Error::None;ioError_=0;return true;
}
void BitmapDocument::Draw(Surface& target,Rect viewport,bool fit,int panX,int panY) const {
    if(!target.Valid() || target.bpp!=32 || viewport.Empty() || !width_ || !height_)return;
    const auto clip=viewport.Intersect({0,0,target.width,target.height});
    for(int y=clip.top;y<clip.bottom;++y)for(int x=clip.left;x<clip.right;++x)target.Set(x,y,0x263849);
    int w=width_,h=height_;
    if(fit && (w>viewport.Width() || h>viewport.Height())) {
        if(int64_t(w)*viewport.Height()>int64_t(h)*viewport.Width()){w=viewport.Width();h=std::max(1,int(int64_t(height_)*w/width_));}
        else {h=viewport.Height();w=std::max(1,int(int64_t(width_)*h/height_));}
    }
    const int left=viewport.left+std::max(0,(viewport.Width()-w)/2),top=viewport.top+std::max(0,(viewport.Height()-h)/2);
    panX=fit?0:std::max(0,std::min(panX,std::max(0,width_-viewport.Width())));
    panY=fit?0:std::max(0,std::min(panY,std::max(0,height_-viewport.Height())));
    const auto draw=Rect(left,top,left+w,top+h).Intersect(clip);
    for(int y=draw.top;y<draw.bottom;++y)for(int x=draw.left;x<draw.right;++x) {
        const int sx=fit?int(int64_t(x-left)*width_/w):panX+x-left;
        const int sy=fit?int(int64_t(y-top)*height_/h):panY+y-top;
        target.Set(x,y,Pixel(sx,sy));
    }
}
} // namespace ui
