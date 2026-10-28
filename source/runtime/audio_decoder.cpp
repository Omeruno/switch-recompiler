#include "audio_decoder.h"
#include "pcm_stream.h"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <cstdio>
#define DR_MP3_NO_STDIO
#define DR_MP3_IMPLEMENTATION
#include "../../third_party/dr_libs/dr_mp3.h"
namespace rt {
namespace {
uint16_t U16(const std::vector<uint8_t>& b,size_t n){return uint16_t(b[n])|uint16_t(b[n+1])<<8;}
uint32_t U32(const std::vector<uint8_t>& b,size_t n){return uint32_t(U16(b,n))|uint32_t(U16(b,n+2))<<16;}
}
struct AudioDecoder::Impl {
    std::vector<uint8_t> bytes;
    drmp3 mp3{};bool initialized=false,ended=false;
    PcmFormat format{};PcmStream converter;
    size_t data=0,length=0,cursor=0;
    uint64_t frames=0;
    ~Impl(){if(initialized)drmp3_uninit(&mp3);}
};
AudioDecoder::AudioDecoder()=default;AudioDecoder::~AudioDecoder()=default;
uint64_t AudioDecoder::Duration() const {return impl_?impl_->frames*48000/impl_->format.rate:0;}
bool AudioDecoder::IsMp3() const {return impl_ && impl_->initialized;}
bool AudioDecoder::Open(const Vfs& vfs,const std::string& path) {
    FileProperties props;
    if(!vfs.QueryProperties(path,props) || props.kind!=FileProperties::Kind::File){error_="Audio file unavailable";return false;}
    if(!props.size || props.size>MaxBytes){error_="Audio file exceeds 32 MiB";return false;}
    const auto host=vfs.ToHost(path);if(host.empty()){error_="Audio path unavailable";return false;}
    FILE* file=std::fopen(host.c_str(),"rb");if(!file){error_="Audio file read failed";return false;}
    std::vector<uint8_t> bytes(size_t(props.size));const auto count=std::fread(bytes.data(),1,bytes.size(),file);
    const int extra=std::fgetc(file);const bool readError=std::ferror(file)!=0;const int close=std::fclose(file);
    if(count!=bytes.size() || extra!=EOF || readError || close){error_="Audio file changed or read failed";return false;}
    return OpenBytes(std::move(bytes));
}
bool AudioDecoder::OpenBytes(std::vector<uint8_t> bytes) {
    if(bytes.empty() || bytes.size()>MaxBytes){error_="Audio size invalid";return false;}
    auto next=std::make_unique<Impl>();next->bytes=std::move(bytes);auto& b=next->bytes;
    if(b.size()>=12 && !std::memcmp(b.data(),"RIFF",4) && !std::memcmp(b.data()+8,"WAVE",4)) {
        const uint64_t end=uint64_t(U32(b,4))+8;bool fmt=false,data=false;
        if(end<12 || end>b.size()){error_="Invalid WAV size";return false;}
        for(size_t p=12;p<end;){
            if(end-p<8){error_="Truncated WAV chunk";return false;}
            const auto n=U32(b,p+4);const uint64_t after=uint64_t(p)+8+n+(n&1);
            if(after>end){error_="Truncated WAV chunk";return false;}
            if(!std::memcmp(b.data()+p,"fmt ",4)){
                if(fmt || n<16 || U16(b,p+8)!=1){error_="Only PCM WAV is supported";return false;}
                next->format={U32(b,p+12),U16(b,p+10),U16(b,p+22)};
                if(!next->format.Valid() || U16(b,p+20)!=next->format.Align() || U32(b,p+16)!=next->format.rate*next->format.Align()){error_="Invalid PCM format";return false;}
                fmt=true;
            }else if(!std::memcmp(b.data()+p,"data",4)){
                if(data){error_="Duplicate WAV data";return false;}
                next->data=p+8;next->length=n;data=true;
            }
            p=size_t(after);
        }
        if(!fmt || !data || !next->length || next->length%next->format.Align()){error_="Invalid WAV data";return false;}
        next->frames=next->length/next->format.Align();
    }else{
        if(!drmp3_init_memory(&next->mp3,b.data(),b.size(),nullptr)){error_="Unsupported or invalid MP3/WAV";return false;}
        next->initialized=true;next->format={next->mp3.sampleRate,uint16_t(next->mp3.channels),16};
        if(!next->format.Valid()){error_="Unsupported MP3 format";return false;}
        next->frames=drmp3_get_pcm_frame_count(&next->mp3);
        if(!next->frames || !drmp3_seek_to_pcm_frame(&next->mp3,0)){error_="Invalid MP3 duration";return false;}
    }
    if(next->frames>uint64_t(next->format.rate)*7200){error_="Audio duration exceeds two hours";return false;}
    next->converter=PcmStream(next->format);impl_=std::move(next);error_="";return true;
}
bool AudioDecoder::Read(std::vector<int16_t>& out) {
    out.clear();if(!impl_){error_="No audio file";return false;}auto& s=*impl_;
    if(s.ended)return true;
    std::vector<int16_t> pcm;size_t frames=0;
    if(s.initialized){pcm.resize(SourceFrames*s.format.channels);frames=size_t(drmp3_read_pcm_frames_s16(&s.mp3,SourceFrames,pcm.data()));pcm.resize(frames*s.format.channels);}
    else {
        frames=std::min(size_t(SourceFrames),(s.length-s.cursor)/s.format.Align());pcm.reserve(frames*s.format.channels);
        const auto count=frames*s.format.Align();
        for(size_t n=0;n<count;n+=s.format.bits/8){const auto p=s.data+s.cursor+n;const uint32_t v=s.format.bits==8?s.bytes[p]:U16(s.bytes,p);
            pcm.push_back(s.format.bits==8?int16_t((int32_t(v)-128)*256):int16_t(int32_t(v)-(v&0x8000?65536:0)));}
        s.cursor+=count;
    }
    s.ended=frames<SourceFrames;
    if(!s.converter.Push(pcm,s.ended,out)){error_="Audio conversion failed";return false;}
    return true;
}
bool AudioDecoder::Seek(uint64_t frame) {
    if(!impl_ || frame>Duration()){error_="Invalid audio position";return false;}auto& s=*impl_;
    if(frame==Duration()){s.ended=true;s.cursor=s.length;s.converter=PcmStream(s.format);error_="";return true;}
    const auto source=frame*s.format.rate/48000;
    if(s.initialized && !drmp3_seek_to_pcm_frame(&s.mp3,source)){error_="MP3 seek failed";return false;}
    s.cursor=size_t(source*s.format.Align());s.converter=PcmStream(s.format);s.ended=false;error_="";return true;
}
}
