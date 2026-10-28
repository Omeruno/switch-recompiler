#pragma once
// Minimal audout transport model for compiling the production adapter on host.
// Not libnx, a Switch SDK, hardware timing or audible-output acceptance.
#include <cstdint>
using Result=uint32_t;using u32=uint32_t;using u64=uint64_t;
#define R_FAILED(rc) ((rc)!=0)
#define R_SUCCEEDED(rc) ((rc)==0)
enum PcmFormat {PcmFormat_Int16=2};
struct AudioOutBuffer {AudioOutBuffer* next;void* buffer;u64 buffer_size,data_size,data_offset;};
Result audoutInitialize();void audoutExit();u32 audoutGetSampleRate();u32 audoutGetChannelCount();PcmFormat audoutGetPcmFormat();
Result audoutStartAudioOut();Result audoutStopAudioOut();Result audoutAppendAudioOutBuffer(AudioOutBuffer*);
Result audoutGetReleasedAudioOutBuffer(AudioOutBuffer**,u32*);Result audoutContainsAudioOutBuffer(AudioOutBuffer*,bool*);Result audoutFlushAudioOutBuffers(bool*);
Result audoutGetAudioOutPlayedSampleCount(u64*);
Result audoutSetAudioOutVolume(float);Result audoutGetAudioOutVolume(float*);
inline bool hosversionAtLeast(unsigned,unsigned,unsigned){return true;}
