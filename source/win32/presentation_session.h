#pragma once
#include "user32.h"

namespace win32::user {
// Declare before GuestExecution: native guest continuations must drain before
// releasing frame storage and restoring the previous platform callbacks.
class PresentationSession {
public:
    PresentationSession(bool enabled,void (*finish)()):enabled_(enabled),finish_(finish),previous_(g_platform) {}
    ~PresentationSession() {
        if(enabled_) {
            if(finish_)finish_();
            g_platform=previous_;
        }
    }
    PresentationSession(const PresentationSession&)=delete;
    PresentationSession& operator=(const PresentationSession&)=delete;
private:
    bool enabled_;
    void (*finish_)();
    Platform previous_;
};
} // namespace win32::user
