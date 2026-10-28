#pragma once
#include <cstdint>
namespace win32 {
// A real scheduled guest worker; never an APC or a call from a device stack.
bool PrepareWaveCallbacks();
bool WaveCallbacksHealthy();
bool QueueWaveCallback(uint32_t routine,uint32_t handle,uint32_t message,
                       uint32_t instance,uint32_t header);
void EndWaveCallbacks();
}
