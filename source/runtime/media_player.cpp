#include "media_player.h"
#include "audio_output.h"
#include <algorithm>
namespace rt {
namespace {uint64_t nextMediaClient=0x1000000000000000ull;}
MediaPlayer::MediaPlayer()=default;MediaPlayer::~MediaPlayer(){Stop();}
bool MediaPlayer::Owns() const {return client_ && Audio().Owner()==owner_ && Audio().Client()==client_;}
bool MediaPlayer::Fail(const char* text){error_=text;return false;}
bool MediaPlayer::Open(const Vfs& vfs,const std::string& path) {
    auto decoder=std::make_unique<AudioDecoder>();
    if(!decoder->Open(vfs,path))return Fail(decoder->Error());
    if(!Stop())return false;
    decoder_=std::move(decoder);path_=vfs.FullGuestPath(path);state_=State::Ready;error_.clear();return true;
}
bool MediaPlayer::Play(bool startPaused) {
    if(!decoder_)return Fail("Open an MP3 or PCM WAV file");
    if(state_==State::Playing && Owns())return true;
    if(state_==State::Paused && Owns()){
        if(startPaused)return true;
        if(!Audio().Pause(owner_,client_,false)){state_=State::Error;return Fail("Audio restart failed; Stop to recover");}
        state_=State::Playing;error_.clear();return true;
    }
    if(client_ && !Stop())return false;
    if(!Audio().Available())return Fail("Audio device unavailable");
    if(Audio().Client() || Audio().Pending())return Fail("Audio device busy with another source");
    if(state_==State::Finished){if(!decoder_->Seek(0))return Fail(decoder_->Error());offset_=lastPosition_=0;}
    if(nextMediaClient==UINT64_MAX)return Fail("Audio client IDs exhausted");
    owner_=Audio().Owner();client_=nextMediaClient++;
    if(!Audio().Claim(owner_,client_)){client_=0;return Fail("Audio device busy");}
    if(!Audio().Position(owner_,client_,positionBase_)){
        Audio().ReleaseClient(owner_,client_);client_=0;return Fail("Device playback position unavailable");}
    retirement_=std::make_shared<Retirement>();submitted_=0;eof_=false;state_=State::Playing;error_.clear();
    if(startPaused){if(!Audio().Pause(owner_,client_,true)){state_=State::Error;return Fail("Audio pause failed; Stop to recover");}state_=State::Paused;}
    Tick();return state_==State::Playing || state_==State::Paused || state_==State::Finished;
}
bool MediaPlayer::Pause() {
    if(state_!=State::Playing || !Owns())return Fail("No playing source");
    if(!Audio().Pause(owner_,client_,true)){state_=State::Error;return Fail("Audio pause failed; Stop to recover");}
    state_=State::Paused;error_.clear();return true;
}
bool MediaPlayer::Stop() {
    // A newer owner may still restore this outer claim. Keep its lease until
    // the caller returns; clearing it here would strand a restored client.
    if(client_ && !Owns() && Audio().Owner()>owner_)return Fail("Stop after the audio session returns");
    if(Owns()){
        if(!Audio().Reset()){state_=State::Error;return Fail("Audio reset failed; buffers retained");}
        Audio().ReleaseClient(owner_,client_);
    }
    client_=owner_=0;retirement_.reset();previews_.clear();eof_=false;submitted_=offset_=lastPosition_=0;
    if(decoder_ && !decoder_->Seek(0)){state_=State::Error;return Fail(decoder_->Error());}
    state_=decoder_?State::Ready:State::Empty;error_.clear();return true;
}
bool MediaPlayer::Close(){if(!Stop())return false;decoder_.reset();path_.clear();state_=State::Empty;return true;}
bool MediaPlayer::Seek(uint64_t frame) {
    if(!decoder_ || frame>Duration())return Fail("Invalid playback position");
    const auto previous=state_;if(!Stop())return false;
    if(!decoder_->Seek(frame)){state_=State::Error;return Fail(decoder_->Error());}
    offset_=lastPosition_=frame;
    if(previous==State::Playing || previous==State::Paused)return Play(previous==State::Paused);
    return true;
}
uint64_t MediaPlayer::Position() const {
    if(state_==State::Finished)return Duration();
    uint64_t frames=0;
    if(Owns() && Audio().Position(owner_,client_,frames) && frames>=positionBase_)
        lastPosition_=std::min(Duration(),offset_+std::min(frames-positionBase_,submitted_));
    return lastPosition_;
}
bool MediaPlayer::Visualization(std::array<int16_t,512>& stereo) const {
    stereo.fill(0);
    if(!Owns() || (state_!=State::Playing && state_!=State::Paused))return false;
    const auto position=Position();bool found=false;
    for(size_t n=0;n<stereo.size()/2;++n)for(const auto& block:previews_){
        const auto frame=position+n;
        if(!block->retired && frame>=block->start && frame-block->start<block->pcm.size()/2){
            const auto at=size_t(frame-block->start)*2;stereo[n*2]=block->pcm[at];stereo[n*2+1]=block->pcm[at+1];found=true;break;
        }
    }
    return found;
}
unsigned MediaPlayer::Volume() const {uint16_t value=0;return Audio().Volume(value)?unsigned((uint32_t(value)*100+32767)/65535):0;}
bool MediaPlayer::SetVolume(unsigned percent) {
    if(percent>100)return Fail("Volume must be 0 to 100");
    if(Audio().Client() && !Owns())return Fail("Audio device busy with another source");
    if(!Audio().SetVolume(Audio().Owner(),Owns()?client_:0,uint16_t(percent*65535/100)))return Fail("Device volume unavailable");
    error_.clear();return true;
}
bool MediaPlayer::Tick() {
    if(!client_)return false;
    if(!Owns()){const bool changed=state_!=State::Error;state_=State::Error;Fail("Audio session changed; press Stop");return changed;}
    if(state_!=State::Playing && state_!=State::Paused)return false;
    const auto before=lastPosition_;uint64_t raw=0;
    if(!Audio().Position(owner_,client_,raw) || raw<positionBase_){state_=State::Error;Fail("Audio position failed; Stop to recover");return true;}
    if(retirement_ && retirement_->cancelled){state_=State::Error;Fail("Audio was cancelled; press Stop");return true;}
    if(!Audio().Poll()){state_=State::Error;Fail("Audio device failed; Stop to recover");return true;}
    previews_.erase(std::remove_if(previews_.begin(),previews_.end(),[](const auto& block){return block->retired;}),previews_.end());
    if(state_==State::Paused)return Position()!=before;
    // At most four bounded decode/submission operations per owner tick.
    for(size_t n=0;n<AudioOutput::Slots && !eof_ && Audio().Pending()<AudioOutput::Slots;++n){
        std::vector<int16_t> pcm;if(!decoder_->Read(pcm)){state_=State::Error;Fail(decoder_->Error());return true;}
        if(pcm.empty()){eof_=true;break;}
        const auto frames=pcm.size()/2;std::weak_ptr<Retirement> weak=retirement_;
        auto preview=std::make_shared<Preview>();preview->start=offset_+submitted_;std::weak_ptr<Preview> weakPreview=preview;
        const auto result=Audio().Submit(owner_,pcm,[weak,frames,weakPreview](bool cancelled){if(auto block=weakPreview.lock())block->retired=true;if(auto state=weak.lock()){if(cancelled)state->cancelled+=frames;else state->completed+=frames;}},client_);
        if(result!=AudioOutput::Result::Accepted){state_=State::Error;Fail("Audio submission failed; Stop to recover");return true;}
        previews_.erase(std::remove_if(previews_.begin(),previews_.end(),[](const auto& block){return block->retired;}),previews_.end());
        preview->pcm=std::move(pcm);previews_.push_back(std::move(preview));submitted_+=frames;
    }
    if(eof_ && !Audio().Pending()){
        Audio().ReleaseClient(owner_,client_);client_=owner_=0;state_=State::Finished;lastPosition_=Duration();return true;}
    return Position()!=before;
}
}
