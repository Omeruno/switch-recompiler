#pragma once
#include <array>
#include <cstdint>
namespace visual {
struct GameInput {
    bool connected = false;
    std::array<float, 4> axes{};
    std::array<bool, 17> buttons{};
    uint64_t timestamp = 0;
};
} // namespace visual
