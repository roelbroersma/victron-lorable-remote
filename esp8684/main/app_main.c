#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_model.h"
#include "ble_scan.h"
#include "esp_app_desc.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "ota_guard.h"
#include "portal.h"
#include "protocol.h"
#include "uart_link.h"
#include "victron_ble.h"

#define CONFIG_CONFIRM_TIMEOUT_MS 5000

typedef struct {
    bool hello_seen;
    bool portal_requested;
    bool portal_restart;
    int64_t portal_deadline_us;
    char portal_ssid[33];
    char portal_password[64];
    bool contact_active;
    portal_mode_t mode;

    app_config_snapshot_t config;
    bool config_request_active;
    uint16_t config_request_id;
    bool config_response_ok;
    uint32_t config_response_revision;
    char config_response_message[96];

    bool ble_request_pending;
    uint16_t ble_request_id;
    uint32_t ble_duration_ms;
    char ble_target_mac[18];
    ble_scan_result_t last_ble_result;
    uint8_t target_state;
    char last_target_mac[18];
    int64_t target_checked_at_us;

    bool victron_request_active;
    bool victron_work_pending;
    uint16_t victron_request_id;
    victron_request_t victron_request;
    victron_result_t last_victron_result;
    bool victron_cache_valid;
    uint16_t victron_cache_id;
    char victron_cache_payload[256];
} application_state_t;

static application_state_t application_state;
static SemaphoreHandle_t state_lock;
static SemaphoreHandle_t config_response_signal;
static TaskHandle_t manager_task;
static uint16_t next_config_request_id = 0x8000;
static volatile bool uart_started;

static void send_ready(void)
{
    char ready[128] = {0};
    const esp_app_desc_t *description = esp_app_get_description();
    protocol_form_append(ready, sizeof(ready), "role", "esp8684");
    protocol_form_append(ready, sizeof(ready), "proto", "1");
    protocol_form_append(ready, sizeof(ready), "fw",
                         description != NULL ? description->version : "unknown");
    protocol_form_append(ready, sizeof(ready), "max_frame", "768");
    uart_link_send("READY", 0, ready);
}

