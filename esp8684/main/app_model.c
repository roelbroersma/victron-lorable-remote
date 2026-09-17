#include "app_model.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "protocol.h"

static void set_error(char *error, size_t error_size, const char *message)
{
    if (error != NULL && error_size > 0) {
        snprintf(error, error_size, "%s", message);
    }
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

static bool normalize_hex(const char *input,
                          size_t expected_digits,
                          bool allow_empty,
                          char *output,
                          size_t output_size)
{
    if (output_size < expected_digits + 1) {
        return false;
    }
    const bool genuinely_empty = input == NULL || input[0] == '\0';
    size_t written = 0;
    for (const char *cursor = input; cursor != NULL && *cursor != '\0'; ++cursor) {
        const unsigned char value = (unsigned char)*cursor;
        if (value == ':' || value == '-' || value == ' ') {
            continue;
        }
        if (!isxdigit(value) || written >= expected_digits) {
            return false;
        }
        output[written++] = (char)toupper(value);
    }
    if (written == 0 && allow_empty && genuinely_empty) {
        output[0] = '\0';
        return true;
    }
    if (written != expected_digits) {
        output[0] = '\0';
        return false;
    }
    bool any_nonzero = false;
    for (size_t index = 0; index < written; ++index) {
        if (output[index] != '0') {
            any_nonzero = true;
            break;
        }
    }
    if (!any_nonzero) {
        output[0] = '\0';
        return false;
    }
    output[written] = '\0';
    return true;
}

static bool normalize_mac(const char *input, bool allow_empty, char output[18])
{
    char compact[13] = {0};
    if (!normalize_hex(input, 12, allow_empty, compact, sizeof(compact))) {
        return false;
    }
    if (compact[0] == '\0') {
        output[0] = '\0';
        return true;
    }
    snprintf(output,
             18,
             "%.2s:%.2s:%.2s:%.2s:%.2s:%.2s",
             compact,
             compact + 2,
             compact + 4,
             compact + 6,
             compact + 8,
             compact + 10);
    return true;
}

static bool six_digits(const char *value)
{
    if (value == NULL || strlen(value) != 6) {
        return false;
    }
    for (size_t index = 0; index < 6; ++index) {
        if (!isdigit((unsigned char)value[index])) {
            return false;
        }
    }
    return true;
}

static bool valid_wifi_password(const char *value)
{
    if (value == NULL) {
        return false;
    }
    const size_t length = strlen(value);
    if (length < 12 || length > 63) {
        return false;
    }
    for (size_t index = 0; index < length; ++index) {
        const unsigned char byte = (unsigned char)value[index];
        if (byte < 0x21 || byte > 0x7e) {
            return false;
        }
    }
    return true;
}

static bool valid_wifi_ssid(const char *value)
{
    if (value == NULL) {
        return false;
    }
    const size_t length = strlen(value);
    if (length < 1 || length > 32) {
        return false;
    }
    for (size_t index = 0; index < length; ++index) {
        const unsigned char byte = (unsigned char)value[index];
        if (byte < 0x20 || byte > 0x7e) {
            return false;
        }
    }
    return true;
}

static esp_err_t required(const char *form,
                          const char *key,
                          char *value,
                          size_t value_size,
                          char *error,
                          size_t error_size)
{
    const esp_err_t result = protocol_form_get(form, key, value, value_size);
    if (result != ESP_OK) {
        set_error(error, error_size, key);
    }
    return result;
}

void app_model_snapshot_defaults(app_config_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->address_type = -1;
    snapshot->device_instance = 3;
    snapshot->status_minutes = 15;
    snapshot->wifi_minutes = 60;
    snapshot->ble_attempts = 3;
    snprintf(snapshot->wifi_ssid,
             sizeof(snapshot->wifi_ssid),
             "RAK-A231-SETUP");
}

static esp_err_t parse_public_values(const char *form,
                                     bool snapshot_mode,
                                     app_config_snapshot_t *values,
                                     char *error,
                                     size_t error_size)
{
    char text[40] = {0};
    uint32_t number = 0;
    int signed_number = 0;

    app_model_snapshot_defaults(values);

    if (required(form, "revision", text, sizeof(text), error, error_size) != ESP_OK ||
        !parse_u32(text, 0, UINT32_MAX, &values->revision)) {
        set_error(error, error_size, "revision");
        return ESP_ERR_INVALID_ARG;
    }

    if (required(form, "victron_mac", text, sizeof(text), error, error_size) != ESP_OK ||
        !normalize_mac(text, true, values->victron_mac)) {
        set_error(error, error_size, "victron_mac");
        return ESP_ERR_INVALID_ARG;
    }

    if (required(form, "addr_type", text, sizeof(text), error, error_size) != ESP_OK ||
        !parse_i32(text, -1, 1, &signed_number)) {
        set_error(error, error_size, "addr_type");
        return ESP_ERR_INVALID_ARG;
    }
    values->address_type = (int8_t)signed_number;

    if (required(form, "device_instance", text, sizeof(text), error, error_size) != ESP_OK ||
        !parse_u32(text, 0, 23, &number)) {
        set_error(error, error_size, "device_instance");
        return ESP_ERR_INVALID_ARG;
    }
    values->device_instance = (uint8_t)number;

    if (required(form, "pairing", text, sizeof(text), error, error_size) != ESP_OK ||
        !parse_u32(text, 0, 1, &number)) {
        set_error(error, error_size, "pairing");
        return ESP_ERR_INVALID_ARG;
    }
    values->pairing_enabled = number != 0;

    if (snapshot_mode) {
        if (required(form, "pin_set", text, sizeof(text), error, error_size) != ESP_OK ||
            !parse_u32(text, 0, 1, &number)) {
            set_error(error, error_size, "pin_set");
            return ESP_ERR_INVALID_ARG;
        }
        values->pin_set = number != 0;
    }

    if (required(form, "status_minutes", text, sizeof(text), error, error_size) != ESP_OK ||
        !parse_u32(text, 1, 1440, &number)) {
        set_error(error, error_size, "status_minutes");
        return ESP_ERR_INVALID_ARG;
    }
    values->status_minutes = (uint16_t)number;

    if (required(form, "wifi_minutes", text, sizeof(text), error, error_size) != ESP_OK ||
        !parse_u32(text, 5, 240, &number)) {
        set_error(error, error_size, "wifi_minutes");
        return ESP_ERR_INVALID_ARG;
    }
    values->wifi_minutes = (uint16_t)number;

    if (required(form, "ble_attempts", text, sizeof(text), error, error_size) != ESP_OK ||
        !parse_u32(text, 1, 3, &number)) {
        set_error(error, error_size, "ble_attempts");
        return ESP_ERR_INVALID_ARG;
    }
    values->ble_attempts = (uint8_t)number;

    const esp_err_t ssid_result = protocol_form_get(form,
                                                     "wifi_ssid",
                                                     text,
                                                     sizeof(text));
    if (ssid_result == ESP_ERR_NOT_FOUND && snapshot_mode) {
        /* Accept a snapshot from the pre-SSID STM firmware during upgrades. */
    } else if (ssid_result != ESP_OK || !valid_wifi_ssid(text)) {
        set_error(error, error_size, "wifi_ssid");
        return ESP_ERR_INVALID_ARG;
    } else {
        memcpy(values->wifi_ssid, text, strlen(text) + 1);
    }

    if (required(form, "deveui", text, sizeof(text), error, error_size) != ESP_OK ||
        !normalize_hex(text, 16, false, values->deveui, sizeof(values->deveui))) {
        set_error(error, error_size, "deveui");
        return ESP_ERR_INVALID_ARG;
    }

    if (required(form, "joineui", text, sizeof(text), error, error_size) != ESP_OK ||
        !normalize_hex(text, 16, false, values->joineui, sizeof(values->joineui))) {
        set_error(error, error_size, "joineui");
        return ESP_ERR_INVALID_ARG;
    }

    values->valid = true;
    protocol_secure_zero(text, sizeof(text));
    return ESP_OK;
}

esp_err_t app_model_snapshot_from_form(const char *form,
                                       app_config_snapshot_t *snapshot,
                                       char *error,
                                       size_t error_size)
{
    if (form == NULL || snapshot == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    app_config_snapshot_t parsed;
    const esp_err_t result = parse_public_values(form, true, &parsed, error, error_size);
    if (result == ESP_OK) {
        *snapshot = parsed;
    }
    return result;
}

esp_err_t app_model_update_from_form(const char *form,
                                     app_config_update_t *update,
                                     char *error,
                                     size_t error_size)
{
    if (form == NULL || update == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(update, 0, sizeof(*update));

    esp_err_t result = parse_public_values(form,
                                           false,
                                           &update->public_values,
                                           error,
                                           error_size);
    if (result != ESP_OK) {
        return result;
    }

    char secret[40] = {0};
    result = protocol_form_get(form, "pin", secret, sizeof(secret));
    if (result == ESP_OK && secret[0] != '\0') {
        if (!six_digits(secret)) {
            set_error(error, error_size, "pin");
            protocol_secure_zero(secret, sizeof(secret));
            return ESP_ERR_INVALID_ARG;
        }
        memcpy(update->pin, secret, sizeof(update->pin));
        update->pin_changed = true;
    } else if (result != ESP_OK && result != ESP_ERR_NOT_FOUND) {
        set_error(error, error_size, "pin");
        protocol_secure_zero(secret, sizeof(secret));
        return result;
    }
    protocol_secure_zero(secret, sizeof(secret));

    result = protocol_form_get(form, "appkey", secret, sizeof(secret));
    if (result == ESP_OK && secret[0] != '\0') {
        if (!normalize_hex(secret, 32, false, update->appkey, sizeof(update->appkey))) {
            set_error(error, error_size, "appkey");
            protocol_secure_zero(secret, sizeof(secret));
            app_model_clear_update_secrets(update);
            return ESP_ERR_INVALID_ARG;
        }
        update->appkey_changed = true;
    } else if (result != ESP_OK && result != ESP_ERR_NOT_FOUND) {
        set_error(error, error_size, "appkey");
        protocol_secure_zero(secret, sizeof(secret));
        app_model_clear_update_secrets(update);
        return result;
    }
    protocol_secure_zero(secret, sizeof(secret));

    char wifi_secret[64] = {0};
    result = protocol_form_get(form, "wifi_password", wifi_secret, sizeof(wifi_secret));
    if (result == ESP_OK && wifi_secret[0] != '\0') {
        if (!valid_wifi_password(wifi_secret)) {
            set_error(error, error_size, "wifi_password");
            protocol_secure_zero(wifi_secret, sizeof(wifi_secret));
            app_model_clear_update_secrets(update);
            return ESP_ERR_INVALID_ARG;
        }
        memcpy(update->wifi_password, wifi_secret, sizeof(update->wifi_password));
        update->wifi_password_changed = true;
    } else if (result != ESP_OK && result != ESP_ERR_NOT_FOUND) {
        set_error(error, error_size, "wifi_password");
        protocol_secure_zero(wifi_secret, sizeof(wifi_secret));
        app_model_clear_update_secrets(update);
        return result;
    }
    protocol_secure_zero(wifi_secret, sizeof(wifi_secret));
    return ESP_OK;
}

static esp_err_t append_number(char *form,
                               size_t form_size,
                               const char *key,
                               long value)
{
    char text[16];
    snprintf(text, sizeof(text), "%ld", value);
    return protocol_form_append(form, form_size, key, text);
}

esp_err_t app_model_update_to_form(const app_config_update_t *update,
                                   char *form,
                                   size_t form_size)
{
    if (update == NULL || form == NULL || form_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    form[0] = '\0';
    esp_err_t result = append_number(form, form_size, "revision", update->public_values.revision);
    if (result == ESP_OK) result = protocol_form_append(form, form_size, "victron_mac",
                                            update->public_values.victron_mac);
    if (result == ESP_OK) result = append_number(form, form_size, "addr_type", update->public_values.address_type);
    if (result == ESP_OK) result = append_number(form, form_size, "device_instance", update->public_values.device_instance);
    if (result == ESP_OK) result = append_number(form, form_size, "pairing", update->public_values.pairing_enabled ? 1 : 0);
    if (result == ESP_OK) result = append_number(form, form_size, "status_minutes", update->public_values.status_minutes);
    if (result == ESP_OK) result = append_number(form, form_size, "wifi_minutes", update->public_values.wifi_minutes);
    if (result == ESP_OK) result = append_number(form, form_size, "ble_attempts", update->public_values.ble_attempts);
    if (result == ESP_OK) result = protocol_form_append(form, form_size, "wifi_ssid", update->public_values.wifi_ssid);
    if (result == ESP_OK) result = protocol_form_append(form, form_size, "deveui", update->public_values.deveui);
    if (result == ESP_OK) result = protocol_form_append(form, form_size, "joineui", update->public_values.joineui);
    if (result == ESP_OK && update->pin_changed) result = protocol_form_append(form, form_size, "pin", update->pin);
    if (result == ESP_OK && update->appkey_changed) result = protocol_form_append(form, form_size, "appkey", update->appkey);
    if (result == ESP_OK && update->wifi_password_changed) result = protocol_form_append(form, form_size, "wifi_password", update->wifi_password);
    return result;
}

void app_model_apply_update(app_config_snapshot_t *snapshot,
                            const app_config_update_t *update,
                            uint32_t revision)
{
    if (snapshot == NULL || update == NULL) {
        return;
    }
    const bool old_pin_set = snapshot->pin_set;
    *snapshot = update->public_values;
    snapshot->revision = revision;
    snapshot->pin_set = old_pin_set || update->pin_changed;
    snapshot->valid = true;
}

void app_model_clear_update_secrets(app_config_update_t *update)
{
    if (update == NULL) {
        return;
    }
    protocol_secure_zero(update->pin, sizeof(update->pin));
    protocol_secure_zero(update->appkey, sizeof(update->appkey));
    protocol_secure_zero(update->wifi_password, sizeof(update->wifi_password));
    update->pin_changed = false;
    update->appkey_changed = false;
    update->wifi_password_changed = false;
}
