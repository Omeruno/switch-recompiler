#pragma once
#include <array>
#include <cstdint>
namespace ui {
struct AudioVisualization {
    std::array<float,32> bands{};
    float leftRms=0,rightRms=0;
    // 256 actual stereo frames at48kHz, Hann-windowed radix2 FFT.
    static AudioVisualization Analyze(const std::array<int16_t,512>&);
};
}
