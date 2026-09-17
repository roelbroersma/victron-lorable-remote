#include "ble_scan.h"

#include <ctype.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

typedef struct {
    SemaphoreHandle_t started;
    SemaphoreHandle_t finished;
    volatile int start_status;
    volatile uint32_t advertisements;
    bool target_requested;
    uint8_t target_address[6];
    volatile bool target_seen;
} scan_context_t;

static scan_context_t scan_context;

static int hex_value(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    return -1;
}

static bool parse_mac(const char *text, uint8_t little_endian[6])
{
    if (text == NULL || text[0] == '\0') {
        return false;
    }
    uint8_t network_order[6] = {0};
    size_t nibble = 0;
    for (const char *cursor = text; *cursor != '\0'; ++cursor) {
        if (*cursor == ':' || *cursor == '-' || *cursor == ' ') {
            continue;
        }
        const int value = hex_value(*cursor);
        if (value < 0 || nibble >= 12) {
            return false;
        }
        if ((nibble & 1u) == 0u) {
            network_order[nibble / 2] = (uint8_t)(value << 4);
        } else {
            network_order[nibble / 2] |= (uint8_t)value;
        }
        ++nibble;
    }
    if (nibble != 12) {
        return false;
    }
    for (size_t index = 0; index < 6; ++index) {
        little_endian[index] = network_order[5 - index];
    }
    return true;
}

static int gap_event(struct ble_gap_event *event, void *argument)
{
    (void)argument;
    if (event->type == BLE_GAP_EVENT_DISC) {
        ++scan_context.advertisements;
        if (scan_context.target_requested &&
            memcmp(event->disc.addr.val, scan_context.target_address, 6) == 0) {
            scan_context.target_seen = true;
        }
    } else if (event->type == BLE_GAP_EVENT_DISC_COMPLETE) {
        if (scan_context.finished != NULL) {
            xSemaphoreGive(scan_context.finished);
        }
    }
    return 0;
}

static void host_sync(void)
{
    uint8_t own_address_type = 0;
    int result = ble_hs_util_ensure_addr(0);
    if (result == 0) {
        result = ble_hs_id_infer_auto(0, &own_address_type);
    }
    if (result == 0) {
        const struct ble_gap_disc_params parameters = {
            .itvl = 0,
            .window = 0,
            .filter_policy = 0,
            .limited = 0,
            .passive = 1,
            .filter_duplicates = 1,
        };
        result = ble_gap_disc(own_address_type,
                              BLE_HS_FOREVER,
                              &parameters,
                              gap_event,
                              NULL);
    }
    scan_context.start_status = result;
    if (scan_context.started != NULL) {
        xSemaphoreGive(scan_context.started);
    }
}

static void host_reset(int reason)
{
    scan_context.start_status = reason == 0 ? BLE_HS_ECONTROLLER : reason;
    if (scan_context.started != NULL) {
        xSemaphoreGive(scan_context.started);
    }
}

static void host_task(void *argument)
{
    (void)argument;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static bool stop_stack(void)
{
    const int stop_result = nimble_port_stop();
    if (stop_result != 0) {
        return false;
    }
    return nimble_port_deinit() == ESP_OK;
}

esp_err_t ble_scan_run(const char *target_mac,
                       uint32_t duration_ms,
                       ble_scan_result_t *result)
{
    if (result == NULL || duration_ms < 1000 || duration_ms > 30000) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(result, 0, sizeof(*result));
    memset(&scan_context, 0, sizeof(scan_context));
    scan_context.start_status = BLE_HS_EUNKNOWN;
    if (target_mac != NULL && target_mac[0] != '\0') {
        if (!parse_mac(target_mac, scan_context.target_address)) {
            result->status = ESP_ERR_INVALID_ARG;
            return ESP_ERR_INVALID_ARG;
        }
        scan_context.target_requested = true;
    }

    scan_context.started = xSemaphoreCreateBinary();
    scan_context.finished = xSemaphoreCreateBinary();
    if (scan_context.started == NULL || scan_context.finished == NULL) {
        if (scan_context.started != NULL) vSemaphoreDelete(scan_context.started);
        if (scan_context.finished != NULL) vSemaphoreDelete(scan_context.finished);
        memset(&scan_context, 0, sizeof(scan_context));
        result->status = ESP_ERR_NO_MEM;
        return ESP_ERR_NO_MEM;
    }

    esp_err_t status = nimble_port_init();
    if (status != ESP_OK) {
        result->restart_required = true;
        result->status = status;
        return status;
    }

    ble_hs_cfg.reset_cb = host_reset;
    ble_hs_cfg.sync_cb = host_sync;
    nimble_port_freertos_init(host_task);

    if (xSemaphoreTake(scan_context.started, pdMS_TO_TICKS(3000)) != pdTRUE ||
        scan_context.start_status != 0) {
        const bool stopped = stop_stack();
        status = stopped ? ESP_ERR_TIMEOUT : ESP_ERR_INVALID_STATE;
        result->restart_required = !stopped;
    } else {
        vTaskDelay(pdMS_TO_TICKS(duration_ms));
        const int cancel_status = ble_gap_disc_cancel();
        if (cancel_status == 0) {
            xSemaphoreTake(scan_context.finished, pdMS_TO_TICKS(1000));
        }
        const bool stopped = stop_stack();
        status = stopped ? ESP_OK : ESP_ERR_INVALID_STATE;
        result->restart_required = !stopped;
    }

    result->status = status;
    result->advertisements = scan_context.advertisements;
    result->target_requested = scan_context.target_requested;
    result->target_seen = scan_context.target_seen;

    if (!result->restart_required) {
        vSemaphoreDelete(scan_context.started);
        vSemaphoreDelete(scan_context.finished);
        memset(&scan_context, 0, sizeof(scan_context));
    }
    return status;
}
