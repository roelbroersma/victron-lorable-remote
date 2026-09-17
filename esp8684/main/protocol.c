#include "protocol.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mbedtls/base64.h"

static int hex_value(char value)
{
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    return -1;
}

void protocol_secure_zero(void *data, size_t length)
{
    volatile uint8_t *cursor = (volatile uint8_t *)data;
    while (length-- > 0) {
        *cursor++ = 0;
    }
}

uint16_t protocol_crc16_ccitt_false(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xffff;

    for (size_t index = 0; index < length; ++index) {
        crc ^= (uint16_t)data[index] << 8;
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000u) != 0u ? (uint16_t)((crc << 1) ^ 0x1021u)
                                         : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

bool protocol_type_is_valid(const char *type)
{
    if (type == NULL) {
        return false;
    }

    const size_t length = strlen(type);
    if (length == 0 || length > PROTOCOL_TYPE_MAX) {
        return false;
    }

    for (size_t index = 0; index < length; ++index) {
        const unsigned char value = (unsigned char)type[index];
        if (!(value == '_' || (value >= 'A' && value <= 'Z') || isdigit(value))) {
            return false;
        }
    }
    return true;
}

esp_err_t protocol_encode(char *destination,
                          size_t destination_size,
                          const char *type,
                          uint16_t id,
                          const char *payload)
{
    if (destination == NULL || destination_size == 0 || !protocol_type_is_valid(type)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (payload == NULL) {
        payload = "";
    }
    const size_t payload_length = strlen(payload);
    if (payload_length > PROTOCOL_PAYLOAD_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }

    char base64[PROTOCOL_B64_MAX + 1] = {0};
    size_t base64_length = 0;
    int result = mbedtls_base64_encode((unsigned char *)base64,
                                       sizeof(base64) - 1,
                                       &base64_length,
                                       (const unsigned char *)payload,
                                       payload_length);
    if (result != 0 || base64_length > PROTOCOL_B64_MAX) {
        protocol_secure_zero(base64, sizeof(base64));
        return ESP_ERR_INVALID_SIZE;
    }
    base64[base64_length] = '\0';

    char checked[PROTOCOL_FRAME_MAX] = {0};
    const int checked_length = snprintf(checked,
                                        sizeof(checked),
                                        "%u|%s|%04X|%s",
                                        PROTOCOL_VERSION,
                                        type,
                                        id,
                                        base64);
    if (checked_length < 0 || (size_t)checked_length >= sizeof(checked)) {
        protocol_secure_zero(base64, sizeof(base64));
        protocol_secure_zero(checked, sizeof(checked));
        return ESP_ERR_INVALID_SIZE;
    }

    const uint16_t crc = protocol_crc16_ccitt_false((const uint8_t *)checked,
                                                     (size_t)checked_length);
    const int frame_length = snprintf(destination,
                                      destination_size,
                                      "@%s|%04X\r\n",
                                      checked,
                                      crc);
    protocol_secure_zero(base64, sizeof(base64));
    protocol_secure_zero(checked, sizeof(checked));
    if (frame_length < 0 || (size_t)frame_length >= destination_size ||
        frame_length > PROTOCOL_FRAME_MAX) {
        if (destination_size > 0) {
            destination[0] = '\0';
        }
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

esp_err_t protocol_decode(const char *line, protocol_frame_t *frame)
{
    if (line == NULL || frame == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const size_t raw_length = strnlen(line, PROTOCOL_FRAME_MAX + 1);
    if (raw_length == 0 || raw_length > PROTOCOL_FRAME_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }

    char work[PROTOCOL_FRAME_MAX + 1] = {0};
    memcpy(work, line, raw_length);
    size_t length = raw_length;
    while (length > 0 && (work[length - 1] == '\r' || work[length - 1] == '\n')) {
        work[--length] = '\0';
    }
    if (length < 10 || work[0] != '@') {
        protocol_secure_zero(work, sizeof(work));
        return ESP_ERR_INVALID_ARG;
    }

    char *crc_separator = strrchr(work + 1, '|');
    if (crc_separator == NULL || strlen(crc_separator + 1) != 4) {
        protocol_secure_zero(work, sizeof(work));
        return ESP_ERR_INVALID_ARG;
    }
    const int c0 = hex_value(crc_separator[1]);
    const int c1 = hex_value(crc_separator[2]);
    const int c2 = hex_value(crc_separator[3]);
    const int c3 = hex_value(crc_separator[4]);
    if (c0 < 0 || c1 < 0 || c2 < 0 || c3 < 0) {
        protocol_secure_zero(work, sizeof(work));
        return ESP_ERR_INVALID_ARG;
    }
    const uint16_t received_crc = (uint16_t)((c0 << 12) | (c1 << 8) | (c2 << 4) | c3);
    const size_t checked_length = (size_t)(crc_separator - (work + 1));
    const uint16_t expected_crc = protocol_crc16_ccitt_false((const uint8_t *)(work + 1),
                                                              checked_length);
    if (received_crc != expected_crc) {
        protocol_secure_zero(work, sizeof(work));
        return ESP_ERR_INVALID_CRC;
    }
    *crc_separator = '\0';

    char *version = work + 1;
    char *separator1 = strchr(version, '|');
    if (separator1 == NULL) {
        protocol_secure_zero(work, sizeof(work));
        return ESP_ERR_INVALID_ARG;
    }
    *separator1 = '\0';
    char *type = separator1 + 1;
    char *separator2 = strchr(type, '|');
    if (separator2 == NULL) {
        protocol_secure_zero(work, sizeof(work));
        return ESP_ERR_INVALID_ARG;
    }
    *separator2 = '\0';
    char *id_text = separator2 + 1;
    char *separator3 = strchr(id_text, '|');
    if (separator3 == NULL || strchr(separator3 + 1, '|') != NULL) {
        protocol_secure_zero(work, sizeof(work));
        return ESP_ERR_INVALID_ARG;
    }
    *separator3 = '\0';
    char *base64 = separator3 + 1;

    if (strcmp(version, "1") != 0 || !protocol_type_is_valid(type) || strlen(id_text) != 4) {
        protocol_secure_zero(work, sizeof(work));
        return ESP_ERR_INVALID_ARG;
    }
    unsigned long parsed_id = 0;
    for (size_t index = 0; index < 4; ++index) {
        const int value = hex_value(id_text[index]);
        if (value < 0) {
            protocol_secure_zero(work, sizeof(work));
            return ESP_ERR_INVALID_ARG;
        }
        parsed_id = (parsed_id << 4) | (unsigned)value;
    }
    if (strlen(base64) > PROTOCOL_B64_MAX) {
        protocol_secure_zero(work, sizeof(work));
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t decoded[PROTOCOL_PAYLOAD_MAX + 1] = {0};
    size_t decoded_length = 0;
    const int result = mbedtls_base64_decode(decoded,
                                             PROTOCOL_PAYLOAD_MAX,
                                             &decoded_length,
                                             (const unsigned char *)base64,
                                             strlen(base64));
    if (result != 0 || decoded_length > PROTOCOL_PAYLOAD_MAX ||
        memchr(decoded, '\0', decoded_length) != NULL) {
        protocol_secure_zero(decoded, sizeof(decoded));
        protocol_secure_zero(work, sizeof(work));
        return ESP_ERR_INVALID_ARG;
    }
    decoded[decoded_length] = '\0';

    memset(frame, 0, sizeof(*frame));
    memcpy(frame->type, type, strlen(type));
    frame->id = (uint16_t)parsed_id;
    memcpy(frame->payload, decoded, decoded_length + 1);

    protocol_secure_zero(decoded, sizeof(decoded));
    protocol_secure_zero(work, sizeof(work));
    return ESP_OK;
}

static bool form_safe(unsigned char value)
{
    return isalnum(value) || value == '-' || value == '_' || value == '.' || value == '~';
}

esp_err_t protocol_form_append(char *form,
                               size_t form_size,
                               const char *key,
                               const char *value)
{
    if (form == NULL || form_size == 0 || key == NULL || value == NULL || key[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    for (const char *cursor = key; *cursor != '\0'; ++cursor) {
        if (!(isalnum((unsigned char)*cursor) || *cursor == '_')) {
            return ESP_ERR_INVALID_ARG;
        }
    }

    size_t used = strnlen(form, form_size);
    if (used >= form_size) {
        return ESP_ERR_INVALID_SIZE;
    }
    const size_t key_length = strlen(key);
    const size_t prefix = used == 0 ? 0 : 1;
    if (used + prefix + key_length + 1 >= form_size) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (prefix != 0) {
        form[used++] = '&';
    }
    memcpy(form + used, key, key_length);
    used += key_length;
    form[used++] = '=';

    static const char hex[] = "0123456789ABCDEF";
    for (const unsigned char *cursor = (const unsigned char *)value; *cursor != '\0'; ++cursor) {
        if (form_safe(*cursor)) {
            if (used + 1 >= form_size) {
                return ESP_ERR_INVALID_SIZE;
            }
            form[used++] = (char)*cursor;
        } else {
            if (used + 3 >= form_size) {
                return ESP_ERR_INVALID_SIZE;
            }
            form[used++] = '%';
            form[used++] = hex[*cursor >> 4];
            form[used++] = hex[*cursor & 0x0f];
        }
    }
    form[used] = '\0';
    return ESP_OK;
}

esp_err_t protocol_form_get(const char *form,
                            const char *key,
                            char *destination,
                            size_t destination_size)
{
    if (form == NULL || key == NULL || destination == NULL || destination_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    destination[0] = '\0';

    const size_t key_length = strlen(key);
    const char *cursor = form;
    const char *match = NULL;
    size_t match_length = 0;
    unsigned matches = 0;

    while (*cursor != '\0') {
        const char *end = strchr(cursor, '&');
        if (end == NULL) {
            end = cursor + strlen(cursor);
        }
        const char *equals = memchr(cursor, '=', (size_t)(end - cursor));
        if (equals != NULL && (size_t)(equals - cursor) == key_length &&
            memcmp(cursor, key, key_length) == 0) {
            match = equals + 1;
            match_length = (size_t)(end - match);
            ++matches;
        }
        cursor = *end == '&' ? end + 1 : end;
    }

    if (matches == 0) {
        return ESP_ERR_NOT_FOUND;
    }
    if (matches != 1) {
        return ESP_ERR_INVALID_STATE;
    }

    size_t written = 0;
    for (size_t index = 0; index < match_length; ++index) {
        unsigned char value = (unsigned char)match[index];
        if (value == '+') {
            value = ' ';
        } else if (value == '%') {
            if (index + 2 >= match_length) {
                return ESP_ERR_INVALID_ARG;
            }
            const int high = hex_value(match[++index]);
            const int low = hex_value(match[++index]);
            if (high < 0 || low < 0) {
                return ESP_ERR_INVALID_ARG;
            }
            value = (unsigned char)((high << 4) | low);
            if (value == 0) {
                return ESP_ERR_INVALID_ARG;
            }
        }
        if (written + 1 >= destination_size) {
            destination[0] = '\0';
            return ESP_ERR_INVALID_SIZE;
        }
        destination[written++] = (char)value;
    }
    destination[written] = '\0';
    return ESP_OK;
}
