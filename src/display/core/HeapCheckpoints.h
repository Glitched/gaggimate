#ifndef HEAPCHECKPOINTS_H
#define HEAPCHECKPOINTS_H
#include <cstddef>
#include <cstdint>

// Internal-heap attribution: call heapCheckpoint("label") at each boot stage and around events of interest. Each call
// logs free / largest / minimum-ever internal heap plus the delta since the previous checkpoint, and keeps the sample in
// a small table that GET /api/debug/heap returns, so the boot phases can be read over Wi-Fi after the fact.
struct HeapCheckpoint {
    const char *label; // string literal, not copied
    uint32_t atMs;
    uint32_t free;
    uint32_t largest;
    uint32_t minFree;
};

static constexpr size_t HEAP_CHECKPOINT_CAPACITY = 32;

void heapCheckpoint(const char *label);
const HeapCheckpoint *heapCheckpoints(size_t &count); // oldest first; stops recording once the table is full

#endif // HEAPCHECKPOINTS_H
