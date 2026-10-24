#pragma once
#include "../runtime/files.h"
#include "../runtime/file_copy_io.h"
#include "surface.h"
#include "region.h"
namespace ui {
// Owned, bounded native BMP snapshot. No USER handles or borrowed guest data.
class BitmapDocument {
public:
    static constexpr size_t MaxBytes=16*1024*1024,MaxPixels=4*1024*1024;
    static constexpr int MaxDimension=8192;
    enum class Error {None,Io,TooLarge,Invalid,Unsupported};
    bool Open(const rt::Vfs&,const std::string&,rt::FileCopyIo* io=nullptr);
    bool Decode(const std::vector<uint8_t>&);
    void Clear() {*this=BitmapDocument();}
    int Width() const {return width_;}
    int Height() const {return height_;}
    uint32_t Pixel(int x,int y) const {return x>=0 && y>=0 && x<width_ && y<height_?pixels_[size_t(y)*width_+x]:0;}
    const std::string& Path() const {return path_;}
    Error LastError() const {return error_;}
    int IoError() const {return ioError_;}
    static const char16_t* ErrorText(Error);
    // Clip all output to viewport and target; fit only reduces, 100% can pan.
    void Draw(Surface&,Rect viewport,bool fit,int panX,int panY) const;
private:
    bool Fail(Error,int error=0);
    std::vector<uint32_t> pixels_;
    std::string path_;
    int width_=0,height_=0,ioError_=0;
    Error error_=Error::None;
};
} // namespace ui
