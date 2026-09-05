#include "StorageStats.h"

#include <Arduino.h>
#include <LittleFS.h>

namespace {
StorageStats::Figures cached;
uint32_t cachedAtMs = 0;
bool valid = false;
} // namespace

StorageStats::Figures StorageStats::littlefs(uint32_t maxAgeMs) {
    const uint32_t now = millis();
    if (!valid || now - cachedAtMs > maxAgeMs) {
        cached.total = LittleFS.totalBytes();
        cached.used = LittleFS.usedBytes();
        cachedAtMs = now;
        valid = true;
    }
    return cached;
}

void StorageStats::invalidate() { valid = false; }
