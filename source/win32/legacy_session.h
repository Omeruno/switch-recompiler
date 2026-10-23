#pragma once
#include <memory>
namespace win32 {
// Compatibility globals remain the legacy frontend facade. Exchange their
// owned values only after guest continuations and presentation have drained.
class LegacySession {
public:
    LegacySession();
    ~LegacySession();
    LegacySession(const LegacySession&)=delete;
    LegacySession& operator=(const LegacySession&)=delete;
private:
    struct Saved;
    std::unique_ptr<Saved> previous_;
};
}
