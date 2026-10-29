#pragma once
#include "audio_decoder.h"
#include <memory>
#include <array>
#include <deque>
#include <string>
namespace rt {
// Native and HLE clients share this real playback engine. One physical output
// is admitted exclusively until a genuine multi-source mixer exists.
class MediaPlayer {
public:
    enum class State {Empty,Ready,Playing,Paused,Finished,Error};
    MediaPlayer();~MediaPlayer();
    MediaPlayer(const MediaPlayer&)=delete;MediaPlayer& operator=(const MediaPlayer&)=delete;
    bool Open(const Vfs&,const std::string&);
    bool Play(bool startPaused=false);bool Pause();bool Stop();bool Close();bool Seek(uint64_t frames);
    bool Tick();
    bool SetVolume(unsigned percent);
    unsigned Volume() const;
    // Actual PCM at the device playback cursor, not the most recently decoded block.
    bool Visualization(std::array<int16_t,512>& stereo) const;
    uint64_t Position() const;
    uint64_t Duration() const {return decoder_?decoder_->Duration():0;}
    uint32_t SampleRate() const {return decoder_?decoder_->SampleRate():0;}
    uint64_t SourceFrameCount() const {return decoder_?decoder_->SourceFrameCount():0;}
    State Status() const {return state_;}
    const std::string& Path() const {return path_;}
    const std::string& Error() const {return error_;}
    bool Loaded() const {return bool(decoder_);}
    bool Mp3() const {return decoder_ && decoder_->IsMp3();}
private:
    struct Preview {uint64_t start=0;std::vector<int16_t> pcm;bool retired=false;};
    std::deque<std::shared_ptr<Preview>> previews_;
    struct Retirement {uint64_t completed=0,cancelled=0;};
    bool Fail(const char* text);
    bool Owns() const;
    std::unique_ptr<AudioDecoder> decoder_;
    std::shared_ptr<Retirement> retirement_;
    uint64_t owner_=0,client_=0,positionBase_=0,offset_=0,submitted_=0;
    mutable uint64_t lastPosition_=0;
    bool eof_=false;
    State state_=State::Empty;
    std::string path_,error_;
};
}
