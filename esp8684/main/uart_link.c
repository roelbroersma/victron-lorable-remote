#include "uart_link.h"

#include <string.h>

#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static uart_link_frame_handler_t frame_handler;
static SemaphoreHandle_t transmit_lock;

static void uart_receive_task(void *argument)
{
    (void)argument;
    uint8_t bytes[64];
    char line[PROTOCOL_FRAME_MAX + 1];
    size_t used = 0;
    bool collecting = false;

    for (;;) {
        const int count = uart_read_bytes(UART_LINK_PORT,
                                          bytes,
                                          sizeof(bytes),
                                          pdMS_TO_TICKS(100));
        for (int index = 0; index < count; ++index) {
            const char value = (char)bytes[index];
            if (value == '@') {
                protocol_secure_zero(line, sizeof(line));
                line[0] = '@';
                used = 1;
                collecting = true;
                continue;
            }
            if (!collecting) {
                continue;
            }
            if (value == '\n') {
                line[used] = '\0';
                protocol_frame_t frame;
                if (protocol_decode(line, &frame) == ESP_OK && frame_handler != NULL) {
                    frame_handler(&frame);
                }
                protocol_secure_zero(&frame, sizeof(frame));
                protocol_secure_zero(line, sizeof(line));
                used = 0;
                collecting = false;
                continue;
            }
            if (used >= PROTOCOL_FRAME_MAX) {
                protocol_secure_zero(line, sizeof(line));
                used = 0;
                collecting = false;
                continue;
            }
            line[used++] = value;
        }
    }
}

esp_err_t uart_link_start(uart_link_frame_handler_t handler)
{
    if (handler == NULL || frame_handler != NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    transmit_lock = xSemaphoreCreateMutex();
    if (transmit_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const uart_config_t configuration = {
        .baud_rate = UART_LINK_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t result = uart_driver_install(UART_LINK_PORT, 1024, 1024, 0, NULL, 0);
    if (result != ESP_OK) {
        return result;
    }
    result = uart_param_config(UART_LINK_PORT, &configuration);
    if (result != ESP_OK) {
        uart_driver_delete(UART_LINK_PORT);
        return result;
    }
    result = uart_set_pin(UART_LINK_PORT,
                          UART_LINK_TX_GPIO,
                          UART_LINK_RX_GPIO,
                          UART_PIN_NO_CHANGE,
                          UART_PIN_NO_CHANGE);
    if (result != ESP_OK) {
        uart_driver_delete(UART_LINK_PORT);
        return result;
    }

    frame_handler = handler;
    if (xTaskCreate(uart_receive_task,
                    "stm32_uart",
                    8192,
                    NULL,
                    8,
                    NULL) != pdPASS) {
        frame_handler = NULL;
        uart_driver_delete(UART_LINK_PORT);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t uart_link_send(const char *type, uint16_t id, const char *payload)
{
    if (transmit_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    char frame[PROTOCOL_FRAME_MAX + 1] = {0};
    esp_err_t result = protocol_encode(frame, sizeof(frame), type, id, payload);
    if (result != ESP_OK) {
        protocol_secure_zero(frame, sizeof(frame));
        return result;
    }

    if (xSemaphoreTake(transmit_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        protocol_secure_zero(frame, sizeof(frame));
        return ESP_ERR_TIMEOUT;
    }
    const size_t length = strlen(frame);
    // RUI's short RX ring drops long bursts; pace every frame, including replies.
    size_t written = 0;
    while (written < length) {
        size_t chunk = length - written;
        if (chunk > 32) chunk = 32;
        if (uart_write_bytes(UART_LINK_PORT, frame + written, chunk) != (int)chunk) break;
        uart_wait_tx_done(UART_LINK_PORT, pdMS_TO_TICKS(1000));
        written += chunk;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    xSemaphoreGive(transmit_lock);
    protocol_secure_zero(frame, sizeof(frame));
    return written == (int)length ? ESP_OK : ESP_FAIL;
}
