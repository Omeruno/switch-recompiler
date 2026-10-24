#include "shell_preferences.h"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <limits>
namespace ui {
namespace {
using Bytes=std::vector<uint8_t>;
constexpr char Magic[]="SRPREF01";
void Put(Bytes& out,uint64_t value,unsigned n=4){for(unsigned i=0;i<n;++i)out.push_back(uint8_t(value>>(8*i)));}
uint64_t Get(const Bytes& in,size_t& at,unsigned n=4){uint64_t v=0;for(unsigned i=0;i<n;++i)v|=uint64_t(in[at++])<<(8*i);return v;}
uint32_t Checksum(const Bytes& bytes) {
    uint32_t crc=~0u;
    for(size_t i=0;i<bytes.size();++i)if(i<20 || i>=24) {
        crc^=bytes[i];for(int bit=0;bit<8;++bit)crc=(crc>>1)^(0xEDB88320u&uint32_t(-int32_t(crc&1)));
    }
    return ~crc;
}
bool Extension(const std::string& ext) {
    if(ext.empty() || ext.size()>16 || ext=="exe")return false;
    for(char c:ext)if(!((c>='a'&&c<='z') || (c>='0'&&c<='9') || c=='_' || c=='-'))return false;
    return true;
}
bool Application(const std::string& path) {
    if(path.size()<7 || path.size()>4096 || path[0]<'A' || path[0]>'Z' || path[1]!=':' || path[2]!='\\')return false;
    for(size_t i=2;i<path.size();++i)if(static_cast<unsigned char>(path[i])<32 || path[i]==':' || path[i]=='/')return false;
    std::string tail=path.substr(path.size()-4);for(char& c:tail)if(c>='A'&&c<='Z')c=char(c+'a'-'A');
    return tail==".exe";
}
bool Decode(const Bytes& bytes,ShellPreferences& prefs,uint64_t& generation) {
    if(bytes.size()<32 || std::memcmp(bytes.data(),Magic,8)!=0)return false;
    size_t at=8;generation=Get(bytes,at,8);const auto size=Get(bytes,at),checksum=Get(bytes,at);
    if(!generation || size!=bytes.size()-24 || checksum!=Checksum(bytes))return false;
    prefs={};prefs.sort=uint32_t(Get(bytes,at));const auto count=Get(bytes,at);
    if(count>ShellPreferenceStore::MaxAssociations)return false;
    auto field=[&](std::string& out) {
        if(at+4>bytes.size())return false;
        const auto n=Get(bytes,at);if(n>4096 || n>bytes.size()-at)return false;
        out.assign(reinterpret_cast<const char*>(bytes.data()+at),size_t(n));at+=size_t(n);return true;
    };
    for(size_t i=0;i<count;++i) {ShellAssociation a;if(!field(a.extension) || !field(a.application))return false;prefs.associations.push_back(std::move(a));}
    return at==bytes.size() && ShellPreferenceStore::Valid(prefs);
}
Bytes Encode(const ShellPreferences& prefs,uint64_t generation) {
    Bytes b(Magic,Magic+8);Put(b,generation,8);Put(b,0);Put(b,0);Put(b,prefs.sort);Put(b,prefs.associations.size());
    for(const auto& a:prefs.associations)for(const auto* field:{&a.extension,&a.application}){Put(b,field->size());b.insert(b.end(),field->begin(),field->end());}
    const auto length=b.size()-24;for(unsigned i=0;i<4;++i)b[16+i]=uint8_t(length>>(8*i));
    const auto crc=Checksum(b);for(unsigned i=0;i<4;++i)b[20+i]=uint8_t(crc>>(8*i));return b;
}
}
std::string ShellPreferenceStore::Directory(const rt::Vfs& vfs) {return vfs.ProfileDirectory()+"\\Application Data\\SwitchRecompiler";}
bool ShellPreferenceStore::Valid(const ShellPreferences& p) {
    if(p.sort>2 || p.associations.size()>MaxAssociations)return false;
    for(size_t i=0;i<p.associations.size();++i) {
        const auto& a=p.associations[i];if(!Extension(a.extension) || !Application(a.application))return false;
        for(size_t j=0;j<i;++j)if(p.associations[j].extension==a.extension)return false;
    }
    return true;
}
ShellPreferenceStore::Status ShellPreferenceStore::Load(const rt::Vfs& vfs,ShellPreferences& out,rt::FileCopyIo* custom) {
    out={};generation_=0;slot_=-1;bool corrupt=false,ioError=false,unsupported=false;
    rt::FileCopyIo native;auto& io=custom?*custom:native;
    for(int slot=0;slot<2;++slot) {
        size_t budget=4096;const auto host=vfs.ToHost(Directory(vfs)+"\\prefs-"+std::to_string(slot)+".bin",false,&budget);
        if(host.empty()){ioError=true;continue;}
        int fd=io.OpenRead(host);if(fd<0){if(errno!=ENOENT)ioError=true;continue;}
        Bytes bytes;char chunk[8192];int failure=0;
        for(;;) {
            const auto n=io.Read(fd,chunk,sizeof(chunk));
            if(n<0){if(errno==EINTR)continue;failure=errno?errno:EIO;break;}
            if(!n)break;
            if(size_t(n)>MaxBytes-bytes.size()){corrupt=true;break;}
            bytes.insert(bytes.end(),chunk,chunk+n);
        }
        if(io.Close(fd)!=0 && !failure)failure=errno?errno:EIO;
        if(failure){ioError=true;continue;}
        if(bytes.size()>=6 && std::memcmp(bytes.data(),"SRPREF",6)==0 && bytes.size()>=8 && std::memcmp(bytes.data(),Magic,8)!=0){unsupported=true;continue;}
        ShellPreferences p;uint64_t gen=0;
        if(!Decode(bytes,p,gen)){corrupt=true;continue;}
        if(gen>generation_){generation_=gen;slot_=slot;out=std::move(p);}
    }
    if(unsupported)return Status::Unsupported;
    if(ioError)return Status::IoError;
    if(slot_>=0)return corrupt?Status::Recovered:Status::Loaded;
    return corrupt?Status::Corrupt:Status::Missing;
}
bool ShellPreferenceStore::Save(const rt::Vfs& vfs,const ShellPreferences& prefs,rt::FileCopyIo* custom) {
    if(!Valid(prefs)){errno=EINVAL;return false;}
    ShellPreferences ignored;const auto load=Load(vfs,ignored,custom);
    if(load==Status::Unsupported || load==Status::IoError){errno=load==Status::Unsupported?ENOTSUP:EIO;return false;}
    if(generation_==std::numeric_limits<uint64_t>::max()){errno=EOVERFLOW;return false;}
    const auto directory=Directory(vfs);size_t budget=4096;
    if(!vfs.CreateDirectory(directory,&budget) && errno!=EEXIST)return false;
    const int target=slot_==0?1:0;const auto host=vfs.ToHost(directory+"\\prefs-"+std::to_string(target)+".bin",true,&budget);
    if(host.empty())return false;
    const auto bytes=Encode(prefs,generation_+1);rt::FileCopyIo native;auto& io=custom?*custom:native;
    const int fd=io.OpenReplace(host);if(fd<0)return false;
    int failure=0;size_t at=0;
    while(at<bytes.size()) {
        const auto n=io.Write(fd,bytes.data()+at,bytes.size()-at);
        if(n<0 && errno==EINTR)continue;
        if(n<=0){failure=n<0 && errno?errno:EIO;break;}at+=size_t(n);
    }
    if(!failure && io.Flush(fd)!=0)failure=errno?errno:EIO;
    if(io.Close(fd)!=0 && !failure)failure=errno?errno:EIO;
    if(failure){errno=failure;return false;}
    ++generation_;slot_=target;return true;
}
} // namespace ui
