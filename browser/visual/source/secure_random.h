#pragma once
#include <cstddef>
#include <cstdint>
#ifdef __SWITCH__
#include <switch.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif
namespace visual {
inline bool secure_random(uint8_t *bytes, size_t count) {
#ifdef __SWITCH__
    randomGet(bytes, count);
    return true;
#else
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return false;
    size_t offset = 0;
    while (offset < count) {
        auto got = read(fd, bytes + offset, count - offset);
        if (got < 0 && errno == EINTR)
            continue;
        if (got <= 0) {
            close(fd);
            return false;
        }
        offset += size_t(got);
    }
    close(fd);
    return true;
#endif
}
} // namespace visual
