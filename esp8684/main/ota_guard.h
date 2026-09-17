#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Start the rollback watchdog when the bootloader marks this OTA image as
 * pending verification. A wired first flash has no pending OTA state.
 */
esp_err_t ota_guard_start(void);

/* Mark a pending image valid only after STM32 configuration and WiFi work. */
esp_err_t ota_guard_confirm(void);

bool ota_guard_pending(void);

#ifdef __cplusplus
}
#endif
