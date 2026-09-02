// Host shim for nvs_flash.h: the sim's Preferences is file-backed, so NVS "init" is a no-op.
#pragma once
#include "esp_err.h"

#ifndef ESP_ERR_NVS_NO_FREE_PAGES
#define ESP_ERR_NVS_NO_FREE_PAGES 0x110d
#endif
#ifndef ESP_ERR_NVS_NEW_VERSION_FOUND
#define ESP_ERR_NVS_NEW_VERSION_FOUND 0x1110
#endif

static inline esp_err_t nvs_flash_init(void) { return ESP_OK; }
