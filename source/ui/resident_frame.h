#pragma once
#include "surface.h"
#include <cstddef>
#include <cstdint>

namespace ui {
// Internal synchronous exchange on the context owner thread. Texture names and
// recovered CPU views must never be retained after Consume returns. A texture
// uses logical top-down rows, like GDI targets, not framebuffer orientation.
struct ResidentImageView {
    uint32_t texture=0;
    int width=0,height=0;
    const Surface* cpu=nullptr; // genuine recovery when GPU is unavailable
};
struct ResidentFrameView {
    const ResidentImageView* images=nullptr;
    size_t count=0;
};
class ResidentFrameConsumer {
public:
    virtual ~ResidentFrameConsumer()=default;
    virtual bool Consume(const ResidentFrameView&)=0;
};
} // namespace ui
