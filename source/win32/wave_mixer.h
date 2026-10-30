#pragma once
#include "host_functions.h"
namespace win32 {
// Device 1 is a real bounded software-mixed WinMM output over the same audout.
// WAVE_MAPPER selects this path when counters work; device 0 remains direct.
bool MixedWaveSupported();
bool IsMixedWaveHandle(uint32_t);
bool MixedWaveHandleLive(uint32_t);
enum class MixedWaveOp {Open,Prepare,Unprepare,Write,Reset,Close,Pause,Restart,Position,SetVolume,GetVolume};
HostCallOutcome MixedWaveCall(MixedWaveOp,cpu::CpuState&,cpu::IGuestMemory&);
void PollMixedWave();
void EndMixedWave();
}
