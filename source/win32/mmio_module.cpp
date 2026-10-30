#include "hle_util.h"
#include "../runtime/files.h"
#include <algorithm>
#include <cstdio>
#include <map>
namespace win32 {namespace {
using namespace hle;
constexpr uint32_t Riff=0x46464952,List=0x5453494c;
constexpr size_t MaxFile=8*1024*1024,MaxTotal=32*1024*1024;
struct File {std::vector<uint8_t> bytes;size_t cursor=0;};
struct Files {std::map<uint32_t,File> files;size_t bytes=0;};
uint32_t nextHandle=0x73000000;
Files& State(){return rt::LocalState<Files>();}
bool Range(cpu::IGuestMemory& m,uint32_t p,size_t n){return p && uint64_t(p)+n<=0x100000000ull && m.IsRangeMapped(p,n);}
uint32_t U32(const File& f,size_t at){uint32_t v=0;for(unsigned n=0;n<4;++n)v|=uint32_t(f.bytes[at+n])<<(n*8);return v;}
File* Find(uint32_t h){auto& files=State().files;auto it=files.find(h);return it==files.end()?nullptr:&it->second;}
HostCallOutcome Open(cpu::CpuState& c,cpu::IGuestMemory& m,bool wide){
    const auto path=Arg(c,m,0),info=Arg(c,m,1),flags=Arg(c,m,2);
    auto fail=[&](uint32_t error){if(info && Range(m,info,72))m.Write32(info+12,error);return Ret(0);};
    if(!rt::g_process || !path || (info && !Range(m,info,72)))return fail(11);
    // Read-only disk snapshots. Custom I/O procedures/memory buffers/writes are
    // explicit unsupported requests, never success-shaped fake HMMIO handles.
    if(flags&~uint32_t(0x10000))return fail(8);
    if(info)for(unsigned n=0;n<72;n+=4)if(m.Read32(info+n))return fail(8);
    auto& state=State();if(state.files.size()>=16 || nextHandle==UINT32_MAX)return fail(271);
    std::string name;std::u16string text;bool ended=false;
    for(unsigned n=0;n<4096;++n){const uint64_t at=uint64_t(path)+n*(wide?2:1);if(at>UINT32_MAX || !Range(m,uint32_t(at),wide?2:1))return fail(11);
        const auto ch=wide?m.Read16(uint32_t(at)):m.Read8(uint32_t(at));if(!ch){ended=true;break;}if(wide)text.push_back(char16_t(ch));else name.push_back(char(ch));}
    if(!ended)return fail(11);
    name=wide?WideToUtf8(text):AnsiPathToUtf8(name);
    auto& vfs=rt::FileSystem();rt::FileProperties props;
    if(!vfs.QueryProperties(name,props) || props.kind!=rt::FileProperties::Kind::File)return fail(257);
    if(props.size>MaxFile || props.size>MaxTotal-state.bytes)return fail(258);
    const auto host=vfs.ToHost(name);FILE* disk=host.empty()?nullptr:std::fopen(host.c_str(),"rb");if(!disk)return fail(259);
    File file;file.bytes.resize(size_t(props.size));const auto count=std::fread(file.bytes.data(),1,file.bytes.size(),disk);
    const int extra=std::fgetc(disk);const bool error=std::ferror(disk)!=0;const int closed=std::fclose(disk);
    if(count!=file.bytes.size() || extra!=EOF || error || closed)return fail(261);
    const auto handle=nextHandle++;state.bytes+=file.bytes.size();state.files.emplace(handle,std::move(file));
    if(info)m.Write32(info+12,0);
    return Ret(handle);
}
bool Chunk(const File& file,cpu::IGuestMemory& m,uint32_t p,size_t& data,size_t& end,uint32_t& id){
    if(!Range(m,p,20))return false;
    id=m.Read32(p);data=m.Read32(p+12);const uint64_t after=uint64_t(data)+m.Read32(p+4);
    if(data<8 || after>file.bytes.size() || U32(file,data-8)!=id || U32(file,data-4)!=m.Read32(p+4))return false;
    if((id==Riff || id==List) && (after-data<4 || U32(file,data)!=m.Read32(p+8)))return false;
    end=size_t(after);return true;
}
#define FN(name) HostCallOutcome name(cpu::CpuState& c,cpu::IGuestMemory& m)
FN(mmioOpenA){return Open(c,m,false);}FN(mmioOpenW){return Open(c,m,true);}
FN(mmioClose){const auto h=Arg(c,m,0);auto* file=Find(h);if(!file)return Ret(5);if(Arg(c,m,1))return Ret(11);auto& s=State();s.bytes-=file->bytes.size();s.files.erase(h);return Ret(0);}
FN(mmioRead){auto* f=Find(Arg(c,m,0));const auto output=Arg(c,m,1);const int32_t count=int32_t(Arg(c,m,2));if(!f || count<0 || (count && !Range(m,output,size_t(count))))return Ret(UINT32_MAX);
    const auto n=std::min(size_t(count),f->bytes.size()-f->cursor);for(size_t i=0;i<n;++i)m.Write8(output+uint32_t(i),f->bytes[f->cursor+i]);f->cursor+=n;return Ret(uint32_t(n));}
FN(mmioDescend){auto* f=Find(Arg(c,m,0));const auto output=Arg(c,m,1),parent=Arg(c,m,2),flags=Arg(c,m,3);
    if(!f)return Ret(5);
    if(!Range(m,output,20))return Ret(11);
    if(flags && flags!=16 && flags!=32 && flags!=64)return Ret(8);
    size_t begin=0,end=f->bytes.size();uint32_t parentId=0;
    if(parent){if(!Chunk(*f,m,parent,begin,end,parentId) || (parentId!=Riff && parentId!=List))return Ret(272);begin+=4;}
    size_t at=std::max(begin,f->cursor);const auto wantedId=m.Read32(output),wantedType=m.Read32(output+8);
    while(at<end){
        if(end-at<8)return Ret(272);
        const auto id=U32(*f,at),size=U32(*f,at+4);const uint64_t after=uint64_t(at)+8+size,padded=after+(size&1);
        if(after>end || padded>end || ((id==Riff || id==List) && size<4))return Ret(272);
        const auto type=(id==Riff || id==List)?U32(*f,at+8):0;
        if(!flags || (flags==16 && id==wantedId) || (flags==32 && id==Riff && type==wantedType) || (flags==64 && id==List && type==wantedType)){
            m.Write32(output,id);m.Write32(output+4,size);m.Write32(output+8,type);m.Write32(output+12,uint32_t(at+8));m.Write32(output+16,0);f->cursor=at+8+((id==Riff || id==List)?4:0);return Ret(0);}
        at=size_t(padded);
    }
    return Ret(265);
}
FN(mmioAscend){auto* f=Find(Arg(c,m,0));if(!f)return Ret(5);if(Arg(c,m,2))return Ret(8);size_t data=0,end=0;uint32_t id=0;
    if(!Chunk(*f,m,Arg(c,m,1),data,end,id) || end+(uint64_t(end-data)&1)>f->bytes.size())return Ret(272);
    f->cursor=end+((end-data)&1);return Ret(0);}
#define E(name,n) {"WINMM.dll",#name,n,CallConv::Stdcall,&name}
const HostFunctionEntry entries[]={E(mmioOpenA,12),E(mmioOpenW,12),E(mmioClose,8),E(mmioRead,12),E(mmioDescend,16),E(mmioAscend,12)};
const bool registered=RegisterHostFunctions(entries,sizeof(entries)/sizeof(entries[0]));
}}
