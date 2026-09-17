#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    esp_err_t status;
    uint32_t advertisements;
    bool target_requested;
    bool target_seen;
    /* Internal recovery signal; this field is never included in UART payloads. */
    bool restart_required;
} ble_scan_result_t;

esp_err_t ble_scan_run(const char *target_mac,
                       uint32_t duration_ms,
                       ble_scan_result_t *result);

#ifdef __cplusplus
}
#endif
