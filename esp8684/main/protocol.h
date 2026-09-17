#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PROTOCOL_VERSION 1
#define PROTOCOL_TYPE_MAX 20
#define PROTOCOL_PAYLOAD_MAX 384
#define PROTOCOL_B64_MAX 512
#define PROTOCOL_FRAME_MAX 768

typedef struct {
    char type[PROTOCOL_TYPE_MAX + 1];
    uint16_t id;
    char payload[PROTOCOL_PAYLOAD_MAX + 1];
} protocol_frame_t;

uint16_t protocol_crc16_ccitt_false(const uint8_t *data, size_t length);

esp_err_t protocol_encode(char *destination,
                          size_t destination_size,
                          const char *type,
                          uint16_t id,
                          const char *payload);

esp_err_t protocol_decode(const char *line, protocol_frame_t *frame);

esp_err_t protocol_form_get(const char *form,
                            const char *key,
                            char *destination,
                            size_t destination_size);

esp_err_t protocol_form_append(char *form,
                               size_t form_size,
                               const char *key,
                               const char *value);

bool protocol_type_is_valid(const char *type);
void protocol_secure_zero(void *data, size_t length);

#ifdef __cplusplus
}
#endif