static void maybe_confirm_ota(void)
{
    if (!ota_guard_pending()) {
        return;
    }
    bool checks_passed = false;
    if (xSemaphoreTake(state_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        checks_passed = application_state.config.valid &&
                        application_state.mode == PORTAL_MODE_WIFI;
        xSemaphoreGive(state_lock);
    }
    if (checks_passed && portal_running()) {
        (void)ota_guard_confirm();
    }
}

static uint32_t seconds_remaining_locked(int64_t now_us)
{
    if (!application_state.portal_requested || now_us >= application_state.portal_deadline_us) {
        return 0;
    }
    const int64_t remaining_us = application_state.portal_deadline_us - now_us;
    return (uint32_t)((remaining_us + 999999) / 1000000);
}

static bool parse_u32(const char *text, uint32_t minimum, uint32_t maximum, uint32_t *value)
{
    if (text == NULL || text[0] == '\0') {
        return false;
    }
    char *end = NULL;
    const unsigned long parsed = strtoul(text, &end, 10);
    if (end == text || *end != '\0' || parsed < minimum || parsed > maximum) {
        return false;
    }
    *value = (uint32_t)parsed;
    return true;
}

static bool parse_i32(const char *text, int minimum, int maximum, int *value)
{
    if (text == NULL || text[0] == '\0') {
        return false;
    }
    char *end = NULL;
    const long parsed = strtol(text, &end, 10);
    if (end == text || *end != '\0' || parsed < minimum || parsed > maximum) {
        return false;
    }
    *value = (int)parsed;
    return true;
}

static bool visible_without_space(const char *text, size_t minimum, size_t maximum)
{
    const size_t length = text == NULL ? 0 : strlen(text);
    if (length < minimum || length > maximum) {
        return false;
    }
    for (size_t index = 0; index < length; ++index) {
        const unsigned char value = (unsigned char)text[index];
        if (value < 0x21 || value > 0x7e) {
            return false;
        }
    }
    return true;
}

static bool valid_ssid(const char *text)
{
    const size_t length = text == NULL ? 0 : strlen(text);
    if (length == 0 || length > 32) {
        return false;
    }
    for (size_t index = 0; index < length; ++index) {
        const unsigned char value = (unsigned char)text[index];
        if (value < 0x20 || value > 0x7e) {
            return false;
        }
    }
    return true;
}

static void derive_ssid(char destination[33])
{
    snprintf(destination, 33, "RAK-A231-SETUP");
}

static esp_err_t append_u32(char *form, size_t form_size, const char *key, uint32_t value)
{
    char text[16];
    snprintf(text, sizeof(text), "%lu", (unsigned long)value);
    return protocol_form_append(form, form_size, key, text);
}

static void send_result(uint16_t id, bool ok, const char *code)
{
    char payload[128] = {0};
    protocol_form_append(payload, sizeof(payload), "ok", ok ? "1" : "0");
    protocol_form_append(payload, sizeof(payload), "code", code);
    uart_link_send("RESULT", id, payload);
    protocol_secure_zero(payload, sizeof(payload));
}

static void send_portal_state(bool running, const char *reason)
{
    char payload[160] = {0};
    protocol_form_append(payload, sizeof(payload), "running", running ? "1" : "0");
    if (running) {
        protocol_form_append(payload, sizeof(payload), "ip", "192.168.4.1");
    }
    protocol_form_append(payload, sizeof(payload), "reason", reason);
    uart_link_send("PORTAL_STATE", 0, payload);
    protocol_secure_zero(payload, sizeof(payload));
}

static void read_portal_status(portal_status_t *status)
{
    memset(status, 0, sizeof(*status));
    if (xSemaphoreTake(state_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    status->mode = application_state.mode;
    status->contact_active = application_state.contact_active;
    status->seconds_left = seconds_remaining_locked(esp_timer_get_time());
    status->config_revision = application_state.config.revision;
    status->config_ready = application_state.config.valid;
    status->config_request_pending = application_state.config_request_active;
    status->last_ble_advertisements = application_state.last_ble_result.advertisements;
    status->last_ble_target_seen = application_state.last_ble_result.target_seen;
    status->last_ble_status = application_state.last_ble_result.status;
    status->target_state = application_state.target_state;
    memcpy(status->target_mac, application_state.last_target_mac, sizeof(status->target_mac));
    status->target_age_seconds = application_state.target_state ?
        (uint32_t)((esp_timer_get_time() - application_state.target_checked_at_us) / 1000000) : 0;
    xSemaphoreGive(state_lock);
}

static void read_portal_config(app_config_snapshot_t *snapshot)
{
    app_model_snapshot_defaults(snapshot);
    if (xSemaphoreTake(state_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        *snapshot = application_state.config;
        xSemaphoreGive(state_lock);
    }
}

static esp_err_t submit_portal_config(app_config_update_t *update,
                                      char *message,
                                      size_t message_size)
{
    char payload[PROTOCOL_PAYLOAD_MAX + 1] = {0};
    esp_err_t result = app_model_update_to_form(update, payload, sizeof(payload));
    if (result != ESP_OK) {
        snprintf(message, message_size, "Instellingen zijn te groot");
        protocol_secure_zero(payload, sizeof(payload));
        return result;
    }

    if (xSemaphoreTake(state_lock, pdMS_TO_TICKS(250)) != pdTRUE) {
        snprintf(message, message_size, "Interne status bezet");
        protocol_secure_zero(payload, sizeof(payload));
        return ESP_ERR_TIMEOUT;
    }
    if (application_state.config_request_active || !application_state.config.valid) {
        xSemaphoreGive(state_lock);
        snprintf(message,
                 message_size,
                 application_state.config.valid ? "Er loopt al een opslagverzoek"
                                                : "Nog geen configuratie van STM32 ontvangen");
        protocol_secure_zero(payload, sizeof(payload));
        return ESP_ERR_INVALID_STATE;
    }
    uint16_t request_id = next_config_request_id++;
    if (next_config_request_id < 0x8000) {
        next_config_request_id = 0x8000;
    }
    application_state.config_request_active = true;
    application_state.config_request_id = request_id;
    application_state.config_response_ok = false;
    application_state.config_response_revision = 0;
    application_state.config_response_message[0] = '\0';
    xSemaphoreTake(config_response_signal, 0);
    xSemaphoreGive(state_lock);

    result = uart_link_send("CONFIG_SET", request_id, payload);
    protocol_secure_zero(payload, sizeof(payload));
    if (result != ESP_OK) {
        if (xSemaphoreTake(state_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
            application_state.config_request_active = false;
            xSemaphoreGive(state_lock);
        }
        snprintf(message, message_size, "Verzenden naar STM32 mislukt");
        return result;
    }

    if (xSemaphoreTake(config_response_signal,
                       pdMS_TO_TICKS(CONFIG_CONFIRM_TIMEOUT_MS)) != pdTRUE) {
        if (xSemaphoreTake(state_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (application_state.config_request_id == request_id) {
                application_state.config_request_active = false;
            }
            xSemaphoreGive(state_lock);
        }
        snprintf(message, message_size, "STM32-timeout");
        return ESP_ERR_TIMEOUT;
    }

    if (xSemaphoreTake(state_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        snprintf(message, message_size, "Interne status-timeout");
        return ESP_ERR_TIMEOUT;
    }
    const bool accepted = application_state.config_response_ok;
    const uint32_t revision = application_state.config_response_revision;
    snprintf(message,
             message_size,
             "%s",
             application_state.config_response_message[0] != '\0'
                 ? application_state.config_response_message
                 : (accepted ? "Opgeslagen door STM32" : "Afgewezen door STM32"));
    application_state.config_request_active = false;
    if (accepted) {
        app_model_apply_update(&application_state.config, update, revision);
    }
    xSemaphoreGive(state_lock);
    return accepted ? ESP_OK : ESP_FAIL;
}

static const portal_hooks_t portal_callbacks = {
    .read_status = read_portal_status,
    .read_config = read_portal_config,
    .submit_config = submit_portal_config,
};

static void send_status(uint16_t id)
{
    portal_status_t status;
    read_portal_status(&status);
    char payload[320] = {0};
    protocol_form_append(payload,
                         sizeof(payload),
                         "mode",
                         status.mode == PORTAL_MODE_WIFI ? "wifi" :
                         status.mode == PORTAL_MODE_BLE ? "ble" : "idle");
    protocol_form_append(payload, sizeof(payload), "contact", status.contact_active ? "1" : "0");
    append_u32(payload, sizeof(payload), "seconds_left", status.seconds_left);
    append_u32(payload, sizeof(payload), "config_revision", status.config_revision);
    protocol_form_append(payload, sizeof(payload), "config_ready", status.config_ready ? "1" : "0");
    append_u32(payload,
               sizeof(payload),
               "ble_advertisements",
               status.last_ble_advertisements);
    protocol_form_append(payload,
                         sizeof(payload),
                         "ble_target_seen",
                         status.last_ble_target_seen ? "1" : "0");
    protocol_form_append(payload, sizeof(payload), "write", "native_gatt");
    uart_link_send("STATUS", id, payload);
    protocol_secure_zero(payload, sizeof(payload));
}

static esp_err_t build_victron_result_payload(const victron_result_t *result,
                                              char *payload,
                                              size_t payload_size)
{
    if (result == NULL || payload == NULL || payload_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    payload[0] = '\0';
    esp_err_t status = protocol_form_append(payload,
                                            payload_size,
                                            "ok",
                                            result->code == VICTRON_RESULT_OK ? "1" : "0");
    if (status == ESP_OK) status = protocol_form_append(payload,
                                                        payload_size,
                                                        "code",
                                                        victron_ble_result_name(result->code));
    if (status == ESP_OK) status = append_u32(payload, payload_size,
                                              "result_code", result->code);
    if (status == ESP_OK) status = append_u32(payload, payload_size,
                                              "attempts", result->attempts);
    if (status == ESP_OK) status = append_u32(payload, payload_size,
                                              "initial_value", result->initial_value);
    if (status == ESP_OK) status = append_u32(payload, payload_size,
                                              "verified_value", result->verified_value);
    if (status == ESP_OK) status = append_u32(payload, payload_size,
                                              "load_value", result->load_value);
    if (status == ESP_OK) status = protocol_form_append(payload, payload_size,
                                                        "changed",
                                                        result->changed ? "1" : "0");
    if (status == ESP_OK) status = protocol_form_append(payload, payload_size,
                                                        "target_seen",
                                                        result->target_seen ? "1" : "0");
    if (status == ESP_OK) status = protocol_form_append(payload, payload_size,
                                                        "verified",
                                                        result->verified ? "1" : "0");
    if (status == ESP_OK) status = append_u32(payload, payload_size,
                                              "advertisements", result->advertisements);
    return status;
}

static void send_victron_result(uint16_t request_id,
                                const victron_result_t *result,
                                bool cache)
{
    char payload[256] = {0};
    if (build_victron_result_payload(result, payload, sizeof(payload)) != ESP_OK) {
        protocol_secure_zero(payload, sizeof(payload));
        return;
    }
    if (cache && xSemaphoreTake(state_lock, portMAX_DELAY) == pdTRUE) {
        application_state.victron_cache_valid = true;
        application_state.victron_cache_id = request_id;
        snprintf(application_state.victron_cache_payload,
                 sizeof(application_state.victron_cache_payload),
                 "%s",
                 payload);
        application_state.last_victron_result = *result;
        application_state.victron_request_active = false;
        xSemaphoreGive(state_lock);
    }
    uart_link_send("VICTRON_RESULT", request_id, payload);
    protocol_secure_zero(payload, sizeof(payload));
}

static void send_victron_accepted(uint16_t request_id)
{
    uart_link_send("VICTRON_ACCEPTED", request_id, "proto=1");
}

static bool get_required_u32(const protocol_frame_t *frame,
                             const char *key,
                             uint32_t minimum,
                             uint32_t maximum,
                             uint32_t *value)
{
    char text[16] = {0};
    const bool valid = protocol_form_get(frame->payload,
                                         key,
                                         text,
                                         sizeof(text)) == ESP_OK &&
                       parse_u32(text, minimum, maximum, value);
    protocol_secure_zero(text, sizeof(text));
    return valid;
}

static void handle_portal_start(const protocol_frame_t *frame)
{
    uint32_t seconds = 0;
    char password[64] = {0};
    char ssid[33] = {0};
    const bool seconds_ok = get_required_u32(frame, "seconds", 60, 86400, &seconds);
    const bool password_ok = protocol_form_get(frame->payload,
                                                "password",
                                                password,
                                                sizeof(password)) == ESP_OK &&
                             visible_without_space(password, 12, 63);
    esp_err_t ssid_result = protocol_form_get(frame->payload, "ssid", ssid, sizeof(ssid));
    if (ssid_result == ESP_ERR_NOT_FOUND || ssid[0] == '\0') {
        derive_ssid(ssid);
        ssid_result = ESP_OK;
    }
    const bool ssid_ok = ssid_result == ESP_OK && valid_ssid(ssid);

    if (!seconds_ok || !password_ok || !ssid_ok || frame->id == 0) {
        send_result(frame->id, false, "bad_portal_start");
        protocol_secure_zero(password, sizeof(password));
        protocol_secure_zero(ssid, sizeof(ssid));
        return;
    }

    if (xSemaphoreTake(state_lock, pdMS_TO_TICKS(250)) != pdTRUE) {
        send_result(frame->id, false, "busy");
        protocol_secure_zero(password, sizeof(password));
        protocol_secure_zero(ssid, sizeof(ssid));
        return;
    }
    const bool credentials_changed =
        strcmp(application_state.portal_password, password) != 0 ||
        strcmp(application_state.portal_ssid, ssid) != 0;
    application_state.portal_requested = true;
    application_state.portal_restart = application_state.portal_restart || credentials_changed;
    application_state.portal_deadline_us = esp_timer_get_time() + (int64_t)seconds * 1000000;
    protocol_secure_zero(application_state.portal_password,
                         sizeof(application_state.portal_password));
    memcpy(application_state.portal_password, password, strlen(password) + 1);
    memcpy(application_state.portal_ssid, ssid, strlen(ssid) + 1);
    xSemaphoreGive(state_lock);
    protocol_secure_zero(password, sizeof(password));
    protocol_secure_zero(ssid, sizeof(ssid));
    send_result(frame->id, true, "accepted");
    xTaskNotifyGive(manager_task);
}

static void handle_portal_stop(const protocol_frame_t *frame)
{
    if (frame->id == 0 || xSemaphoreTake(state_lock, pdMS_TO_TICKS(250)) != pdTRUE) {
        send_result(frame->id, false, "busy");
        return;
    }
    application_state.portal_requested = false;
    application_state.portal_restart = false;
    application_state.portal_deadline_us = 0;
    protocol_secure_zero(application_state.portal_password,
                         sizeof(application_state.portal_password));
    xSemaphoreGive(state_lock);
    send_result(frame->id, true, "accepted");
    xTaskNotifyGive(manager_task);
}

static void handle_contact(const protocol_frame_t *frame)
{
    uint32_t active = 0;
    if (frame->id == 0 || !get_required_u32(frame, "active", 0, 1, &active)) {
        send_result(frame->id, false, "bad_contact");
        return;
    }
    if (xSemaphoreTake(state_lock, pdMS_TO_TICKS(250)) != pdTRUE) {
        send_result(frame->id, false, "busy");
        return;
    }
    application_state.contact_active = active != 0;
    xSemaphoreGive(state_lock);
    send_result(frame->id, true, "updated");
}

static void handle_snapshot(const protocol_frame_t *frame)
{
    app_config_snapshot_t snapshot;
    char error[40] = {0};
    if (frame->id == 0 ||
        app_model_snapshot_from_form(frame->payload,
                                     &snapshot,
                                     error,
                                     sizeof(error)) != ESP_OK) {
        send_result(frame->id, false, "bad_snapshot");
        return;
    }
    if (xSemaphoreTake(state_lock, pdMS_TO_TICKS(250)) != pdTRUE) {
        send_result(frame->id, false, "busy");
        return;
    }
    application_state.config = snapshot;
    xSemaphoreGive(state_lock);
    send_result(frame->id, true, "snapshot_loaded");
    maybe_confirm_ota();
}

static void handle_config_result(const protocol_frame_t *frame)
{
    char ok_text[4] = {0};
    char revision_text[16] = {0};
    char message[96] = {0};
    uint32_t ok_number = 0;
    uint32_t revision = 0;
    if (frame->id == 0 ||
        protocol_form_get(frame->payload, "ok", ok_text, sizeof(ok_text)) != ESP_OK ||
        !parse_u32(ok_text, 0, 1, &ok_number)) {
        return;
    }
    if (ok_number != 0 &&
        (protocol_form_get(frame->payload,
                           "revision",
                           revision_text,
                           sizeof(revision_text)) != ESP_OK ||
         !parse_u32(revision_text, 0, UINT32_MAX, &revision))) {
        return;
    }
    if (protocol_form_get(frame->payload, "message", message, sizeof(message)) != ESP_OK) {
        message[0] = '\0';
    }

    if (xSemaphoreTake(state_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (application_state.config_request_active &&
            application_state.config_request_id == frame->id) {
            application_state.config_response_ok = ok_number != 0;
            application_state.config_response_revision = revision;
            snprintf(application_state.config_response_message,
                     sizeof(application_state.config_response_message),
                     "%s",
                     message);
            xSemaphoreGive(config_response_signal);
        }
        xSemaphoreGive(state_lock);
    }
    protocol_secure_zero(ok_text, sizeof(ok_text));
    protocol_secure_zero(revision_text, sizeof(revision_text));
    protocol_secure_zero(message, sizeof(message));
}

static void handle_ble_scan(const protocol_frame_t *frame)
{
    uint32_t duration_ms = 0;
    char target[18] = {0};
    if (frame->id == 0 ||
        !get_required_u32(frame, "duration_ms", 1000, 30000, &duration_ms)) {
        send_result(frame->id, false, "bad_ble_scan");
        return;
    }
    const esp_err_t target_result = protocol_form_get(frame->payload,
                                                       "target_mac",
                                                       target,
                                                       sizeof(target));
    if (target_result != ESP_OK && target_result != ESP_ERR_NOT_FOUND) {
        send_result(frame->id, false, "bad_ble_scan");
        return;
    }

    if (xSemaphoreTake(state_lock, pdMS_TO_TICKS(250)) != pdTRUE) {
        send_result(frame->id, false, "busy");
        return;
    }
    if (application_state.ble_request_pending || application_state.mode == PORTAL_MODE_BLE) {
        xSemaphoreGive(state_lock);
        send_result(frame->id, false, "ble_busy");
        return;
    }
    application_state.ble_request_pending = true;
    application_state.ble_request_id = frame->id;
    application_state.ble_duration_ms = duration_ms;
    snprintf(application_state.ble_target_mac,
             sizeof(application_state.ble_target_mac),
             "%s",
             target);
    xSemaphoreGive(state_lock);
    protocol_secure_zero(target, sizeof(target));
    send_result(frame->id, true, "accepted");
    xTaskNotifyGive(manager_task);
}

static void handle_victron_load_set(const protocol_frame_t *frame)
{
    char cached[256] = {0};
    bool resend_cached = false;
    bool duplicate_active = false;
    bool busy = false;
    if (xSemaphoreTake(state_lock, pdMS_TO_TICKS(250)) == pdTRUE) {
        if (application_state.victron_cache_valid &&
            application_state.victron_cache_id == frame->id) {
            snprintf(cached, sizeof(cached), "%s",
                     application_state.victron_cache_payload);
            resend_cached = true;
        } else if (application_state.victron_request_active) {
            duplicate_active = application_state.victron_request_id == frame->id;
            busy = !duplicate_active;
        }
        xSemaphoreGive(state_lock);
    } else {
        busy = true;
    }
    if (resend_cached) {
        uart_link_send("VICTRON_RESULT", frame->id, cached);
        protocol_secure_zero(cached, sizeof(cached));
        return;
    }
    protocol_secure_zero(cached, sizeof(cached));
    if (duplicate_active) {
        send_victron_accepted(frame->id);
        return;
    }
    if (busy) {
        victron_result_t result = {
            .code = VICTRON_RESULT_BUSY,
            .attempts = 0,
            .initial_value = VICTRON_VALUE_UNKNOWN,
            .verified_value = VICTRON_VALUE_UNKNOWN,
            .load_value = VICTRON_VALUE_UNKNOWN,
        };
        send_victron_result(frame->id, &result, false);
        return;
    }

    victron_request_t request = {
        .address_type = -2,
        .desired_value = 0,
        .driver = 1,
    };
    char number[16] = {0};
    uint32_t parsed = 0;
    int signed_number = -2;
    bool valid = frame->id != 0 &&
                 protocol_form_get(frame->payload, "mac",
                                   request.mac, sizeof(request.mac)) == ESP_OK;
    valid = valid &&
            protocol_form_get(frame->payload, "addr_type",
                              number, sizeof(number)) == ESP_OK &&
            parse_i32(number, -1, 1, &signed_number);
    request.address_type = (int8_t)signed_number;
    protocol_secure_zero(number, sizeof(number));

    valid = valid && get_required_u32(frame, "instance", 0, 23, &parsed);
    request.instance = (uint8_t)parsed;
    valid = valid && get_required_u32(frame, "attempts", 1, 3, &parsed);
    request.max_attempts = (uint8_t)parsed;
    valid = valid && get_required_u32(frame, "pairing", 0, 1, &parsed);
    request.pairing = parsed != 0;
    valid = valid && get_required_u32(frame, "value",
                                      0,
                                      7,
                                      &parsed);
    request.desired_value = (uint8_t)parsed;

    const esp_err_t pin_status = protocol_form_get(frame->payload,
                                                    "pin",
                                                    request.pin,
                                                    sizeof(request.pin));
    if (request.pairing) {
        valid = valid && pin_status == ESP_OK;
    } else {
        valid = valid && (pin_status == ESP_OK || pin_status == ESP_ERR_NOT_FOUND);
        protocol_secure_zero(request.pin, sizeof(request.pin));
    }
    valid=valid && get_required_u32(frame,"generic",0,4,&parsed);
    request.generic_kind=parsed;
    /* Older STM firmware omits driver and remains MPPT-compatible. */
    const esp_err_t driver_status=protocol_form_get(frame->payload,"driver",number,sizeof(number));
    if(driver_status==ESP_OK) {
        uint32_t driver=0;
        valid=valid && parse_u32(number,1,3,&driver);
        request.driver=(uint8_t)driver;
    } else if(driver_status!=ESP_ERR_NOT_FOUND) valid=false;
    if(request.generic_kind) {
        char hex[65]={0};
        valid=valid && (parsed==3 || parsed==4) &&
          protocol_form_get(frame->payload,"service",request.service_uuid,sizeof(request.service_uuid))==ESP_OK &&
          protocol_form_get(frame->payload,"char",request.characteristic_uuid,sizeof(request.characteristic_uuid))==ESP_OK &&
          protocol_form_get(frame->payload,"hex",hex,sizeof(hex))==ESP_OK;
        size_t n=strlen(hex);
        if(!n || n>64 || n%2) valid=false;
        request.value_length=n/2;
        for(size_t i=0;valid && i<n;i+=2) {
            if(!isxdigit((unsigned char)hex[i]) || !isxdigit((unsigned char)hex[i+1])) {valid=false;break;}
            char pair[3]={hex[i],hex[i+1],0};
            request.value[i/2]=(uint8_t)strtoul(pair,NULL,16);
        }
    }
    valid = valid && victron_ble_request_valid(&request);
    if (!valid) {
        victron_result_t result = {
            .code = VICTRON_RESULT_BAD_SETTINGS,
            .attempts = 0,
            .initial_value = VICTRON_VALUE_UNKNOWN,
            .verified_value = VICTRON_VALUE_UNKNOWN,
            .load_value = VICTRON_VALUE_UNKNOWN,
        };
        send_victron_result(frame->id, &result, true);
        victron_ble_clear_request(&request);
        return;
    }

    if (xSemaphoreTake(state_lock, pdMS_TO_TICKS(250)) != pdTRUE) {
        victron_result_t result = {
            .code = VICTRON_RESULT_BUSY,
            .attempts = 0,
            .initial_value = VICTRON_VALUE_UNKNOWN,
            .verified_value = VICTRON_VALUE_UNKNOWN,
            .load_value = VICTRON_VALUE_UNKNOWN,
        };
        send_victron_result(frame->id, &result, false);
        victron_ble_clear_request(&request);
        return;
    }
    if (application_state.victron_request_active) {
        xSemaphoreGive(state_lock);
        victron_result_t result = {
            .code = VICTRON_RESULT_BUSY,
            .attempts = 0,
            .initial_value = VICTRON_VALUE_UNKNOWN,
            .verified_value = VICTRON_VALUE_UNKNOWN,
            .load_value = VICTRON_VALUE_UNKNOWN,
        };
        send_victron_result(frame->id, &result, false);
        victron_ble_clear_request(&request);
        return;
    }
    application_state.victron_request_active = true;
    application_state.victron_work_pending = true;
    application_state.victron_request_id = frame->id;
    application_state.victron_request = request;
    xSemaphoreGive(state_lock);
    victron_ble_clear_request(&request);
    send_victron_accepted(frame->id);
    xTaskNotifyGive(manager_task);
}

static void handle_hello(const protocol_frame_t *frame)
{
    char role[16] = {0};
    uint32_t version = 0;
    if (frame->id == 0 ||
        protocol_form_get(frame->payload, "role", role, sizeof(role)) != ESP_OK ||
        strcmp(role, "stm32") != 0 ||
        !get_required_u32(frame, "proto", 1, 1, &version)) {
        send_result(frame->id, false, "bad_hello");
        return;
    }
    char payload[64] = {0};
    protocol_form_append(payload, sizeof(payload), "role", "esp8684");
    protocol_form_append(payload, sizeof(payload), "proto", "1");
    uart_link_send("HELLO_ACK", frame->id, payload);
    if (xSemaphoreTake(state_lock, pdMS_TO_TICKS(250)) == pdTRUE) {
        application_state.hello_seen = true;
        xSemaphoreGive(state_lock);
    }
}

static void receive_uart_frame(const protocol_frame_t *frame)
{
    if (strncmp(frame->type, "HTTP_", 5) == 0) { portal_receive_frame(frame); return; }
    if (strcmp(frame->type, "HELLO") == 0) {
        handle_hello(frame);
    } else if (strcmp(frame->type, "PORTAL_START") == 0) {
        handle_portal_start(frame);
    } else if (strcmp(frame->type, "PORTAL_STOP") == 0) {
        handle_portal_stop(frame);
    } else if (strcmp(frame->type, "CONTACT") == 0) {
        handle_contact(frame);
    } else if (strcmp(frame->type, "CONFIG_SNAPSHOT") == 0) {
        handle_snapshot(frame);
    } else if (strcmp(frame->type, "CONFIG_RESULT") == 0) {
        handle_config_result(frame);
    } else if (strcmp(frame->type, "STATUS_GET") == 0) {
        send_status(frame->id);
    } else if (strcmp(frame->type, "BLE_SCAN") == 0) {
        handle_ble_scan(frame);
    } else if (strcmp(frame->type, "VICTRON_LOAD_SET") == 0) {
        handle_victron_load_set(frame);
    } else {
        send_result(frame->id, false, "unsupported_type");
    }
}

static void controlled_radio_restart(void)
{
    /* uart_link_send() waits for TX completion before this short guard delay. */
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
}

static esp_err_t manager_stop_portal(const char *reason)
{
    const bool was_running = portal_running();
    const esp_err_t result = portal_stop();
    if (result != ESP_OK) {
        send_portal_state(portal_running(), "stop_failed");
        return result;
    }
    if (was_running) {
        send_portal_state(false, reason);
    }
    if (xSemaphoreTake(state_lock, pdMS_TO_TICKS(250)) == pdTRUE) {
        application_state.mode = PORTAL_MODE_IDLE;
        xSemaphoreGive(state_lock);
    }
    return ESP_OK;
}

static void manager_start_portal(void)
{
    char ssid[33] = {0};
    char password[64] = {0};
    bool should_start = false;

    if (xSemaphoreTake(state_lock, pdMS_TO_TICKS(250)) == pdTRUE) {
        const int64_t now_us = esp_timer_get_time();
        should_start = application_state.portal_requested &&
                       now_us < application_state.portal_deadline_us;
        if (should_start) {
            memcpy(ssid, application_state.portal_ssid, sizeof(ssid));
            memcpy(password, application_state.portal_password, sizeof(password));
        }
        application_state.portal_restart = false;
        xSemaphoreGive(state_lock);
    }

    if (!should_start) {
        protocol_secure_zero(password, sizeof(password));
        return;
    }
    bool restart_required = false;
    const esp_err_t result = portal_start(ssid,
                                          password,
                                          &portal_callbacks,
                                          &restart_required);
    protocol_secure_zero(password, sizeof(password));
    protocol_secure_zero(ssid, sizeof(ssid));

    if (xSemaphoreTake(state_lock, pdMS_TO_TICKS(250)) == pdTRUE) {
        application_state.mode = result == ESP_OK ? PORTAL_MODE_WIFI : PORTAL_MODE_IDLE;
        if (result != ESP_OK) {
            application_state.portal_requested = false;
            application_state.portal_deadline_us = 0;
            protocol_secure_zero(application_state.portal_password,
                                 sizeof(application_state.portal_password));
        }
        xSemaphoreGive(state_lock);
    }
    send_portal_state(result == ESP_OK, result == ESP_OK ? "started" : "start_failed");
    if (result == ESP_OK) {
        maybe_confirm_ota();
    }
    if (restart_required) {
        controlled_radio_restart();
    }
}

static void manager_run_ble(uint16_t request_id,
                            uint32_t duration_ms,
                            const char *target_mac)
{
    ble_scan_result_t scan_result = {
        .status = ESP_ERR_INVALID_STATE,
    };
    esp_err_t status = manager_stop_portal("ble_pause");
    if (status == ESP_OK) {
        if (xSemaphoreTake(state_lock, pdMS_TO_TICKS(250)) == pdTRUE) {
            application_state.mode = PORTAL_MODE_BLE;
            xSemaphoreGive(state_lock);
        }
        status = ble_scan_run(target_mac, duration_ms, &scan_result);
    } else {
        scan_result.status = status;
        scan_result.restart_required = true;
    }

    if (!scan_result.restart_required &&
        xSemaphoreTake(state_lock, pdMS_TO_TICKS(250)) == pdTRUE) {
        application_state.last_ble_result = scan_result;
        if (scan_result.target_requested) {
            application_state.target_state = scan_result.target_seen ? 1 : (status == ESP_OK ? 2 : 3);
            snprintf(application_state.last_target_mac, sizeof(application_state.last_target_mac), "%s", target_mac);
            application_state.target_checked_at_us = esp_timer_get_time();
        }
        application_state.mode = PORTAL_MODE_IDLE;
        xSemaphoreGive(state_lock);
    }

    char payload[180] = {0};
    protocol_form_append(payload, sizeof(payload), "ok", status == ESP_OK ? "1" : "0");
    append_u32(payload, sizeof(payload), "advertisements", scan_result.advertisements);
    protocol_form_append(payload,
                         sizeof(payload),
                         "target_seen",
                         scan_result.target_seen ? "1" : "0");
    protocol_form_append(payload, sizeof(payload), "write", "not_implemented");
    uart_link_send("BLE_RESULT", request_id, payload);
    protocol_secure_zero(payload, sizeof(payload));

    if (scan_result.restart_required) {
        controlled_radio_restart();
        return;
    }

    bool restart = false;
    if (xSemaphoreTake(state_lock, pdMS_TO_TICKS(250)) == pdTRUE) {
        restart = application_state.portal_requested &&
                  esp_timer_get_time() < application_state.portal_deadline_us;
        xSemaphoreGive(state_lock);
    }
    if (restart) {
        manager_start_portal();
    }
}

static void manager_run_victron(uint16_t request_id,
                                victron_request_t *request)
{
    victron_result_t result = {
        .code = VICTRON_RESULT_STACK_ERROR,
        .initial_value = VICTRON_VALUE_UNKNOWN,
        .verified_value = VICTRON_VALUE_UNKNOWN,
        .load_value = VICTRON_VALUE_UNKNOWN,
    };
    const esp_err_t stop_status = manager_stop_portal("ble_pause");
    if (stop_status == ESP_OK) {
        if (xSemaphoreTake(state_lock, pdMS_TO_TICKS(250)) == pdTRUE) {
            application_state.mode = PORTAL_MODE_BLE;
            xSemaphoreGive(state_lock);
        }
        (void)victron_ble_run(request, &result);
    } else {
        result.restart_required = true;
    }
    if (!result.restart_required &&
        xSemaphoreTake(state_lock, pdMS_TO_TICKS(250)) == pdTRUE) {
        application_state.target_state = result.target_seen ? 1 :
            (result.code == VICTRON_RESULT_TARGET_NOT_FOUND ? 2 : 3);
        snprintf(application_state.last_target_mac, sizeof(application_state.last_target_mac), "%s", request->mac);
        application_state.target_checked_at_us = esp_timer_get_time();
        application_state.mode = PORTAL_MODE_IDLE;
        xSemaphoreGive(state_lock);
    }
    victron_ble_clear_request(request);
    send_victron_result(request_id, &result, true);

    if (result.restart_required) {
        /* Never resume WiFi over a BLE host/controller that may still be live. */
        controlled_radio_restart();
        return;
    }

    bool restart = false;
    if (xSemaphoreTake(state_lock, pdMS_TO_TICKS(250)) == pdTRUE) {
        restart = application_state.portal_requested &&
                  esp_timer_get_time() < application_state.portal_deadline_us;
        xSemaphoreGive(state_lock);
    }
    if (restart) {
        manager_start_portal();
    }
}

static void manager_task_main(void *argument)
{
    (void)argument;
    int64_t next_ready_us = 0;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(250));
        // The retained stock bootloader can fill the STM UART receive buffer
        // during OTA. Repeat the startup handshake until the STM has replied;
        // losing the first READY must not leave its ten-minute OTA hold stuck.
        bool announce = false;
        if (uart_started && esp_timer_get_time() >= next_ready_us &&
            xSemaphoreTake(state_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
            announce = !application_state.hello_seen;
            xSemaphoreGive(state_lock);
            next_ready_us = esp_timer_get_time() + 3000000;
        }
        if (announce) send_ready();
        if (portal_ota_in_progress()) {
            /* Never stop WiFi or start BLE while flash is being written. */
            continue;
        }
        const bool verification_pending = ota_guard_pending();

        bool portal_requested = false;
        bool portal_restart = false;
        bool expired = false;
        bool ble_pending = false;
        uint16_t ble_id = 0;
        uint32_t ble_duration = 0;
        char ble_target[18] = {0};
        bool victron_pending = false;
        uint16_t victron_id = 0;
        victron_request_t victron_request = {0};

        if (xSemaphoreTake(state_lock, pdMS_TO_TICKS(250)) == pdTRUE) {
            const int64_t now_us = esp_timer_get_time();
            expired = application_state.portal_requested &&
                      now_us >= application_state.portal_deadline_us;
            if (expired) {
                application_state.portal_requested = false;
                application_state.portal_deadline_us = 0;
                protocol_secure_zero(application_state.portal_password,
                                     sizeof(application_state.portal_password));
            }
            portal_requested = application_state.portal_requested;
            portal_restart = application_state.portal_restart;

            if (!verification_pending && application_state.ble_request_pending) {
                ble_pending = true;
                ble_id = application_state.ble_request_id;
                ble_duration = application_state.ble_duration_ms;
                memcpy(ble_target,
                       application_state.ble_target_mac,
                       sizeof(ble_target));
                application_state.ble_request_pending = false;
                memset(application_state.ble_target_mac,
                       0,
                       sizeof(application_state.ble_target_mac));
            }
            if (!verification_pending && application_state.victron_work_pending) {
                victron_pending = true;
                victron_id = application_state.victron_request_id;
                victron_request = application_state.victron_request;
                application_state.victron_work_pending = false;
                victron_ble_clear_request(&application_state.victron_request);
            }
            xSemaphoreGive(state_lock);
        }

        if (victron_pending) {
            manager_run_victron(victron_id, &victron_request);
            victron_ble_clear_request(&victron_request);
            continue;
        }

        if (ble_pending) {
            manager_run_ble(ble_id, ble_duration, ble_target);
            protocol_secure_zero(ble_target, sizeof(ble_target));
            continue;
        }

        if (expired || !portal_requested) {
            if (manager_stop_portal(expired ? "deadline" : "stopped") != ESP_OK) {
                controlled_radio_restart();
                return;
            }
        } else if (portal_restart) {
            if (manager_stop_portal("restart") != ESP_OK) {
                controlled_radio_restart();
                return;
            }
            manager_start_portal();
        } else if (!portal_running()) {
            manager_start_portal();
        }
    }
}

void app_main(void)
{
    if (ota_guard_start() != ESP_OK) {
        return;
    }
    // No NVS init/erase/write: radio NVS and persistent BLE bonds are disabled.
    if (esp_netif_init() != ESP_OK ||
        esp_event_loop_create_default() != ESP_OK) {
        return;
    }

    state_lock = xSemaphoreCreateMutex();
    config_response_signal = xSemaphoreCreateBinary();
    if (state_lock == NULL || config_response_signal == NULL) {
        return;
    }
    app_model_snapshot_defaults(&application_state.config);
    application_state.mode = PORTAL_MODE_IDLE;
    application_state.last_ble_result.status = ESP_ERR_INVALID_STATE;

    if (xTaskCreate(manager_task_main,
                    "radio_manager",
                    6144,
                    NULL,
                    7,
                    &manager_task) != pdPASS) {
        return;
    }
    if (uart_link_start(receive_uart_frame) != ESP_OK) {
        return;
    }

    uart_started = true;
}
