#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_model.h"
#include "protocol.h"
void portal_receive_frame(const protocol_frame_t *frame);
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PORTAL_MODE_IDLE = 0,
    PORTAL_MODE_WIFI,
    PORTAL_MODE_BLE,
} portal_mode_t;

typedef struct {
    portal_mode_t mode;
    bool contact_active;
    uint32_t seconds_left;
    uint32_t config_revision;
    bool config_ready;
    bool config_request_pending;
    uint32_t last_ble_advertisements;
    bool last_ble_target_seen;
    esp_err_t last_ble_status;
    uint8_t target_state; // 0 unchecked, 1 seen, 2 not seen, 3 check failed
    char target_mac[18];
    uint32_t target_age_seconds;
} portal_status_t;

typedef void (*portal_read_status_fn)(portal_status_t *status);
typedef void (*portal_read_config_fn)(app_config_snapshot_t *snapshot);
typedef esp_err_t (*portal_submit_config_fn)(app_config_update_t *update,
                                             char *message,
                                             size_t message_size);

typedef struct {
    portal_read_status_fn read_status;
    portal_read_config_fn read_config;
    portal_submit_config_fn submit_config;
} portal_hooks_t;

esp_err_t portal_start(const char *ssid,
                       const char *password,
                       const portal_hooks_t *hooks,
                       bool *restart_required);
esp_err_t portal_stop(void);
bool portal_running(void);
bool portal_ota_in_progress(void);

#ifdef __cplusplus
}
#endif
