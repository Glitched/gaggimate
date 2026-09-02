// Host shim for esp_sntp.h: time is taken from the host clock, so these are no-ops.
#pragma once

typedef enum { SNTP_SYNC_MODE_IMMED = 0, SNTP_SYNC_MODE_SMOOTH = 1 } sntp_sync_mode_t;

static inline void esp_sntp_init(void) {}
static inline void esp_sntp_stop(void) {}
static inline void esp_sntp_set_sync_mode(sntp_sync_mode_t mode) { (void)mode; }
static inline void esp_sntp_setservername(int idx, const char *server) {
    (void)idx;
    (void)server;
}
