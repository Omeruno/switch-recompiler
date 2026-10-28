#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <functional>
#include <vector>
namespace rt {
// Immutable canonical 48kHz stereo signed16 PCM, 4096-aligned/padded for audout.
class AudioBlock {
public:
    explicit AudioBlock(const std::vector<int16_t>&);
    const void* Data() const {return storage_.data()+offset_;}
    size_t Bytes() const {return bytes_;}
    size_t Capacity() const {return padded_;}
private:
    std::vector<uint8_t> storage_;
    size_t offset_=0,bytes_=0,padded_=0;
};
class AudioDevice {
public:
    enum class Completion {None,Released,Error};
    virtual ~AudioDevice()=default;
    virtual bool Available() const=0;
    virtual const char* Name() const=0;
    // Device retains shared ownership until release/reset, even on ambiguous error.
    virtual bool Submit(uint64_t,std::shared_ptr<const AudioBlock>)=0;
    virtual Completion Poll(uint64_t&)=0;
    // TRUE confirms all previously borrowed buffers revoked; readiness is separate.
    virtual bool Reset()=0;
    // Optional genuine device controls. Unsupported devices must refuse.
    virtual bool SetPaused(bool) {return false;}
    virtual bool Position(uint64_t&) {return false;}
    virtual bool VolumeSupported() const {return false;}
    virtual bool SetVolume(uint16_t) {return false;}
    virtual bool Volume(uint16_t&) {return false;}
};
class AudioOutput {
public:
    static constexpr size_t Slots=4,MaxFrames=48000*5,MaxSamples=MaxFrames*2;
    enum class Result {Accepted,Busy,Unavailable,WrongOwner,Invalid,DeviceError};
    ~AudioOutput();
    bool Bind(AudioDevice*);
    uint64_t Enter();
    void Leave(uint64_t owner,uint64_t previous,uint64_t previousClient=0);
    uint64_t Owner() const {return owner_;}
    bool Available() const {return owner_ && device_ && !fault_ && device_->Available();}
    const char* Name() const {return device_?device_->Name():"";}
    // Host-only retirement observer: FALSE=played release, TRUE=confirmed cancel.
    // It may enqueue notifications but must not execute guest code.
    Result Submit(uint64_t,const std::vector<int16_t>&,std::function<void(bool)> done={},uint64_t client=0);
    uint64_t Client() const {return client_;}
    bool Claim(uint64_t owner,uint64_t client);
    bool ReleaseClient(uint64_t owner,uint64_t client);
    bool Poll();
    bool Reset();
    bool Pause(uint64_t owner,uint64_t client,bool paused);
    bool Position(uint64_t owner,uint64_t client,uint64_t& frames);
    bool SetVolume(uint64_t owner,uint64_t client,uint16_t value);
    bool Volume(uint16_t& value);
    bool VolumeSupported() const {return device_ && device_->VolumeSupported();}
    size_t Pending() const;
    uint64_t Accepted() const {return accepted_;}
    uint64_t Completed() const {return completed_;}
    uint64_t Cancelled() const {return cancelled_;}
private:
    struct Ticket {uint64_t id=0;std::shared_ptr<const AudioBlock> block;std::function<void(bool)> done;};
    AudioDevice* device_=nullptr;
    std::array<Ticket,Slots> tickets_{};
    uint64_t client_=0,owner_=0,nextOwner_=1,nextTicket_=1,accepted_=0,completed_=0,cancelled_=0;
    bool fault_=false;
};
AudioOutput& Audio();
class AudioScope {
public:
    AudioScope():previous_(Audio().Owner()),previousClient_(Audio().Client()),owner_(Audio().Enter()){}
    ~AudioScope(){Close();}
    void Close(){if(!closed_){Audio().Leave(owner_,previous_,previousClient_);closed_=true;}}
    AudioScope(const AudioScope&)=delete;
    AudioScope& operator=(const AudioScope&)=delete;
private:
    uint64_t previous_,previousClient_,owner_;
    bool closed_=false;
};
// Checked RIFF PCM8/16 mono/stereo -> canonical output; preserves stereo channels.
bool DecodeWave(const std::vector<uint8_t>&,std::vector<int16_t>&);
} // namespace rt
