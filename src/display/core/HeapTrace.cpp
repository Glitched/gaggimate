#include "HeapTrace.h"

#ifdef GM_HEAP_TRACE

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <cstdio>
#include <cstring>
#include <esp_heap_caps.h>
#include <esp_heap_trace.h>

namespace {

constexpr size_t RECORD_CAPACITY = 8192;
heap_trace_record_t *records = nullptr;

// Runs before every other translation unit's constructors (user init priorities start at 101, lowest first), so
// the Settings constructor's NVS work and everything initArduino() allocates are already on record when
// Controller::setup() begins. IDF's example keeps this buffer in internal RAM; ours is in PSRAM on purpose: 8192
// records of ~124 B would eat the whole internal heap this env exists to measure, and the tracer only touches the
// buffer from inside malloc/free, which never run while the flash cache (and with it PSRAM) is disabled.
__attribute__((constructor(101))) void heapTraceStartEarly() {
    records = static_cast<heap_trace_record_t *>(
        heap_caps_calloc(RECORD_CAPACITY, sizeof(heap_trace_record_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (records == nullptr) {
        log_e("no PSRAM for %u trace records; heap tracing disabled", static_cast<unsigned>(RECORD_CAPACITY));
        return;
    }
    esp_err_t err = heap_trace_init_standalone(records, RECORD_CAPACITY);
    if (err == ESP_OK)
        err = heap_trace_start(HEAP_TRACE_LEAKS);
    if (err != ESP_OK) {
        log_e("heap tracing failed to start: %s", esp_err_to_name(err));
        heap_trace_init_standalone(nullptr, 0);
        heap_caps_free(records);
        records = nullptr;
        return;
    }
    log_i("heap tracing %u records (%u B each, PSRAM)", static_cast<unsigned>(RECORD_CAPACITY),
          static_cast<unsigned>(sizeof(heap_trace_record_t)));
}

// State of one dump in flight. The chunk callback is invoked repeatedly by AsyncTCP with whatever buffer space the
// socket has; it emits whole lines only and keeps its position here.
struct DumpCursor {
    size_t index = 0;
    bool headerSent = false;
    heap_trace_summary_t summary{};
};

size_t fillDump(DumpCursor *cur, uint8_t *buf, size_t maxLen) {
    // Longest line: 10-digit size, 8-digit address, 12 frames of " %08x", newline. The header is shorter.
    char line[32 + 9 * CONFIG_HEAP_TRACING_STACK_DEPTH + 96];
    size_t written = 0;
    if (!cur->headerSent) {
        const heap_trace_summary_t &s = cur->summary;
        const int n = snprintf(line, sizeof line,
                               "# count=%u capacity=%u high=%u overflowed=%u allocs=%u frees=%u internalFree=%u uptimeMs=%lu\n",
                               static_cast<unsigned>(s.count), static_cast<unsigned>(s.capacity),
                               static_cast<unsigned>(s.high_water_mark), static_cast<unsigned>(s.has_overflowed ? 1 : 0),
                               static_cast<unsigned>(s.total_allocations), static_cast<unsigned>(s.total_frees),
                               static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_DEFAULT | MALLOC_CAP_INTERNAL)),
                               static_cast<unsigned long>(millis()));
        if (n <= 0 || static_cast<size_t>(n) > maxLen)
            return RESPONSE_TRY_AGAIN;
        memcpy(buf, line, static_cast<size_t>(n));
        written = static_cast<size_t>(n);
        cur->headerSent = true;
    }
    while (cur->index < RECORD_CAPACITY) {
        // Tracing is stopped for the duration of the dump, so the array is stable. A zero address is a slot the
        // tracer freed (list_remove clears it) or never used.
        const heap_trace_record_t &r = records[cur->index];
        if (r.address == nullptr) {
            cur->index++;
            continue;
        }
        int n = snprintf(line, sizeof line, "%u %08x", static_cast<unsigned>(r.size),
                         static_cast<unsigned>(reinterpret_cast<uintptr_t>(r.address)));
        for (size_t i = 0; i < CONFIG_HEAP_TRACING_STACK_DEPTH && r.alloced_by[i] != nullptr; i++)
            n += snprintf(line + n, sizeof line - static_cast<size_t>(n), " %08x",
                          static_cast<unsigned>(reinterpret_cast<uintptr_t>(r.alloced_by[i])));
        line[n++] = '\n';
        if (written + static_cast<size_t>(n) > maxLen)
            break; // whole lines only; this record goes into the next chunk
        memcpy(buf + written, line, static_cast<size_t>(n));
        written += static_cast<size_t>(n);
        cur->index++;
    }
    if (written == 0 && cur->index < RECORD_CAPACITY)
        return RESPONSE_TRY_AGAIN; // buffer smaller than one line; never seen with AsyncTCP's ~1.4 KB chunks
    return written; // 0 once every record has been written, which ends the chunked response
}

void handleTraceDump(AsyncWebServerRequest *request) {
    if (records == nullptr) {
        request->send(503, "application/json", R"({"error":"heap tracing is not running"})");
        return;
    }
    // Stop rather than read a moving list: the tracer mutates records under its own spinlock and a torn record
    // would put a wrong size against a wrong call stack. Frees that happen while the dump streams are not recorded,
    // so their blocks linger in the table until the next reset; that is a second or two of drift per dump.
    auto *cursor = new DumpCursor();
    heap_trace_stop();
    heap_trace_summary(&cursor->summary);
    request->onDisconnect([cursor]() {
        delete cursor;
        heap_trace_resume(); // keeps the existing records, unlike heap_trace_start()
    });
    request->send(request->beginChunkedResponse(
        "text/plain", [cursor](uint8_t *buf, size_t maxLen, size_t) { return fillDump(cursor, buf, maxLen); }));
}

void handleTraceReset(AsyncWebServerRequest *request) {
    if (records == nullptr) {
        request->send(503, "application/json", R"({"error":"heap tracing is not running"})");
        return;
    }
    heap_trace_stop();
    const esp_err_t err = heap_trace_start(HEAP_TRACE_LEAKS); // clears the records: a later dump shows only what came after
    if (err != ESP_OK) {
        request->send(500, "application/json", R"({"error":"heap_trace_start failed"})");
        return;
    }
    request->send(200, "application/json", R"({"ok":true})");
}

} // namespace

void heapTraceRegisterRoutes(AsyncWebServer &server) {
    server.on("/api/debug/heap/trace", HTTP_GET, handleTraceDump);
    server.on("/api/debug/heap/trace/reset", HTTP_POST, handleTraceReset);
}

#endif // GM_HEAP_TRACE
