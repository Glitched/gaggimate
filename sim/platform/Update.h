// Host shim for the ESP32 Update library.
//
// The simulator has no OTA partitions, so this accepts a streamed image,
// validates the ESP image magic byte the way the real Updater does, and
// accounts for the bytes -- enough to exercise POST /api/ota/upload end to end
// (auth, chunking, progress, error paths) without writing anything. The
// firmware never reboots into it: sim/main.cpp's ESP.restart() is a no-op.
#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

#define U_FLASH 0
#define U_SPIFFS 100
#define UPDATE_SIZE_UNKNOWN 0xFFFFFFFF

// Matches the real Updater's check (Updater.cpp): the first byte of a valid
// ESP32 application image.
static constexpr uint8_t ESP_IMAGE_HEADER_MAGIC_SIM = 0xE9;

// A plausible stand-in for the app partition size so percentage maths behaves.
static constexpr size_t SIM_OTA_PARTITION_SIZE = 0x640000;

class UpdateClassSim {
  public:
    bool begin(size_t size = UPDATE_SIZE_UNKNOWN, int command = U_FLASH) {
        (void)size;
        _command = command;
        _running = true;
        _finished = false;
        _error = nullptr;
        _written = 0;
        _sawFirstByte = false;
        return true;
    }

    size_t write(uint8_t *data, size_t len) {
        if (!_running) {
            return 0;
        }
        // Real Updater checks the image magic only for U_FLASH; a LittleFS
        // image (U_SPIFFS) has no such header.
        if (!_sawFirstByte && len > 0 && _command == U_FLASH) {
            _sawFirstByte = true;
            if (data[0] != ESP_IMAGE_HEADER_MAGIC_SIM) {
                _error = "Wrong Magic Byte";
                _running = false;
                return 0;
            }
        }
        _written += len;
        return len;
    }

    bool end(bool evenIfRemaining = false) {
        (void)evenIfRemaining;
        if (!_running) {
            return false;
        }
        _running = false;
        _finished = _error == nullptr;
        return _finished;
    }

    void abort() {
        _running = false;
        _finished = false;
        if (_error == nullptr) {
            _error = "Aborted";
        }
    }

    bool hasError() const { return _error != nullptr; }
    const char *errorString() const { return _error ? _error : "No Error"; }
    bool isRunning() const { return _running; }
    bool isFinished() const { return _finished; }
    size_t size() const { return SIM_OTA_PARTITION_SIZE; }
    size_t progress() const { return _written; }

  private:
    bool _running = false;
    bool _finished = false;
    bool _sawFirstByte = false;
    int _command = U_FLASH;
    size_t _written = 0;
    const char *_error = nullptr;
};

extern UpdateClassSim Update;
