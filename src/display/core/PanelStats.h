#ifndef PANELSTATS_H
#define PANELSTATS_H
#include <atomic>
#include <cstdint>

// Render-pipeline counters for "the UI runs but the picture lags" diagnosis. Producers are the UI task (frames), the
// LVGL flush callback (copies into the PSRAM framebuffer) and the RGB panel's VSYNC interrupt; the consumer is
// WebUIPlugin, which snapshots them every 10 s for a serial line and for GET /api/ota. Lock-free by design so the
// ISR producer never blocks.
struct PanelStats {
    std::atomic<uint32_t> uiFrames{0};     // DefaultUI::loop() iterations
    std::atomic<uint32_t> flushes{0};      // LVGL flush callbacks
    std::atomic<uint32_t> flushUsTotal{0}; // time spent inside flush, microseconds
    std::atomic<uint32_t> flushUsMax{0};
    std::atomic<uint32_t> vsyncs{0}; // RGB panel VSYNC interrupts (nominal ~23.5 Hz on the T-RGB)

    struct Snapshot {
        float uiFps = 0, flushHz = 0, vsyncHz = 0;
        uint32_t flushAvgUs = 0, flushMaxUs = 0;
    };

    void recordFlush(uint32_t us) {
        flushes.fetch_add(1, std::memory_order_relaxed);
        flushUsTotal.fetch_add(us, std::memory_order_relaxed);
        uint32_t prev = flushUsMax.load(std::memory_order_relaxed);
        while (us > prev && !flushUsMax.compare_exchange_weak(prev, us, std::memory_order_relaxed)) {
        }
    }

    // Rates over the window since the previous snapshot; resets the counters.
    Snapshot snapshot(uint32_t elapsedMs) {
        Snapshot s;
        const uint32_t frames = uiFrames.exchange(0), fl = flushes.exchange(0), total = flushUsTotal.exchange(0),
                       mx = flushUsMax.exchange(0), vs = vsyncs.exchange(0);
        if (elapsedMs == 0)
            return s;
        const float sec = elapsedMs / 1000.0f;
        s.uiFps = frames / sec;
        s.flushHz = fl / sec;
        s.vsyncHz = vs / sec;
        s.flushAvgUs = fl ? total / fl : 0;
        s.flushMaxUs = mx;
        return s;
    }
};

inline PanelStats &panelStats() {
    static PanelStats stats;
    return stats;
}

#endif // PANELSTATS_H
