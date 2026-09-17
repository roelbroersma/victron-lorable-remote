#include "ota_guard.h"
// Stock 2MB bootloader has one application slot. No rollback can be promised.
// Integrity/target checks belong to the compressed-storage uploader.
esp_err_t ota_guard_start(void) { return ESP_OK; }
esp_err_t ota_guard_confirm(void) { return ESP_OK; }
bool ota_guard_pending(void) { return false; }
