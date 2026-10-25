#pragma once
#include <atomic>
#include <cstdint>
#include <limits>
namespace ui::detail {
// Native-only resource identities survive guest/session storage and handle reuse.
// Exhaustion disables optional GPU eligibility rather than recycling an identity.
inline uint64_t NextRasterIdentity() {
    static std::atomic<uint64_t> next{1};
    auto n=next.load();
    for(;;) {
        if(n==std::numeric_limits<uint64_t>::max())return 0;
        if(next.compare_exchange_weak(n,n+1))return n;
    }
}
} // namespace ui::detail
