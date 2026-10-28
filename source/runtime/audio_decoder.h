#pragma once
#include "files.h"
#include <memory>
#include <vector>
namespace rt {
// Bounded compressed input; streaming PCM. VFS paths and owned file snapshot.
class AudioDecoder {
public:
    static constexpr size_t MaxBytes=32*1024*1024,SourceFrames=4096;
    AudioDecoder();~AudioDecoder();
    AudioDecoder(const AudioDecoder&)=delete;
    AudioDecoder& operator=(const AudioDecoder&)=delete;
    bool Open(const Vfs&,const std::string&);
    bool OpenBytes(std::vector<uint8_t>);
    // One bounded source block (4096 frames), possibly zero at EOF.
    bool Read(std::vector<int16_t>&);
    bool Seek(uint64_t canonicalFrame);
    uint64_t Duration() const;
    bool IsMp3() const;
    const char* Error() const {return error_;}
private:
    struct Impl;std::unique_ptr<Impl> impl_;
    const char* error_="No audio file";
};
}
