#include "HeapCheckpoints.h"
#include <Arduino.h>
#include <esp_heap_caps.h>

static HeapCheckpoint table[HEAP_CHECKPOINT_CAPACITY];
static size_t used = 0;

void heapCheckpoint(const char *label) {
    const uint32_t caps = MALLOC_CAP_DEFAULT | MALLOC_CAP_INTERNAL;
    const HeapCheckpoint cp{label, static_cast<uint32_t>(millis()), static_cast<uint32_t>(heap_caps_get_free_size(caps)),
                            static_cast<uint32_t>(heap_caps_get_largest_free_block(caps)),
                            static_cast<uint32_t>(heap_caps_get_minimum_free_size(caps))};
    const int32_t delta = used > 0 ? static_cast<int32_t>(cp.free) - static_cast<int32_t>(table[used - 1].free) : 0;
    log_i("heap[%s] free %u (%+d) largest %u min %u", label, static_cast<unsigned>(cp.free), static_cast<int>(delta),
          static_cast<unsigned>(cp.largest), static_cast<unsigned>(cp.minFree));
    if (used < HEAP_CHECKPOINT_CAPACITY)
        table[used++] = cp;
}

const HeapCheckpoint *heapCheckpoints(size_t &count) {
    count = used;
    return table;
}
