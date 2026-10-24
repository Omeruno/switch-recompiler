#include "source/ui/bitmap_document.h"
#include "source/ui/desktop_shell.h"
#include "source/ui/font.h"
#include "bitmap_fixture.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
namespace fs=std::filesystem;namespace fixture=bitmap_fixture;
using Image=ui::BitmapDocument;using Shell=ui::DesktopShell;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"bitmap %d: %s errno=%d\n",__LINE__,#x,errno);std::exit(1);}}while(0)
static void Write(const fs::path& p,const std::vector<uint8_t>& b){std::ofstream f(p,std::ios::binary);f.write(reinterpret_cast<const char*>(b.data()),b.size());CHECK(f.good());}
struct Fault:rt::FileCopyIo {
    enum class Mode {Short,Read,Open,Close,EndlessInterrupt,Truncated};Mode mode;int live=0;unsigned reads=0;
    explicit Fault(Mode m):mode(m){}
    int OpenRead(const std::string& p) override {if(mode==Mode::Open){errno=EACCES;return -1;}int fd=FileCopyIo::OpenRead(p);if(fd>=0)++live;return fd;}
    ssize_t Read(int fd,void* p,size_t n) override {
        ++reads;if(mode==Mode::Read || mode==Mode::EndlessInterrupt){errno=mode==Mode::Read?EIO:EINTR;return -1;}
        if(mode==Mode::Truncated && reads>1)return 0;
        return FileCopyIo::Read(fd,p,std::min(n,size_t(7)));
    }
    int Close(int fd) override {const auto r=FileCopyIo::Close(fd);--live;if(mode==Mode::Close){errno=EIO;return -1;}return r;}
};
static void Click(Shell& shell,int target){auto r=Shell::Bounds(target);shell.PointerDown(r.left+5,r.top+5);shell.PointerUp(r.left+5,r.top+5);}
static void OpenImage(Shell& shell) {
    Click(shell,Shell::FileActions);CHECK(shell.ContextOpen());
    for(size_t i=0;i<shell.ContextCount();++i)if(shell.ContextAction(i)==Shell::ImageOpen){auto r=shell.ContextBounds(Shell::ContextBase+int(i));shell.PointerDown(r.left+5,r.top+5);shell.PointerUp(r.left+5,r.top+5);return;}
    CHECK(false);
}
int main(int argc,char** argv) {
    Image image;
    for(unsigned bpp:{1,4,8,24,32})for(bool top:{false,true}) {
        CHECK(image.Decode(fixture::Make(7,3,bpp,top)) && image.Width()==7 && image.Height()==3);
        for(unsigned y=0;y<3;++y)for(unsigned x=0;x<7;++x)CHECK(image.Pixel(x,y)==(bpp<=8?fixture::Color((x+y)%(1u<<bpp),0):fixture::Color(x,y)));
    }
    const auto good=fixture::Make(7,3);CHECK(image.Decode(good));
    for(size_t size=0;size<good.size();++size){auto truncated=good;truncated.resize(size);CHECK(!image.Decode(truncated) && image.Width()==7 && image.Pixel(6,2)==fixture::Color(6,2));}
    auto bad=good;fixture::Put32(bad,18,0x7fffffff);CHECK(!image.Decode(bad) && image.LastError()==Image::Error::TooLarge);
    bad=good;fixture::Put32(bad,22,0x80000000);CHECK(!image.Decode(bad) && image.LastError()==Image::Error::TooLarge);
    bad=good;fixture::Put32(bad,18,0xffffffff);CHECK(!image.Decode(bad));
    bad=good;fixture::Put32(bad,18,4096);fixture::Put32(bad,22,4096);CHECK(!image.Decode(bad) && image.LastError()==Image::Error::TooLarge);
    for(auto field:{2,10,14,18,22,26,28,30,34,46}) {bad=good;fixture::Put32(bad,size_t(field),0xffffffff);CHECK(!image.Decode(bad));}
    bad=good;fixture::Put16(bad,6,1);CHECK(!image.Decode(bad));
    bad=good;fixture::Put32(bad,10,53);CHECK(!image.Decode(bad));
    bad=fixture::Make(7,3,8);fixture::Put32(bad,46,257);CHECK(!image.Decode(bad));
    bad=fixture::Make(7,3,8);fixture::Put32(bad,46,1);CHECK(!image.Decode(bad)); // palette index beyond declared table
    bad=good;fixture::Put32(bad,14,124);CHECK(!image.Decode(bad) && image.LastError()==Image::Error::Unsupported);
    uint32_t random=1;
    for(unsigned n=0;n<2000;++n){bad=good;random=random*1664525+1013904223;bad[random%54]^=uint8_t(random>>24);image.Decode(bad);}
    CHECK(image.Decode(fixture::Make(2048,2048)) && image.Width()==2048 && image.Pixel(2047,2047)==fixture::Color(2047,2047));
    CHECK(image.Decode(fixture::Make(8192,1)) && image.Width()==Image::MaxDimension);
    CHECK(image.Decode(fixture::Make(600,400)));
    ui::OwnedSurface pixels;pixels.Allocate(1280,720);for(int y=0;y<720;++y)for(int x=0;x<1280;++x)pixels.s.Set(x,y,0xABCDEF);
    image.Draw(pixels.s,{10,20,110,120},false,32,64);CHECK(pixels.s.Get(10,20)==fixture::Color(32,64) && pixels.s.Get(109,119)==fixture::Color(131,163));CHECK(pixels.s.Get(9,20)==0xABCDEF);
    image.Draw(pixels.s,{10,20,110,120},true,10000,10000);CHECK(pixels.s.Get(10,20)==0x263849 && pixels.s.Get(10,37)==fixture::Color(0,0));
    image.Draw(pixels.s,{-10,-20,90,80},false,0,0);CHECK(pixels.s.Get(0,0)==fixture::Color(10,20));
    char pattern[]="/tmp/sr-bitmap-XXXXXX";CHECK(mkdtemp(pattern));const fs::path root=pattern,rom=root/"rom";
    struct Cleanup{fs::path p;~Cleanup(){fs::remove_all(p);}} cleanup{root};fs::create_directory(rom);
    rt::Vfs v;v.SetRoot(root.string());CHECK(v.Mount('R',rom.string(),true));Write(root/"картинка 😀.bmp",good);Write(rom/"read.bmp",good);
    CHECK(image.Open(v,"C:\\картинка 😀.bmp") && image.Path()=="C:\\картинка 😀.bmp");
    for(auto mode:{Fault::Mode::Open,Fault::Mode::Read,Fault::Mode::Close,Fault::Mode::EndlessInterrupt,Fault::Mode::Truncated}) {
        Fault io(mode);CHECK(!image.Open(v,"C:\\картинка 😀.bmp",&io) && io.live==0 && image.Width()==7);
        if(mode==Fault::Mode::EndlessInterrupt)CHECK(io.reads==17);
    }
    Fault shorts(Fault::Mode::Short);CHECK(image.Open(v,"R:\\read.bmp",&shorts) && shorts.live==0);
    CHECK(!image.Open(v,"C:\\missing.bmp") && image.Width()==7);CHECK(!image.Open(v,"C:\\rom"));
    Write(root/"huge.bmp",{});fs::resize_file(root/"huge.bmp",Image::MaxBytes+1);CHECK(!image.Open(v,"C:\\huge.bmp") && image.LastError()==Image::Error::TooLarge);
    fs::create_symlink(root/"картинка 😀.bmp",root/"link.bmp");CHECK(!image.Open(v,"C:\\link.bmp"));
    // Actual native shell: context/controller, viewport pixels, scope lifetime.
    ui::FontCache::SetDirectory(argc>1?argv[1]:"../../romfs/fonts");ui::FontDesc font;font.face="Liberation Sans";CHECK(!ui::FontCache::Get(font)->Metrics().fixedPitch);
    Shell shell;std::string error;CHECK(shell.ConfigureFilesystem((root/"system").string(),"",rom.string(),error));
    Write(root/"system/Documents and Settings/Player/My Documents/Просмотр.BMP",fixture::Make(900,500));
    shell.Enter();shell.SetGuestActive("public guest");Click(shell,Shell::Documents);
    for(size_t i=0;i<shell.Browser().Entries().size();++i)if(shell.Browser().Entries()[i].name=="Просмотр.BMP")Click(shell,Shell::BrowserRowBase+int(i));
    OpenImage(shell);CHECK(shell.ActiveDialog()==Shell::Dialog::ImageViewer && shell.ImageFitted());
    shell.ToggleStart();CHECK(!shell.StartOpen());Click(shell,Shell::GuestTask);CHECK(shell.ModalActive());
    shell.NextFocus();CHECK(shell.Focus()==Shell::ImageActual);shell.Accept();CHECK(!shell.ImageFitted());
    shell.Page(1);shell.Move(2);CHECK(shell.ImagePanX()==32 && shell.ImagePanY()==64);
    shell.Render(pixels.s,u"14:32");const auto viewport=Shell::ImageViewport();CHECK(pixels.s.Get(viewport.left,viewport.top)==fixture::Color(32,64));
    if(argc>2){FILE* f=std::fopen(argv[2],"wb");CHECK(f);std::fprintf(f,"P6\n1280 720\n255\n");for(int y=0;y<720;++y)for(int x=0;x<1280;++x){auto c=pixels.s.Get(x,y);unsigned char rgb[]={uint8_t(c>>16),uint8_t(c>>8),uint8_t(c)};CHECK(std::fwrite(rgb,1,3,f)==3);}CHECK(std::fclose(f)==0);}
    shell.Page(100000);shell.Move(100000);CHECK(shell.ImagePanX()==900-viewport.Width() && shell.ImagePanY()==500-viewport.Height());
    shell.EndGuest();shell.Enter();CHECK(shell.ActiveDialog()==Shell::Dialog::ImageViewer && shell.Image().Width()==900);
    CHECK(!shell.ConfigureFilesystem((root/"other").string(),"","",error));Click(shell,Shell::ImageFit);CHECK(shell.ImageFitted() && shell.ImagePanX()==0);
    shell.Back();CHECK(!shell.ModalActive() && shell.Image().Width()==0 && shell.ActivePanel()==Shell::Panel::Explorer);
    CHECK(fs::file_size(root/"system/Documents and Settings/Player/My Documents/Просмотр.BMP")==fixture::Make(900,500).size());
    puts("PASSED: bounded native BMP codecs/palettes/stride/orientation, real mounts/I/O errors, owned viewport pixels and modal controller/lifetime");
}
