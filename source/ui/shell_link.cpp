#include "shell_link.h"
#include "text_document.h"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
namespace ui {
namespace {
using Bytes=std::vector<uint8_t>;
constexpr uint8_t Clsid[]={1,20,2,0,0,0,0,0,192,0,0,0,0,0,0,70};
uint32_t Get(const Bytes& b,size_t p){return uint32_t(b[p])|(uint32_t(b[p+1])<<8)|(uint32_t(b[p+2])<<16)|(uint32_t(b[p+3])<<24);}
void Set(Bytes& b,size_t p,uint32_t v){for(unsigned i=0;i<4;++i)b[p+i]=uint8_t(v>>(8*i));}
void Put16(Bytes& b,uint16_t v){b.push_back(uint8_t(v));b.push_back(uint8_t(v>>8));}
bool Wide(const Bytes& b,size_t start,size_t units,std::string& out) {
    if(start>b.size() || units>(b.size()-start)/2)return false;
    std::u16string w;for(size_t n=0;n<units;++n){auto c=char16_t(b[start+2*n]|uint16_t(b[start+2*n+1])<<8);if(!c || c<32)return false;w+=c;}
    for(size_t i=0;i<w.size();++i)if(w[i]>=0xd800 && w[i]<=0xdbff){if(++i>=w.size() || w[i]<0xdc00 || w[i]>0xdfff)return false;}else if(w[i]>=0xdc00 && w[i]<=0xdfff)return false;
    out=TextDocument::Utf8(w);return true;
}
bool ZWide(const Bytes& b,size_t start,size_t end,std::string& out) {
    if(start>end || end>b.size())return false;
    size_t units=0;while(start+units*2+1<end){if(!b[start+units*2] && !b[start+units*2+1])return Wide(b,start,units,out);if(++units>4096)return false;}return false;
}
bool String(Bytes& b,const std::string& s,bool counted) {
    std::u16string w;if(!TextDocument::DecodeUtf8(s,w) || w.size()>16384)return false;
    for(auto c:w)if(c<32)return false;
    if(counted)Put16(b,uint16_t(w.size()));
    for(auto c:w)Put16(b,c);
    if(!counted)Put16(b,0);
    return true;
}
bool Absent(const std::string& p){
    struct stat st{};
#ifdef __SWITCH__
    const int status=stat(p.c_str(),&st);
#else
    const int status=lstat(p.c_str(),&st);
#endif
    if(status==0){errno=EEXIST;return false;}return errno==ENOENT;
}
}
bool ShellLink::Absolute(const std::string& p) {
    if(p.size()<3 || p.size()>4096 || !((p[0]>='A'&&p[0]<='Z') || (p[0]>='a'&&p[0]<='z')) || p[1]!=':' || p[2]!='\\')return false;
    for(size_t i=2;i<p.size();++i)if(uint8_t(p[i])<32 || p[i]==':' || p[i]=='/' || p[i]=='"')return false;
    return true;
}
bool ShellLink::IsLink(const std::string& p) {if(p.size()<4)return false;auto tail=p.substr(p.size()-4);for(auto& c:tail)if(c>='A'&&c<='Z')c+=32;return tail==".lnk";}
Bytes ShellLink::Encode() const {
    if(!Absolute(target) || (!workingDirectory.empty() && !Absolute(workingDirectory)) || target.size()<4 || arguments.size()>16384 || name.size()>255 || icon.size()>4096)return {};
    Bytes b(76);Set(b,0,76);std::copy(std::begin(Clsid),std::end(Clsid),b.begin()+4);
    Set(b,20,0xf6);Set(b,24,0x20);Set(b,60,1);
    Bytes info(36);Set(info,4,36);Set(info,8,1);Set(info,12,36);
    Bytes volume(20);Set(volume,0,20);Set(volume,4,0);Set(volume,12,16); // empty provider label, unknown serial 0
    info.insert(info.end(),volume.begin(),volume.end());Set(info,16,uint32_t(info.size()));
    std::u16string targetWide;if(!TextDocument::DecodeUtf8(target,targetWide))return {};
    for(auto c:targetWide)info.push_back(c<128?uint8_t(c):uint8_t('?'));
    info.push_back(0);
    Set(info,24,uint32_t(info.size()));info.push_back(0);
    Set(info,28,uint32_t(info.size()));if(!String(info,target,false))return {};
    Set(info,32,uint32_t(info.size()));Put16(info,0);Set(info,0,uint32_t(info.size()));b.insert(b.end(),info.begin(),info.end());
    if(!String(b,name,true) || !String(b,workingDirectory,true) || !String(b,arguments,true) || !String(b,icon,true))return {};
    b.resize(b.size()+4);if(b.size()>MaxBytes)return {};return b;
}
bool ShellLink::Decode(const Bytes& b) {
    if(b.size()<116 || b.size()>MaxBytes || Get(b,0)!=76 || std::memcmp(b.data()+4,Clsid,16)!=0)return false;
    const auto flags=Get(b,20);if((flags&~0xf6u) || (flags&0x82)!=0x82 || Get(b,60)!=1)return false;
    const size_t begin=76,size=Get(b,begin);if(size<36 || size>b.size()-begin || Get(b,begin+4)<36 || Get(b,begin+4)>size || Get(b,begin+8)!=1)return false;
    const auto volume=Get(b,begin+12),ansi=Get(b,begin+16),suffix=Get(b,begin+24),unicode=Get(b,begin+28),usuffix=Get(b,begin+32);
    if(volume<36 || volume>size-16 || Get(b,begin+volume)<16 || Get(b,begin+volume)>size-volume || ansi<36 || ansi>=size || suffix<36 || suffix>=size || unicode<36 || usuffix<36 || unicode>=size || usuffix>=size)return false;
    ShellLink next;std::string tail;
    if(!ZWide(b,begin+unicode,begin+size,next.target) || !ZWide(b,begin+usuffix,begin+size,tail))return false;
    // LocalBasePath already includes the local target; suffix is shared metadata.
    size_t at=begin+size;
    auto field=[&](std::string& out,uint32_t flag) {
        if(!(flags&flag))return true;
        if(at+2>b.size())return false;
        const size_t units=b[at]|size_t(b[at+1])<<8;at+=2;if(!Wide(b,at,units,out))return false;at+=units*2;return true;
    };
    if(!field(next.name,4) || !field(next.workingDirectory,16) || !field(next.arguments,32) || !field(next.icon,64) || at+4!=b.size() || Get(b,at)!=0 || !Absolute(next.target) || (!next.workingDirectory.empty() && !Absolute(next.workingDirectory)) || next.name.size()>255 || next.arguments.size()>16384)return false;
    *this=std::move(next);return true;
}
bool ShellLink::Load(const rt::Vfs& vfs,const std::string& path,rt::FileCopyIo* injected) {
    size_t budget=8192;const auto host=vfs.ToHost(path,false,&budget);if(host.empty())return false;
    struct stat st{};if(stat(host.c_str(),&st)!=0)return false;if(!S_ISREG(st.st_mode) || st.st_size<0 || uint64_t(st.st_size)>MaxBytes){errno=EFBIG;return false;}
    rt::FileCopyIo native;auto& io=injected?*injected:native;int fd=io.OpenRead(host);if(fd<0)return false;
    Bytes b;uint8_t buf[4096];int error=0;unsigned tries=0;
    for(;;){auto n=io.Read(fd,buf,sizeof(buf));if(n<0){if(errno==EINTR && ++tries<=16)continue;error=errno;break;}if(!n)break;if(size_t(n)>MaxBytes-b.size()){error=EFBIG;break;}b.insert(b.end(),buf,buf+n);}
    if(io.Close(fd)!=0 && !error)error=errno;
    if(error){errno=error;return false;}if(!Decode(b)){errno=EINVAL;return false;}return true;
}
bool ShellLink::Save(const rt::Vfs& vfs,const std::string& path,rt::FileCopyIo* injected) const {
    auto b=Encode();if(b.empty()){errno=EINVAL;return false;}size_t budget=8192;
    const auto host=vfs.ToHost(path,true,&budget);if(host.empty() || !Absent(host))return false;
    rt::FileCopyIo native;auto& io=injected?*injected:native;static std::atomic<unsigned> serial{0};std::string stage;int fd=-1;
    for(unsigned n=0;n<64;++n){char leaf[40];std::snprintf(leaf,sizeof(leaf),".sr-link-%08x.tmp",serial++);stage=host.substr(0,host.find_last_of('/')+1)+leaf;fd=io.CreateExclusive(stage);if(fd>=0)break;if(errno!=EEXIST)return false;}
    if(fd<0){errno=EEXIST;return false;}int error=0;size_t at=0;unsigned tries=0;
    while(at<b.size()){auto n=io.Write(fd,b.data()+at,b.size()-at);if(n<0 && errno==EINTR && ++tries<=16)continue;if(n<=0){error=n<0?errno:EIO;break;}at+=size_t(n);}
    if(!error && io.Flush(fd)!=0)error=errno;
    if(io.Close(fd)!=0 && !error)error=errno;
    if(!error && (!Absent(host) || io.Rename(stage,host)!=0))error=errno;
    if(error){io.Remove(stage);errno=error;return false;}return true;
}
} // namespace ui
