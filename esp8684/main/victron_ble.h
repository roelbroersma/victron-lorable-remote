#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VICTRON_LOAD_ALWAYS_ON 0x04u
#define VICTRON_VALUE_UNKNOWN  0xffu

typedef enum {
    VICTRON_RESULT_NOT_RUN = 0,
    VICTRON_RESULT_OK = 1,
    VICTRON_RESULT_BAD_SETTINGS = 2,
    VICTRON_RESULT_STACK_ERROR = 3,
    VICTRON_RESULT_TARGET_NOT_FOUND = 4,
    VICTRON_RESULT_CONNECT_FAILED = 5,
    VICTRON_RESULT_GATT_NOT_FOUND = 6,
    VICTRON_RESULT_INITIAL_READ_FAILED = 7,
    VICTRON_RESULT_WRITE_FAILED = 8,
    VICTRON_RESULT_VERIFY_FAILED = 9,
    VICTRON_RESULT_SECURITY_FAILED = 10,
    VICTRON_RESULT_BUSY = 11,
} victron_result_code_t;

typedef struct {
    char mac[18];
    int8_t address_type;
    uint8_t instance;
    bool pairing;
    char pin[7];
    uint8_t desired_value;
    uint8_t max_attempts;
    uint8_t generic_kind; // 0 Victron, 3 acknowledged GATT write, 4 write+exact readback
    uint8_t driver; // 1 SmartSolar MPPT; 3 Smart BatteryProtect (supported PID A3B1)
    char service_uuid[37], characteristic_uuid[37];
    uint8_t value[32], value_length;
} victron_request_t;

typedef struct {
    victron_result_code_t code;
    uint8_t attempts;
    uint8_t initial_value;
    uint8_t verified_value;
    uint8_t load_value;
    bool changed;
    bool target_seen;
    bool verified;
    /* Internal recovery signal; this field is never included in UART payloads. */
    bool restart_required;
    uint32_t advertisements;
} victron_result_t;

bool victron_ble_request_valid(const victron_request_t *request);
const char *victron_ble_result_name(victron_result_code_t code);

/*
 * Runs synchronously in the radio-manager task. WiFi must already be stopped.
 * MPPT changes only EDAB mode 0..7, preserving the upper bits. Generic writes
 * the explicitly configured GATT characteristic. BatteryProtect uses instance 0,
 * mode 0x0200, identity validation and actual-output 0xEDA8 readback.
 */
esp_err_t victron_ble_run(const victron_request_t *request,
                          victron_result_t *result);

void victron_ble_clear_request(victron_request_t *request);

#ifdef __cplusplus
}
#endif
