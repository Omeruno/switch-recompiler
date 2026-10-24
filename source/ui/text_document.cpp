#include "text_document.h"
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <sys/stat.h>
namespace ui {
namespace {
bool Absent(const std::string& path) {
    struct stat st{};
#ifdef __SWITCH__
    const int result=stat(path.c_str(),&st);
#else
    const int result=lstat(path.c_str(),&st);
#endif
    if(result==0){errno=EEXIST;return false;}return errno==ENOENT;
}
std::string Sibling(const std::string& path,const char* suffix) {
    static std::atomic<unsigned> serial{0};char leaf[64];
    std::snprintf(leaf,sizeof(leaf),".sr-txt-%08x.%s",serial.fetch_add(1),suffix);
    return path.substr(0,path.find_last_of('/')+1)+leaf;
}
}
bool TextDocument::Fail(int e) {error_=e?e:EIO;errno=error_;return false;}
bool TextDocument::DecodeUtf8(const std::string& s,std::u16string& out) {
    out.clear();
    for(size_t i=0;i<s.size();) {
        auto a=uint8_t(s[i++]);uint32_t cp=a,min=0;unsigned count=0;
        if(a>=0xc2 && a<=0xdf){cp=a&31;count=1;min=0x80;}
        else if(a>=0xe0 && a<=0xef){cp=a&15;count=2;min=0x800;}
        else if(a>=0xf0 && a<=0xf4){cp=a&7;count=3;min=0x10000;}
        else if(a>=128)return false;
        if(i+count>s.size())return false;
        for(unsigned n=0;n<count;++n){auto b=uint8_t(s[i++]);if((b&0xc0)!=0x80)return false;cp=(cp<<6)|(b&63);}
        if(cp<min || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff) || !cp)return false;
        if(cp>0xffff){cp-=0x10000;out.push_back(char16_t(0xd800+(cp>>10)));cp=0xdc00+(cp&1023);}
        out.push_back(char16_t(cp));
    }
    return true;
}
std::string TextDocument::Utf8(const std::u16string& s) {
    std::string out;
    for(size_t i=0;i<s.size();++i) {
        uint32_t cp=s[i];
        if(cp>=0xd800 && cp<=0xdbff && i+1<s.size()){cp=0x10000+((cp-0xd800)<<10)+(s[++i]-0xdc00);}
        if(cp<0x80)out+=char(cp);
        else if(cp<0x800){out+=char(0xc0|(cp>>6));out+=char(0x80|(cp&63));}
        else if(cp<0x10000){out+=char(0xe0|(cp>>12));out+=char(0x80|((cp>>6)&63));out+=char(0x80|(cp&63));}
        else {out+=char(0xf0|(cp>>18));out+=char(0x80|((cp>>12)&63));out+=char(0x80|((cp>>6)&63));out+=char(0x80|(cp&63));}
    }
    return out;
}
void TextDocument::New() {*this=TextDocument();}
bool TextDocument::Read(rt::FileCopyIo& io,const std::string& path,std::string& bytes) {
    struct stat st{};if(stat(path.c_str(),&st)!=0)return Fail(errno);
    if(!S_ISREG(st.st_mode))return Fail(EACCES);
    if(st.st_size<0 || uint64_t(st.st_size)>MaxBytes)return Fail(EFBIG);
    const int fd=io.OpenRead(path);if(fd<0)return Fail(errno);
    bytes.clear();int error=0;unsigned interrupts=0;char buf[4096];
    for(;;) {
        auto n=io.Read(fd,buf,sizeof(buf));
        if(n<0){if(errno==EINTR && ++interrupts<=16)continue;error=errno;break;}
        if(!n)break;
        if(bytes.size()+size_t(n)>MaxBytes){error=EFBIG;break;}
        bytes.append(buf,size_t(n));
    }
    if(io.Close(fd)!=0 && !error)error=errno;
    return error?Fail(error):true;
}
bool TextDocument::Open(const rt::Vfs& vfs,const std::string& path,rt::FileCopyIo* injected) {
    rt::FileCopyIo native;auto& io=injected?*injected:native;size_t budget=16384;
    const auto host=vfs.ToHost(path,false,&budget);if(host.empty())return Fail(errno);
    std::string bytes;if(!Read(io,host,bytes))return false;
    TextDocument next;std::u16string text;size_t offset=0;
    if(bytes.compare(0,3,"\xef\xbb\xbf")==0){next.encoding_=Encoding::Utf8Bom;offset=3;}
    else if(bytes.compare(0,2,"\xff\xfe")==0){next.encoding_=Encoding::Utf16LE;offset=2;}
    else if(bytes.compare(0,2,"\xfe\xff")==0){next.encoding_=Encoding::Utf16BE;offset=2;}
    if(next.encoding_==Encoding::Utf8 || next.encoding_==Encoding::Utf8Bom) {
        if(!DecodeUtf8(bytes.substr(offset),text))return Fail(EILSEQ);
    } else {
        if((bytes.size()-offset)%2)return Fail(EILSEQ);
        for(size_t i=offset;i<bytes.size();i+=2) {
            uint16_t a=uint8_t(bytes[i]),b=uint8_t(bytes[i+1]);text.push_back(char16_t(next.encoding_==Encoding::Utf16LE?a|(b<<8):(a<<8)|b));
        }
        for(size_t i=0;i<text.size();++i) {
            auto c=text[i];if(!c)return Fail(EILSEQ);
            if(c>=0xd800 && c<=0xdbff){if(++i>=text.size() || text[i]<0xdc00 || text[i]>0xdfff)return Fail(EILSEQ);}
            else if(c>=0xdc00 && c<=0xdfff)return Fail(EILSEQ);
        }
    }
    // Binary/control input is refused, never guessed as an ANSI code page.
    for(auto c:text)if((c<32 && c!='\t' && c!='\r' && c!='\n') || c==127)return Fail(EILSEQ);
    next.lines_.clear();size_t start=0;bool foundEnding=false;
    for(size_t i=0;i<text.size();++i)if(text[i]=='\r' || text[i]=='\n') {
        const size_t end=i;if(text[i]=='\r' && i+1<text.size() && text[i+1]=='\n')++i;
        auto ending=text.substr(end,i-end+1);next.lines_.push_back({text.substr(start,end-start),ending});
        if(!foundEnding){next.ending_=ending;foundEnding=true;}start=i+1;
    }
    next.lines_.push_back({text.substr(start),{}});next.original_=bytes;next.path_=vfs.FullGuestPath(path);next.fresh_=false;
    *this=std::move(next);return true;
}
std::string TextDocument::Bytes() const {
    std::u16string text;for(const auto& l:lines_){text+=l.text;text+=l.ending;}
    if(encoding_==Encoding::Utf8)return Utf8(text);
    if(encoding_==Encoding::Utf8Bom)return std::string("\xef\xbb\xbf")+Utf8(text);
    std::string bytes=encoding_==Encoding::Utf16LE?"\xff\xfe":"\xfe\xff";
    for(auto c:text){bytes+=char(encoding_==Encoding::Utf16LE?c&255:c>>8);bytes+=char(encoding_==Encoding::Utf16LE?c>>8:c&255);}return bytes;
}
bool TextDocument::Fits(const std::vector<Line>& lines) const {
    // Avoid serializing a candidate larger than the bounded document.
    size_t units=0;for(const auto& l:lines)units+=l.text.size()+l.ending.size();
    if(units>MaxBytes)return false;
    auto copy=*this;copy.lines_=lines;return copy.Bytes().size()<=MaxBytes;
}
bool TextDocument::Dirty() const {return fresh_ || Bytes()!=original_;}
bool TextDocument::Edit(size_t index,const std::string& utf8) {
    std::u16string text;if(index>=lines_.size())return Fail(EINVAL);
    if(!DecodeUtf8(utf8,text))return Fail(EILSEQ);
    if(text.size()>MaxEditUnits)return Fail(EFBIG);
    for(auto c:text)if((c<32 && c!='\t') || c==127)return Fail(EILSEQ);
    auto lines=lines_;lines[index].text=std::move(text);if(!Fits(lines))return Fail(EFBIG);
    lines_=std::move(lines);error_=0;return true;
}
bool TextDocument::InsertAfter(size_t index) {
    if(index>=lines_.size())return Fail(EINVAL);
    auto lines=lines_;auto ending=lines[index].ending;
    if(ending.empty())lines[index].ending=ending_;else ending=ending_;
    lines.insert(lines.begin()+index+1,Line{{},ending});if(!Fits(lines))return Fail(EFBIG);
    lines_=std::move(lines);error_=0;return true;
}
bool TextDocument::Erase(size_t index) {
    if(index>=lines_.size())return Fail(EINVAL);
    if(lines_.size()==1)lines_[0]={};else lines_.erase(lines_.begin()+index);
    error_=0;return true;
}
bool TextDocument::Save(const rt::Vfs& vfs,const std::string& saveAs,rt::FileCopyIo* injected) {
    rt::FileCopyIo native;auto& io=injected?*injected:native;error_=0;backup_.clear();recovery_.clear();size_t budget=16384;
    const bool replace=saveAs.empty() && !fresh_;const auto guest=saveAs.empty()?path_:vfs.FullGuestPath(saveAs);
    if(guest.empty())return Fail(EINVAL);
    const auto host=vfs.ToHost(guest,true,&budget);if(host.empty())return Fail(errno);
    const auto bytes=Bytes();if(bytes.size()>MaxBytes)return Fail(EFBIG);
    auto unchanged=[&]() {
        std::string current;rt::FileProperties p;
        if(!vfs.QueryProperties(guest,p,&budget))return Fail(errno);
        if(p.kind!=rt::FileProperties::Kind::File || !p.ownerWriteBit)return Fail(EACCES);
        if(!Read(io,host,current))return false;
        return current==original_?true:Fail(EBUSY);
    };
    if(replace){if(!unchanged())return false;}else if(!Absent(host))return Fail(errno);
    std::string stage;int fd=-1;
    for(unsigned n=0;n<64;++n){stage=Sibling(host,"tmp");fd=io.CreateExclusive(stage);if(fd>=0)break;if(errno!=EEXIST)return Fail(errno);}
    if(fd<0)return Fail(EEXIST);
    int error=0;size_t offset=0;unsigned interrupts=0;
    while(offset<bytes.size()) {
        const auto n=io.Write(fd,bytes.data()+offset,bytes.size()-offset);
        if(n<0){if(errno==EINTR && ++interrupts<=16)continue;error=errno;break;}
        if(!n){error=EIO;break;}offset+=size_t(n);
    }
    if(!error && io.Flush(fd)!=0)error=errno;
    if(io.Close(fd)!=0 && !error)error=errno;
    auto cleanup=[&](){if(io.Remove(stage)!=0)recovery_=stage;};
    if(error){cleanup();return Fail(error);}
    if(replace) {
        // Recheck after all staged writes, before moving the original. No yield
        // or guest callback inside this bounded synchronous publication block.
        if(!unchanged()){error=error_;cleanup();return Fail(error);}
        std::string backup;bool available=false;
        for(unsigned n=0;n<64;++n){backup=Sibling(host,"bak");if(Absent(backup)){available=true;break;}if(errno!=EEXIST)break;}
        if(!available){error=errno;cleanup();return Fail(error);}
        if(io.Rename(host,backup)!=0){error=errno;cleanup();return Fail(error);}
        backup_=backup; // retained even on success: real original bytes
        if(!Absent(host) || io.Rename(stage,host)!=0) {
            error=errno;
            if(Absent(host) && io.Rename(backup,host)==0)backup_.clear();
            cleanup();return Fail(error);
        }
    } else if(!Absent(host) || io.Rename(stage,host)!=0){error=errno;cleanup();return Fail(error);}
    original_=bytes;path_=guest;fresh_=false;error_=0;return true;
}
} // namespace ui
