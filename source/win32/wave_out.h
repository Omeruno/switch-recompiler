#pragma once
namespace win32 {
// Host-only retirement service. Never executes a guest callback on a driver stack.
void PollWaveOut();
void EndWaveOutSession();
}
