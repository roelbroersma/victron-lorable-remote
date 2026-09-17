#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool valid;
    uint32_t revision;
    char victron_mac[18];
    int8_t address_type;
    uint8_t device_instance;
    bool pairing_enabled;
    bool pin_set;
    uint16_t status_minutes;
    uint16_t wifi_minutes;
    uint8_t ble_attempts;
    char wifi_ssid[33];
    char deveui[17];
    char joineui[17];
} app_config_snapshot_t;

typedef struct {
    app_config_snapshot_t public_values;
    bool pin_changed;
    char pin[7];
    bool appkey_changed;
    char appkey[33];
    bool wifi_password_changed;
    char wifi_password[64];
} app_config_update_t;

void app_model_snapshot_defaults(app_config_snapshot_t *snapshot);

esp_err_t app_model_snapshot_from_form(const char *form,
                                       app_config_snapshot_t *snapshot,
                                       char *error,
                                       size_t error_size);

esp_err_t app_model_update_from_form(const char *form,
                                     app_config_update_t *update,
                                     char *error,
                                     size_t error_size);

esp_err_t app_model_update_to_form(const app_config_update_t *update,
                                   char *form,
                                   size_t form_size);

void app_model_apply_update(app_config_snapshot_t *snapshot,
                            const app_config_update_t *update,
                            uint32_t revision);

void app_model_clear_update_secrets(app_config_update_t *update);

#ifdef __cplusplus
}
#endif
