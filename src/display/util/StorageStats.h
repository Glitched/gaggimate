#ifndef STORAGESTATS_H
#define STORAGESTATS_H

#include <cstddef>
#include <cstdint>

// LittleFS.usedBytes() walks the whole filesystem: a burst of back-to-back flash reads that holds the SPI bus for
// longer than the panel's bounce-buffer slack, so every call shows as one shifted frame (`panelUnderruns`,
// 2026-09-04: exactly one per poll of GET /api/ota, which the web UI's System tab requests every 10 s). Nothing
// that is polled may call it. The figures here are refreshed at most once per `maxAgeMs`, or after a writer
// calls invalidate().
namespace StorageStats {
struct Figures {
    size_t total = 0;
    size_t used = 0;
};
Figures littlefs(uint32_t maxAgeMs = 60000);
void invalidate();
} // namespace StorageStats

#endif // STORAGESTATS_H
