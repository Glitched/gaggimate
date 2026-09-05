#include "RawFile.h"

#include <cstdint>
#include <string>
#ifdef GAGGIMATE_SIM
#include <LittleFS.h>
#endif

namespace {
// Controller::setup() mounts the filesystem at this base path (LittleFS.begin(false, "/littlefs", 16)).
constexpr const char *LITTLEFS_MOUNT = "/littlefs";

std::string devicePath(const char *logical) {
#ifdef GAGGIMATE_SIM
    return LittleFS.hostPath(logical);
#else
    return std::string(LITTLEFS_MOUNT) + logical;
#endif
}
} // namespace

bool RawFile::open(const char *logicalPath, int flags) {
    close();
    fd = ::open(devicePath(logicalPath).c_str(), flags, 0644);
    return fd >= 0;
}

void RawFile::close() {
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }
}

bool RawFile::read(void *buf, size_t len) const {
    auto *p = static_cast<uint8_t *>(buf);
    while (len > 0) {
        const ssize_t n = ::read(fd, p, len);
        if (n <= 0)
            return false;
        p += n;
        len -= static_cast<size_t>(n);
    }
    return true;
}

bool RawFile::write(const void *buf, size_t len) const {
    const auto *p = static_cast<const uint8_t *>(buf);
    while (len > 0) {
        const ssize_t n = ::write(fd, p, len);
        if (n <= 0)
            return false;
        p += n;
        len -= static_cast<size_t>(n);
    }
    return true;
}

bool RawFile::seek(long offset, int whence) const { return ::lseek(fd, offset, whence) >= 0; }
