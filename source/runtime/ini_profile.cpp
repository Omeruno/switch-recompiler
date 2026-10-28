#include "ini_profile.h"
#include <algorithm>
#include <cerrno>
#include <set>
namespace rt {namespace {
using Text=std::u16string;
bool Space(char16_t c){return c==' ' || c=='\t';}
Text Trim(Text s){while(!s.empty() && Space(s.back()))s.pop_back();size_t n=0;while(n<s.size() && Space(s[n]))++n;return s.substr(n);}
Text Fold(Text s){for(auto& c:s)if(c>='A' && c<='Z')c+=32;return s;}
bool Section(const Text& line,Text& name){const auto s=Trim(line);if(s.size()<2 || s[0]!='[')return false;const auto end=s.find(']');if(end==Text::npos)return false;name=Trim(s.substr(1,end-1));return true;}
bool Pair(const Text& line,Text& key,Text& value){const auto s=Trim(line);if(s.empty() || s[0]==';' || s[0]=='[')return false;const auto at=s.find('=');if(at==Text::npos)return false;key=Trim(s.substr(0,at));value=Trim(s.substr(at+1));return !key.empty();}
bool Field(const Text& s,bool key){return !s.empty() && s.size()<=IniProfile::MaxLine && s.find_first_of(key?u"\r\n=[]":u"\r\n[]")==Text::npos && s==Trim(s);}
}
uint32_t ProfileIoError(){switch(errno){case ENOENT:return 2;case ENOTDIR:return 3;case EACCES:case EPERM:return 5;case EROFS:return 19;case ENODEV:return 15;case ENOSPC:return 112;case EEXIST:return 80;case ENAMETOOLONG:return 206;case EOVERFLOW:return 223;case EINVAL:return 87;default:return 1117;}}
bool IniProfile::ValidUtf16(const Text& s){for(size_t i=0;i<s.size();++i){const auto c=s[i];if(c>=0xd800 && c<=0xdbff){if(++i==s.size() || s[i]<0xdc00 || s[i]>0xdfff)return false;}else if(c>=0xdc00 && c<=0xdfff)return false;}return true;}
uint32_t IniProfile::Load(const Vfs& vfs,const std::string& path,FileCopyIo* custom){
    lines_.clear();unicode_=false;size_t budget=4096;const auto host=vfs.ToHost(path,false,&budget);if(host.empty())return ProfileIoError();
    FileProperties props;if(!vfs.QueryProperties(path,props,&budget)){if(errno==ENOENT)return 0;return ProfileIoError();}
    if(props.kind!=FileProperties::Kind::File)return 5;
    if(props.size>MaxBytes)return 223;
    FileCopyIo native;auto& io=custom?*custom:native;const int fd=io.OpenRead(host);if(fd<0)return ProfileIoError();
    std::vector<uint8_t> bytes;uint8_t chunk[8192];uint32_t error=0;
    for(;;){const auto n=io.Read(fd,chunk,sizeof(chunk));if(n<0 && errno==EINTR)continue;if(n<0){error=ProfileIoError();break;}if(!n)break;
        if(size_t(n)>sizeof(chunk) || size_t(n)>MaxBytes-bytes.size()){error=223;break;}bytes.insert(bytes.end(),chunk,chunk+n);}
    if(io.Close(fd)!=0 && !error)error=ProfileIoError();
    if(error)return error;
    Text text;size_t at=0;if(bytes.size()>=2 && bytes[0]==255 && bytes[1]==254){unicode_=true;at=2;if((bytes.size()-2)%2)return 13;}
    while(at<bytes.size()){uint16_t c=bytes[at++];if(unicode_){c|=uint16_t(bytes[at++])<<8;}else if(c>127)return 1113;if(!c)return 13;text.push_back(char16_t(c));}
    if(!ValidUtf16(text))return 1113;
    size_t start=0;for(size_t i=0;i<=text.size();++i)if(i==text.size() || text[i]=='\r' || text[i]=='\n'){
        if(i-start>MaxLine || lines_.size()>=MaxLines)return 223;
        if(i>start || i<text.size())lines_.push_back(text.substr(start,i-start));
        if(i<text.size() && text[i]=='\r' && i+1<text.size() && text[i+1]=='\n')++i;
        start=i+1;
    }
    return 0;
}
Text IniProfile::Read(const Text& section,const Text& key,const Text& fallback) const {
    const auto wantedSection=Fold(section),wantedKey=Fold(key);
    Text current,k,v;bool active=false;
    for(const auto& line:lines_){if(Section(line,current)){active=Fold(current)==wantedSection;continue;}
        if(active && Pair(line,k,v) && Fold(k)==wantedKey){if(v.size()>=2 && (v.front()=='\'' || v.front()=='"') && v.back()==v.front())return v.substr(1,v.size()-2);return v;}}
    auto result=fallback;while(!result.empty() && result.back()==' ')result.pop_back();return result;
}
std::vector<Text> IniProfile::List(const Text* section,bool entries) const {
    const auto wanted=section?Fold(*section):Text{};
    std::vector<Text> result;std::set<Text> seen;Text current,k,v;bool active=false;
    for(const auto& line:lines_){if(Section(line,current)){active=section && Fold(current)==wanted;if(!section && !current.empty() && seen.insert(Fold(current)).second)result.push_back(current);continue;}
        if(active && Pair(line,k,v) && seen.insert(Fold(k)).second)result.push_back(entries?k+u"="+v:k);}
    return result;
}
uint32_t IniProfile::Integer(const Text& text){size_t at=0;while(at<text.size() && Space(text[at]))++at;bool negative=false;if(at<text.size() && (text[at]=='+' || text[at]=='-'))negative=text[at++]=='-';unsigned base=10;
    if(at+1<text.size() && text[at]=='0' && (text[at+1]=='x' || text[at+1]=='X')){base=16;at+=2;}
    uint32_t value=0;for(;at<text.size();++at){const auto c=text[at];const unsigned digit=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:16;if(digit>=base)break;value=value*base+digit;}return negative?0u-value:value;
}
uint32_t IniProfile::Write(const Vfs& vfs,const std::string& path,const Text& section,const Text* key,const Text* value,FileCopyIo* custom){
    if(!Field(section,false) || (key && !Field(*key,true)) || (value && (value->size()>MaxLine || value->find_first_of(u"\r\n")!=Text::npos)))return 87;
    if(!ValidUtf16(section) || (key && !ValidUtf16(*key)) || (value && !ValidUtf16(*value)))return 1113;
    const auto error=Load(vfs,path,custom);if(error)return error;
    size_t budget=4096;const auto host=vfs.ToHost(path,true,&budget);if(host.empty())return ProfileIoError();
    FileProperties props;if(vfs.QueryProperties(path,props,&budget)){if(props.kind!=FileProperties::Kind::File || !props.ownerWriteBit)return 5;}else if(errno!=ENOENT)return ProfileIoError();
    const auto wantedSection=Fold(section),wantedKey=key?Fold(*key):Text{};
    std::vector<Text> next;next.reserve(lines_.size()+2);
    Text current,k,v;bool active=false,updated=false;
    for(const auto& line:lines_){
        const bool header=Section(line,current);
        if(header)active=Fold(current)==wantedSection;
        if(active && !key)continue;
        if(active && !header && Pair(line,k,v) && Fold(k)==wantedKey){
            if(!value)continue;
            if(!updated){next.push_back(*key+u"="+*value);updated=true;continue;}
        }
        next.push_back(line);
    }
    if(key && value && !updated){
        bool exists=false;size_t end=next.size();for(size_t i=0;i<next.size();++i)if(Section(next[i],current)){if(exists){end=i;break;}if(Fold(current)==wantedSection)exists=true;}
        if(!exists)next.push_back(u"["+section+u"]");
        next.insert(next.begin()+(exists?end:next.size()),*key+u"="+*value);
    }
    if(next==lines_)return 0;
    if(next.size()>MaxLines)return 223;
    std::vector<uint8_t> bytes;if(unicode_)bytes={255,254};
    for(const auto& line:next){if(line.size()>MaxLine)return 223;for(char16_t c:line){if(!unicode_ && c>127)return 1113;bytes.push_back(uint8_t(c));if(unicode_)bytes.push_back(uint8_t(c>>8));}bytes.push_back('\r');if(unicode_)bytes.push_back(0);bytes.push_back('\n');if(unicode_)bytes.push_back(0);if(bytes.size()>MaxBytes)return 223;}
    FileCopyIo native;auto& io=custom?*custom:native;std::string stage;int fd=-1;
    for(unsigned attempt=0;attempt<32;++attempt){stage=host+".sr-profile-"+std::to_string(attempt);fd=io.CreateExclusive(stage);if(fd>=0)break;if(errno!=EEXIST)return ProfileIoError();}
    if(fd<0)return 80;
    uint32_t failure=0;size_t at=0;while(at<bytes.size()){const auto n=io.Write(fd,bytes.data()+at,bytes.size()-at);if(n<0 && errno==EINTR)continue;if(n<=0 || size_t(n)>bytes.size()-at){failure=n<0?ProfileIoError():1117;break;}at+=size_t(n);}
    if(!failure && io.Flush(fd)!=0)failure=ProfileIoError();
    if(io.Close(fd)!=0 && !failure)failure=ProfileIoError();
    // Revalidate VFS policy before native publication; never unlink destination.
    if(!failure){size_t checkBudget=4096;if(vfs.ToHost(path,true,&checkBudget)!=host)failure=5;}
    if(!failure && io.Rename(stage,host)!=0)failure=ProfileIoError();
    if(failure){io.Remove(stage);return failure;}
    lines_=std::move(next);return 0;
}
} // namespace rt
