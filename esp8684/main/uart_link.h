#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UART_LINK_PORT UART_NUM_1
#define UART_LINK_TX_GPIO 7
#define UART_LINK_RX_GPIO 6
#define UART_LINK_BAUD_RATE 115200

typedef void (*uart_link_frame_handler_t)(const protocol_frame_t *frame);

esp_err_t uart_link_start(uart_link_frame_handler_t handler);
esp_err_t uart_link_send(const char *type, uint16_t id, const char *payload);
typedef void (*uart_raw_handler_t)(uint8_t byte);
void uart_link_raw_mode(uart_raw_handler_t handler);
esp_err_t uart_link_raw_write(const void *bytes, size_t length);

#ifdef __cplusplus
}
#endif
